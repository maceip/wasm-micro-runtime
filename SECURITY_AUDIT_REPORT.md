# Security Audit Report: WAMR JIT, Native Dispatch, and libc-builtin

**Date:** 2026-07-02  
**Scope:** JIT compilation, native function dispatch, libc-builtin wrappers, AOT relocation, fast interpreter, and C API  
**Auditor:** Automated Security Review

---

## Finding 1: Stale `bytes` Variable in `WASM_OP_ATOMIC_NOTIFY` JIT Compilation

**Severity: HIGH**  
**File:** `core/iwasm/fast-jit/jit_frontend.c:2453-2456`  
**CWE:** CWE-457 (Use of Uninitialized Variable / Stale State)

### Description

In the `jit_compile_func` function, the `WASM_OP_ATOMIC_NOTIFY` case passes the
local variable `bytes` to `jit_compiler_op_atomic_notify`, but `bytes` is never
explicitly set before this case. Instead, it retains whatever value was assigned
by a previously processed opcode (e.g., `WASM_OP_I32_LOAD8_U` sets `bytes = 1`).

```c
// jit_frontend.c:2453
case WASM_OP_ATOMIC_NOTIFY:
    if (!jit_compiler_op_atomic_notify(cc, align, offset,
                                       bytes))  // <-- stale
        return false;
    break;
```

Contrast with `WASM_OP_ATOMIC_WAIT32` and `WASM_OP_ATOMIC_WAIT64` which
correctly pass explicit literal values (4 and 8 respectively).

### What the Attacker Controls

A malicious WASM module author controls:
- The sequence of opcodes in a function body (to set `bytes` to a favorable value)
- The `addr` and `offset` operands of the `atomic.notify` instruction

### Attack Chain

1. Craft a WASM function with an `i32.load8_u` instruction (which sets `bytes = 1`)
   followed by an `atomic.notify` instruction.
2. When the fast-JIT compiles this function, `jit_compiler_op_atomic_notify` is called
   with `bytes = 1`.
3. Inside `jit_compiler_op_atomic_notify`, `check_and_seek(cc, addr, offset, bytes)`
   selects `mem_bound_check_1byte` (= `mem_size - 1`) instead of the correct
   `mem_bound_check_4bytes` (= `mem_size - 4`).
4. An `addr + offset` value in the range `[mem_size - 3, mem_size - 1]` passes the
   1-byte boundary check but represents an out-of-bounds 4-byte access location.
5. This out-of-bounds address is passed to `wasm_runtime_atomic_notify`. Depending on
   the implementation of that function, this could lead to an out-of-bounds memory
   read or other undefined behavior.

### Existing Mitigations

- If `OS_ENABLE_HW_BOUND_CHECK` is enabled, the software bounds check is skipped
  entirely (hardware guard pages provide protection instead), making this bug
  irrelevant on those platforms.
- `wasm_runtime_atomic_notify` may not directly dereference the address (it may
  use it only as a key), which could limit exploitability.
- The initial value of `bytes` is 4, so the bug only manifests when a sub-4-byte
  memory operation precedes `atomic.notify` in the same function.

### Recommendation

Explicitly set `bytes = 4` before the `WASM_OP_ATOMIC_NOTIFY` case, matching the
WebAssembly specification requirement that `atomic.notify` operates on a 4-byte
aligned i32 address.

---

## Finding 2: Stale `sign` Variable in Atomic Load JIT Compilation

**Severity: HIGH**  
**File:** `core/iwasm/fast-jit/jit_frontend.c:2464-2493`  
**CWE:** CWE-457 (Use of Uninitialized Variable / Stale State)

### Description

All atomic load opcodes (`WASM_OP_ATOMIC_I32_LOAD8_U`, `WASM_OP_ATOMIC_I32_LOAD16_U`,
`WASM_OP_ATOMIC_I64_LOAD8_U`, `WASM_OP_ATOMIC_I64_LOAD16_U`,
`WASM_OP_ATOMIC_I64_LOAD32_U`) are specified as unsigned (`_U`) in the WebAssembly
threads proposal, but the `sign` variable passed to `jit_compile_op_i32_load` /
`jit_compile_op_i64_load` is never set before these cases. It retains whatever value
was assigned by a previously processed regular load opcode.

```c
// jit_frontend.c:2467-2475
case WASM_OP_ATOMIC_I32_LOAD8_U:
    bytes = 1;
    goto op_atomic_i32_load;
case WASM_OP_ATOMIC_I32_LOAD16_U:
    bytes = 2;
op_atomic_i32_load:
    if (!jit_compile_op_i32_load(cc, align, offset, bytes,
                                 sign, true))  // <-- sign is stale
        return false;
    break;
```

### What the Attacker Controls

- The opcode sequence within a function (to make `sign = true` before an atomic
  unsigned load)
- The memory contents being loaded

### Attack Chain

1. Craft a WASM function where `i32.load8_s` (which sets `sign = true`) precedes
   `i32.atomic.load8_u`.
2. The JIT compiler generates sign-extending code (`LDI8`) instead of zero-extending
   code (`LDU8`) for the atomic load.
3. A byte value `0x80` is loaded as `0xFFFFFF80` (sign-extended) instead of `0x00000080`
   (zero-extended).
4. Subsequent computations using this value produce incorrect results. This can corrupt
   program logic, potentially leading to:
   - Array index miscalculation causing out-of-bounds access within the sandbox
   - Incorrect control flow decisions (e.g., a comparison that should succeed now fails)
   - Security-relevant state corruption (e.g., a length or permission value computed
     incorrectly)

### Existing Mitigations

- The `sign` variable defaults to `true`, so the bug manifests even without a
  preceding signed load.
- The corrupted value remains within the WASM sandbox; it cannot directly
  escape the sandbox boundary.
- AOT and classic interpreter paths may not share this bug.

### Recommendation

Explicitly set `sign = false` for all atomic load cases, since the WebAssembly
threads specification defines all sub-word atomic loads as unsigned.

---

## Findings Examined but Not Confirmed as Exploitable

### JIT Memory Bounds Checks (`jit_emit_memory.c`)

The bounds-checking logic in `check_and_seek_on_64bit_platform` correctly
zero-extends the 32-bit WASM address to 64-bit before adding the static offset,
preventing integer overflow in the effective address computation. The subsequent
comparison against the pre-computed `memory_boundary` register is correct.
On 32-bit platforms, an explicit overflow check (`offset1 < addr`) is present.
No vulnerability found.

### Native Function Signature Validation (`wasm_native.c`)

The `check_symbol_signature` function properly validates that the WASM function
type matches the registered native signature before allowing dispatch. Mismatches
cause the import resolution to return NULL, preventing type confusion. The
`wasm_native_resolve_symbol` function correctly rejects functions with invalid
signatures (line 237-244). No vulnerability found.

### libc-builtin Format String Handling (`libc_builtin_wrapper.c`)

The `_vprintf_wa` function implements its own format string parser rather than
passing guest-controlled format strings directly to host `printf`. This prevents
classic format string attacks. Key mitigations observed:
- `%n` is neutered to a no-op (line 301-303), preventing write-what-where attacks
- `%s` arguments are validated via `validate_app_str_addr` before use (line 229)
- All numeric format outputs use bounded `snprintf` into fixed-size buffers (line 202, 211)
- The `CHECK_VA_ARG` macro prevents reading past the end of the va_list region (line 55-63)
- `sprintf_wrapper` limits output to the remaining valid memory region (line 427-434)

No exploitable vulnerability found.

### AOT x86-64 Relocation Handling (`aot_reloc_x86_64.c`)

The `apply_relocation` function validates relocation offsets via
`check_reloc_offset` before applying them. Truncation checks are present for
32-bit relocations (`R_X86_64_PC32`, `R_X86_64_32`, `R_X86_64_32S`). The
`reloc_addend` value from the AOT file is inherently trusted (AOT modules are
equivalent to native code). No vulnerability beyond the AOT trust boundary.

### Fast Interpreter Bounds Checks (`wasm_interp_fast.c`)

The `CHECK_MEMORY_OVERFLOW` macro correctly computes the effective address as
`uint64` (line 48: `uint64 offset1 = (uint64)offset + (uint64)addr`), preventing
32-bit overflow. The subsequent check `offset1 + bytes <= get_linear_mem_size()`
uses 64-bit arithmetic, is correct, and accounts for the access width. The
configurable bounds-check disable (`WASM_CONFIGURABLE_BOUNDS_CHECKS`) is an
intentional embedder-controlled feature, not a vulnerability. No vulnerability found.

### C API Integer Overflow (`wasm_c_api.c`)

The `malloc_internal` function checks `size < UINT32_MAX` before allocation
(line 98). The `WASM_DEFINE_VEC_PLAIN` and `WASM_DEFINE_VEC_OWN` macros compute
`size_in_bytes = (uint32)(size * sizeof(...))`, which could theoretically truncate.
However, `bh_vector_init` -> `alloc_vector_data` independently validates that the
total allocation size fits in `uint32` (bh_vector.c:14-16), causing the allocation
to fail before the truncated `size_in_bytes` is used. No practically exploitable
overflow found.
