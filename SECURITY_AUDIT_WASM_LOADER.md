# Security Audit Report: wasm_loader.c — WASM Binary Parser

**Date:** 2026-07-02
**Scope:** `core/iwasm/interpreter/wasm_loader.c` (~17,000 lines)
**Focus:** Integer overflow, bounds check bypass, OOB access, index validation, debug/release divergence

---

## Executive Summary

`wasm_loader.c` is the main WASM binary parser that processes untrusted input. Overall, it is
significantly more robust than `wasm_mini_loader.c`: its `CHECK_BUF` macro uses real function
calls with error returns (not `bh_assert`), so bounds checks remain active in release builds.
Allocation paths consistently use `uint64` intermediate sizes and `loader_malloc` which caps
at `UINT32_MAX`.

That said, the audit identified **three medium-severity and one low-medium-severity** confirmed
issues with real (though in some cases constrained) attack paths, plus one defense-in-depth
concern. No critical-severity findings were identified.

---

## Finding 1 — `skip_leb` macros completely ignore bounds parameter

| Field | Value |
|---|---|
| **Severity** | MEDIUM |
| **Location** | `wasm_loader.c:138-153` (definitions), `wasm_loader.c:7517-8340` (~65 call sites) |
| **Type** | Out-of-bounds read due to missing bounds check |

### Description

The `skip_leb` family of macros accepts a `p_end` parameter but completely ignores it:

```c
#define skip_leb(p) while (*p++ & 0x80)
#define skip_leb_int64(p, p_end) skip_leb(p)
#define skip_leb_uint32(p, p_end) skip_leb(p)
#define skip_leb_int32(p, p_end) skip_leb(p)
#define skip_leb_mem_offset(p, p_end) skip_leb(p)
#define skip_leb_memidx(p, p_end) skip_leb(p)
```

The expansion `while (*p++ & 0x80)` will keep reading memory with no upper bound until it
finds a byte with the high bit clear. It is used ~65 times inside
`wasm_loader_find_block_addr()`.

### Attack Path

`wasm_loader_find_block_addr()` is called at **runtime** from the classic interpreter
(`wasm_interp_classic.c`), with `code_end_addr` set to `(uint8 *)-1`:

```c
// wasm_interp_classic.c:2005-2008
if (!wasm_loader_find_block_addr(
        exec_env, (BlockAddr *)exec_env->block_addr_cache,
        lookup_cursor, (uint8 *)-1, LABEL_TYPE_TRY,
        &else_addr, &end_addr)) {
```

This means the `while (p < code_end_addr)` loop at line 7546 provides **no protection**—the
pointer can advance arbitrarily far. The attacker cannot directly reach this path from raw
input (bytecode is validated first by `wasm_loader_prepare_bytecode`), but:

1. If there is **any** validation bypass in `wasm_loader_prepare_bytecode` that allows a
   malformed LEB encoding to pass, `skip_leb` will read unbounded memory.
2. If the bytecode buffer is modified between validation and interpretation (e.g., through a
   use-after-free or a shared-memory race), the same occurs.
3. The contract between `skip_leb_*` and its callers is deceptive: callers pass `p_end` as
   though it constrains the read, but it is silently discarded.

**Impact:** Out-of-bounds heap read; potential information disclosure. On some platforms,
could cause a crash (denial of service) if the read crosses into unmapped memory.

### Mitigations

- Bytecode is validated by `wasm_loader_prepare_bytecode` before `wasm_loader_find_block_addr`
  is called. If validation is correct, all LEB sequences are well-formed.
- However, defense-in-depth is absent: there is zero safety net.

### Recommendation

Replace `skip_leb(p)` with a bounds-checked variant:

```c
#define skip_leb_checked(p, p_end)      \
    do {                                \
        if (p >= p_end) goto fail;      \
        while (*p++ & 0x80) {           \
            if (p >= p_end) goto fail;  \
        }                               \
    } while (0)
```

---

## Finding 2 — Missing `is_indices_overflow` for table and memory index spaces

| Field | Value |
|---|---|
| **Severity** | MEDIUM |
| **Location** | `wasm_loader.c:4134-4232` (`load_table_section`), `wasm_loader.c:4237-4279` (`load_memory_section`) |
| **Type** | Integer overflow in index bounds check |

### Description

The loader validates that `import_count + defined_count` does not overflow `UINT32_MAX` for
functions (line 3871), globals (line 4296), and tags (line 5285) using `is_indices_overflow()`.
However, this check is **missing** for tables and memories.

For tables in `load_table_section` (line 4143):
```c
if (module->import_table_count + table_count > 1) {
    // In REF_TYPES/GC mode: just sets is_ref_types_used = true
    // No overflow check
}
```

For memories in `load_memory_section` (line 4249):
```c
if (module->import_memory_count + memory_count > 1) {
    // Only checked in non-MULTI_MEMORY mode
}
```

Subsequent index validation checks use unchecked additions:
```c
// load_export_section, line 4523:
if (index >= module->table_count + module->import_table_count) {
    // This addition can overflow, bypassing the bounds check
}
```

### Attack Path (Tables — requires WASM_ENABLE_REF_TYPES or WASM_ENABLE_GC)

1. Attacker crafts a WASM binary with a large import_table_count (many table imports in the
   import section) and a table_count such that their sum exceeds UINT32_MAX.
2. The allocation `sizeof(WASMTable) * (uint64)table_count` is safe (uses uint64 + loader_malloc
   cap), so the table array allocation does not overflow.
3. However, subsequent index checks like
   `index >= module->table_count + module->import_table_count` overflow to a small value,
   allowing the attacker to use an out-of-range export index that passes validation.
4. At instantiation or runtime, the out-of-range index is used to access memory beyond the
   allocated table arrays.

### Practical Constraints

- Exploiting this requires allocating `import_table_count` import entries (each `sizeof(WASMImport)` ≈
  100+ bytes), requiring a multi-gigabyte WASM binary or a very large memory allocation.
  On 32-bit platforms with limited address space, this is infeasible.
- On 64-bit platforms with abundant memory, the attack becomes theoretically possible but
  requires significant memory consumption.

### Mitigations

- Without `WASM_ENABLE_REF_TYPES` and `WASM_ENABLE_GC`, tables are limited to 1 total.
- Without `WASM_ENABLE_MULTI_MEMORY`, memories are limited to 1 total.
- Memory allocation limits provide a practical (but not guaranteed) barrier.

### Recommendation

Add `is_indices_overflow()` checks in `load_table_section` and `load_memory_section`,
consistent with the pattern already used for functions, globals, and tags:

```c
// In load_table_section, after read_leb_uint32(p, p_end, table_count):
if (is_indices_overflow(module->import_table_count, table_count,
                        error_buf, error_buf_size))
    return false;

// In load_memory_section, after read_leb_uint32(p, p_end, memory_count):
if (is_indices_overflow(module->import_memory_count, memory_count,
                        error_buf, error_buf_size))
    return false;
```

---

## Finding 3 — Unchecked pointer arithmetic in `load_function_section` (32-bit)

| Field | Value |
|---|---|
| **Severity** | LOW-MEDIUM |
| **Location** | `wasm_loader.c:3903` |
| **Type** | Pointer arithmetic wraparound on 32-bit platforms |

### Description

The code size check in `load_function_section` uses raw pointer comparison:

```c
read_leb_uint32(p_code, buf_code_end, code_size);
if (code_size == 0 || p_code + code_size > buf_code_end) {
    // error
}
```

On 32-bit platforms, `p_code + code_size` can wrap around to a small value when `p_code` is
near the top of the address space and `code_size` is large. This would bypass the bounds check.
The established `check_buf()` function handles this correctly with `(uintptr_t)buf + length < (uintptr_t)buf`,
but that function is not used here.

### Attack Path (32-bit only)

1. On a 32-bit platform, attacker crafts a WASM binary with a function whose `code_size`
   LEB value, when added to the current `p_code` pointer, wraps past `0xFFFFFFFF`.
2. The wrapped result `p_code + code_size` is smaller than `buf_code_end`, so the check passes.
3. `p_code_end = p_code + code_size` is set to the wrapped (incorrect) value.
4. Subsequent operations using `p_code_end` as a boundary operate with a wrong limit,
   potentially leading to OOB reads of the WASM binary buffer.

### Mitigations

- On 64-bit platforms, pointer width is 64-bit and this wraparound is infeasible.
- The outer `CHECK_BUF1(p, p_end, section_size)` in `create_sections()` (line 6965) ensures
  the section body is within the loaded buffer. Since `p_code` starts within the section and
  `code_size` comes from within the section, the wrap requires `p_code` to be near
  `0xFFFFFFFF`, which is unusual for heap allocations on 32-bit.

### Recommendation

Replace the raw comparison with the overflow-safe `check_buf` pattern:

```c
if (code_size == 0
    || !check_buf(p_code, buf_code_end, code_size, error_buf, error_buf_size)) {
    set_error_buf(error_buf, error_buf_size, "invalid function code size");
    return false;
}
```

---

## Finding 4 — `read_leb_quick` has no bounds checking in fast-interp second pass

| Field | Value |
|---|---|
| **Severity** | MEDIUM |
| **Location** | `wasm_loader.c:11640-11660` (definition), `wasm_loader.c:11662-11695` (macros) |
| **Type** | Out-of-bounds read due to missing bounds check |

### Description

In `WASM_ENABLE_FAST_INTERP` mode, `wasm_loader_prepare_bytecode` runs twice. The first pass
uses `read_leb_uint32` with full format and bounds checking. The second pass uses
`read_leb_quick`, which is unbounded:

```c
static uint64
read_leb_quick(uint8 **p_buf, uint32 maxbits, bool sign)
{
    uint8 *buf = *p_buf;
    uint64 result = 0, byte = 0;
    uint32 shift = 0;
    do {
        byte = *buf++;
        result |= ((byte & 0x7f) << shift);
        shift += 7;
    } while (byte & 0x80);
    // ...
}
```

The `do { ... } while (byte & 0x80)` loop reads bytes without any bounds check. The `p_end`
parameter passed to the `pb_read_leb_uint32` wrapper is discarded in the second pass:

```c
#define pb_read_leb_uint32(p, p_end, res)                 \
    do {                                                  \
        if (!loader_ctx->p_code_compiled)                 \
            read_leb_uint32(p, p_end, res);               \
        else                                              \
            res = (uint32)read_leb_quick(&p, 32, false);  \
    } while (0)
```

### Attack Path

The second pass relies on the first pass having validated all LEB sequences. If:

1. The bytecode buffer is mutated between the two passes (e.g., by another thread if the
   buffer is in shared memory, or by a bug in the first pass's bytecode rewriting), or
2. The first pass's emitted code has different structure than expected by the second pass
   (a logic error in the fast-interp emission),

then `read_leb_quick` can read beyond the function's code boundary.

### Mitigations

- The first pass performs thorough validation.
- The bytecode buffer is typically in a single-owner allocation not accessible to WASM guest
  code.

### Recommendation

Add a bounds check to `read_leb_quick` or to the `pb_read_leb_*` macros for the second pass.
At minimum, pass `p_end` through and add a single `if (buf >= p_end) return 0;` guard in the
loop.

---

## Positive Finding — `CHECK_BUF` in wasm_loader.c is properly implemented

| Field | Value |
|---|---|
| **Category** | Defense-in-depth: ADEQUATE |
| **Location** | `wasm_loader.c:99-136` |

### Description

Unlike `wasm_mini_loader.c`, where `CHECK_BUF` is implemented as `bh_assert()` (a no-op in
release builds), `wasm_loader.c` implements `CHECK_BUF` as a real function call that:

1. Checks for pointer arithmetic wraparound: `(uintptr_t)buf + length < (uintptr_t)buf`
2. Checks bounds: `(uintptr_t)buf + length > (uintptr_t)buf_end`
3. Sets an error message and returns `false` on failure
4. The macro uses `goto fail` on failure, providing consistent error handling

**wasm_loader.c (safe):**
```c
static bool
check_buf(const uint8 *buf, const uint8 *buf_end, uint32 length, ...)
{
    if ((uintptr_t)buf + length < (uintptr_t)buf
        || (uintptr_t)buf + length > (uintptr_t)buf_end) {
        set_error_buf(error_buf, error_buf_size, "unexpected end ...");
        return false;
    }
    return true;
}
```

**wasm_mini_loader.c (UNSAFE in release):**
```c
#define CHECK_BUF(buf, buf_end, length) \
    do { \
        bh_assert(buf + length >= buf && buf + length <= buf_end); \
    } while (0)
```

This is a critical difference. The `wasm_loader.c` parser is safe against CHECK_BUF bypass
in release builds.

---

## Summary of Other Checked Areas (No Issues Found)

| Area | Status | Notes |
|---|---|---|
| Allocation overflow in `loader_malloc` | **Safe** | Uses `uint64` size, checks `>= UINT32_MAX` |
| Function index validation | **Safe** | `is_indices_overflow` + per-use `check_function_index` |
| Type index validation | **Safe** | `check_type_index` with proper bounds |
| Global index validation | **Safe** | `is_indices_overflow` + per-use validation |
| Tag index validation | **Safe** | `is_indices_overflow` + per-use validation |
| `load_type_section` (GC rec groups) | **Safe** | Overflow check before realloc, bounds check on type indices |
| `load_init_expr` | **Safe** | Proper CHECK_BUF usage, index validation for global.get/ref.func |
| `load_data_segment_section` | **Safe** | data_seg_idx validated, memory index validated |
| `load_table_segment_section` | **Safe** | table_index validated via `check_table_index` |
| `load_export_section` | **Safe** | Index validated per export kind (but see Finding 2 for overflow) |
| Local count overflow | **Safe** | `sub_local_count > UINT32_MAX - local_count` check |
| `local_cell_num` overflow | **Safe** | Checked against `UINT16_MAX` |
| Section ordering/duplication | **Safe** | `create_sections` enforces ordering |
| `TEMPLATE_READ_VALUE` alignment | **Non-issue** | Targets x86/ARM which support unaligned access |

---

## Risk Matrix

| Finding | Severity | Exploitability | Requires |
|---|---|---|---|
| 1: `skip_leb` unbounded read | Medium | Low (requires validation bypass or memory corruption) | Classic interpreter mode |
| 2: Missing `is_indices_overflow` for tables/memories | Medium | Low (requires multi-GB input) | `WASM_ENABLE_REF_TYPES` or `WASM_ENABLE_MULTI_MEMORY` |
| 3: Pointer wraparound in code_size check | Low-Medium | Low (32-bit only, unusual allocator layout) | 32-bit platform |
| 4: `read_leb_quick` unbounded in fast-interp | Medium | Low (requires corruption between passes) | `WASM_ENABLE_FAST_INTERP` |
