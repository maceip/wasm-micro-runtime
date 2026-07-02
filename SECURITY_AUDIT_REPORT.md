# Security Audit Report: WAMR Thread Management, GC Objects, and Runtime Instantiation

**Date:** 2026-07-02
**Scope:** Thread lifecycle, GC object handling, runtime instantiation, ELF parsing, attestation code
**Auditor:** Automated Security Review

---

## Finding 1: ELF Parser — Out-of-Bounds Reads via Unvalidated Header Fields

**Severity:** HIGH
**File:** `core/iwasm/aot/debug/elf_parser.c`
**Prerequisite:** `WASM_ENABLE_DEBUG_AOT` is enabled

### Description

The `get_text_section()` function parses ELF headers from an in-memory buffer but never receives or validates the buffer size. Multiple fields read from the (potentially attacker-controlled) ELF header are used as offsets and indices without any bounds checking, enabling out-of-bounds reads.

### Specific Locations

**1a. `e_shstrndx` used without bounds check (lines 122, 145)**

```c
sh_str = get_section64(eh, sh_table[eh->e_shstrndx]);
```

`e_shstrndx` is used to index into `sh_table` (allocated with `e_shnum` entries) but is never validated to be less than `e_shnum`. A crafted ELF with `e_shstrndx >= e_shnum` causes a heap out-of-bounds read. The read value is then dereferenced as a `Elf64_Shdr*`, and its `sh_offset` field is used to compute a pointer — producing an arbitrary read primitive.

**1b. `e_shoff` used without bounds check (lines 67, 80)**

```c
buf += eh->e_shoff;
```

`e_shoff` is added to the buffer base pointer to locate the section header table. If `e_shoff` exceeds the actual buffer size, the subsequent loop reads memory beyond the buffer.

**1c. `sh_offset` used without bounds check (lines 92, 99)**

```c
return buf + section_header->sh_offset;
```

`get_section()` / `get_section64()` return a pointer computed from `sh_offset` without validating it against the buffer boundary. This pointer is used as the string table base for `strcmp` calls (`is_text_section`), which read until a null terminator.

**1d. `sh_name` used without bounds check (lines 124, 147)**

```c
if (is_text_section(sh_str + sh_table[i]->sh_name))
```

`sh_name` is an offset into the string section but is never validated. Combined with an already-unvalidated `sh_str`, this provides unbounded read access via `strcmp`.

### What an Attacker Controls

An attacker who can supply a crafted AOT module (with an embedded ELF object) controls all ELF header fields: `e_shoff`, `e_shnum`, `e_shstrndx`, `e_shentsize`, and all section header fields (`sh_offset`, `sh_name`, `sh_size`).

### Attack Chain

1. Attacker crafts an AOT module whose text section contains a malformed ELF object.
2. The AOT loader calls `get_text_section()` on this ELF data (aot_loader.c:2759).
3. Setting `e_shstrndx` to a value >= `e_shnum` causes an out-of-bounds read from the `sh_table` heap allocation, interpreting adjacent heap memory as a section header pointer.
4. The dereferenced pointer's `sh_offset` field yields an arbitrary read target relative to the ELF buffer base.
5. `strcmp` on the resulting pointer reads memory until a null byte, potentially leaking sensitive data or causing a crash.

### Existing Mitigations

- Only active when `WASM_ENABLE_DEBUG_AOT` is enabled (not a default build option).
- AOT modules are typically loaded from trusted sources in production deployments.
- Some deployments use guard pages that would cause a fault on large out-of-bounds access.

### Recommended Fix

Add a `buf_size` parameter to `get_text_section()` and validate all offsets and indices:
- Check `e_shstrndx < e_shnum`.
- Check `e_shoff + e_shnum * e_shentsize <= buf_size`.
- Check `sh_offset + sh_size <= buf_size` for each accessed section header.
- Check `sh_name` falls within the bounds of the string table section.

---

## Finding 2: librats Evidence Parsing — Buffer Overflow via Unvalidated Quote Length

**Severity:** HIGH
**File:** `core/iwasm/libraries/lib-rats/lib_rats_wrapper.c:82-83`
**Prerequisite:** `WASM_ENABLE_LIB_RATS` is enabled (SGX/TEE environment)

### Description

In `librats_parse_evidence_wrapper()`, the `att_ev.ecdsa.quote_len` value (parsed from attacker-provided JSON evidence) is used as both the source length and the **destination capacity** in `bh_memcpy_s`, instead of the actual destination buffer size (`SGX_QUOTE_MAX_SIZE = 8192`). This bypasses the safe-copy bounds check.

### Specific Location

```c
// Line 82-83
bh_memcpy_s(evidence->quote, att_ev.ecdsa.quote_len, att_ev.ecdsa.quote,
            att_ev.ecdsa.quote_len);
```

The `evidence->quote` field is a fixed 8192-byte buffer (`uint8_t quote[SGX_QUOTE_MAX_SIZE]`), but `bh_memcpy_s` is told the destination has `att_ev.ecdsa.quote_len` bytes available. Since `b_memcpy_s` checks `slen > dlen` (and both are `quote_len`), the check always passes regardless of `quote_len`'s value. If `quote_len > 8192`, the copy writes past `evidence->quote` into subsequent struct fields and potentially past the struct entirely.

### What an Attacker Controls

- The `evidence_json` parameter: the attacker provides the JSON string from which `att_ev.ecdsa.quote_len` and `att_ev.ecdsa.quote` are parsed.
- The `evidence` pointer location and `evidence_size` in WASM linear memory.

### Attack Chain

1. WASM code calls `librats_parse_evidence` with crafted JSON containing a quote > 8192 bytes.
2. `get_evidence_from_json` parses the JSON and sets `att_ev.ecdsa.quote_len` to the oversized value.
3. `bh_memcpy_s` copies `quote_len` bytes into the 8192-byte `evidence->quote` buffer.
4. The overflow corrupts subsequent `rats_sgx_evidence_t` fields (`quote_size`, `user_data`, `mr_enclave`, etc.) and may extend past the struct.
5. If `evidence` is placed near the end of WASM linear memory, the overflow can write past the linear memory boundary. Without hardware guard pages, this corrupts host memory.

### Additional Issue: `evidence_size` Parameter Ignored

The function signature includes `uint32_t evidence_size` (validated by the WAMR runtime via the `*~` native signature), but the function body never uses this parameter. The function always writes `sizeof(rats_sgx_evidence_t)` bytes regardless of `evidence_size`. If a WASM module passes `evidence_size < sizeof(rats_sgx_evidence_t)`, writes extend past the runtime-validated memory region.

### Existing Mitigations

- Only available in SGX/TEE builds with `WASM_ENABLE_LIB_RATS`.
- The `evidence_json` content comes from attestation infrastructure, not arbitrary external input in typical deployments.
- Some platforms use hardware guard pages that would fault on out-of-bounds writes.

### Recommended Fix

```c
if (att_ev.ecdsa.quote_len > SGX_QUOTE_MAX_SIZE) {
    return -1;
}
if (evidence_size < sizeof(rats_sgx_evidence_t)) {
    return -1;
}
bh_memcpy_s(evidence->quote, SGX_QUOTE_MAX_SIZE,
            att_ev.ecdsa.quote, att_ev.ecdsa.quote_len);
```

---

## Finding 3: librats Evidence Parsing — `evidence_size` Not Validated Against Struct Size

**Severity:** HIGH
**File:** `core/iwasm/libraries/lib-rats/lib_rats_wrapper.c:65-97`
**Prerequisite:** `WASM_ENABLE_LIB_RATS` is enabled

### Description

The `librats_parse_evidence_wrapper` function receives an `evidence_size` parameter that the WAMR runtime uses to validate the `evidence` pointer covers `evidence_size` bytes within WASM linear memory. However, the function ignores `evidence_size` entirely and writes a full `sizeof(rats_sgx_evidence_t)` worth of data (over 8400 bytes) across multiple `bh_memcpy_s` calls and direct field assignments (lines 82-94).

### Specific Location

```c
// evidence_size is declared but never referenced in function body
static int
librats_parse_evidence_wrapper(wasm_exec_env_t exec_env,
                               const char *evidence_json, uint32_t json_size,
                               rats_sgx_evidence_t *evidence,
                               uint32_t evidence_size)
{
    // ... evidence_size never checked or used ...
    bh_memcpy_s(evidence->quote, ...);      // writes up to 8192 bytes
    evidence->quote_size = ...;              // +4 bytes
    bh_memcpy_s(evidence->user_data, ...);   // +64 bytes
    bh_memcpy_s(evidence->mr_enclave, ...);  // +32 bytes
    bh_memcpy_s(evidence->mr_signer, ...);   // +32 bytes
    evidence->product_id = ...;              // +4 bytes
    evidence->security_version = ...;        // +4 bytes
    evidence->att_flags = ...;               // +8 bytes
    evidence->att_xfrm = ...;               // +8 bytes
}
```

### Attack Chain

1. WASM code calls `librats_parse_evidence` with `evidence_size = 4` and `evidence` pointing to the last 4 bytes of WASM linear memory.
2. The WAMR runtime validates that 4 bytes are accessible — the check passes.
3. The native function writes the full struct (~8400 bytes), overflowing past the end of WASM linear memory.
4. Without guard pages, this corrupts host heap/stack memory.

### Existing Mitigations

Same as Finding 2.

### Recommended Fix

Add a check at function entry:

```c
if (evidence_size < sizeof(rats_sgx_evidence_t)) {
    return -1;
}
```

---

## Files Reviewed With No Confirmed HIGH/CRITICAL Findings

### `core/iwasm/libraries/thread-mgr/thread_manager.c`

The thread lifecycle management uses a consistent locking protocol: `cluster_list_lock` is always acquired before `cluster->lock` when both are needed, preventing deadlocks. The `safe_traverse_exec_env_list` function correctly handles concurrent list modifications by re-reading from the head after each visitor call. Exception propagation uses the global `_exception_lock`. No confirmed race conditions leading to exploitable use-after-free were found.

### `core/iwasm/libraries/lib-wasi-threads/lib_wasi_threads_wrapper.c` and `tid_allocator.c`

Thread ID allocation/deallocation is correctly serialized via `thread_id_lock`. The `tid_allocator_get_tid` function has proper overflow checks during resize (lines 47-57). Thread IDs are released slightly before the thread finishes cleanup (line 65 of `lib_wasi_threads_wrapper.c`), creating a narrow window for ID reuse, but this produces application-level confusion rather than a memory safety violation.

### `core/iwasm/common/gc/gc_object.c`

Array objects have a length limit of `(1 << 29) - 1` (line 213), which prevents integer overflow in `elem_size * length` computations (max: `8 * 536870911 = 4294967288 < UINT32_MAX`). The interpreter validates bounds before calling `wasm_array_obj_set_elem`, `wasm_array_obj_fill`, and `wasm_array_obj_copy`. Struct field access uses `field_idx < struct_type->field_count` assertions. No type confusion paths were found — the `rtt_type->type_flag` is checked via assertions before casting.

### `core/iwasm/common/gc/gc_type.c`

Subtype checking in `wasm_type_is_supers_of` correctly walks the parent chain and is bounded by `inherit_depth_diff`. The `wasm_reftype_is_subtype_of` function is complex but correctly handles all reference type hierarchy cases. No unbounded recursion or type confusion was found.

### `core/iwasm/interpreter/wasm_runtime.c`

Module instantiation uses `uint64` for size computations and checks against `UINT32_MAX` before calling allocators. Memory data segment initialization properly validates `base_offset + length <= memory_size` (lines 2839-2876). Global initialization recursion depth is bounded by the type hierarchy depth. No integer overflow in allocation sizing was confirmed.
