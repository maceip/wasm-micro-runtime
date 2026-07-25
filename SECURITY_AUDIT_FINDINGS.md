# WAMR Security Audit Findings

## Finding 1: Fast Interpreter `SET_GLOBAL_AUX_STACK` Truncates 64-bit Value with Memory64

**Severity:** HIGH  
**File:** `core/iwasm/interpreter/wasm_interp_fast.c`  
**Lines:** 3686-3697

### Vulnerable Code

```c
HANDLE_OP(WASM_OP_SET_GLOBAL_AUX_STACK)
{
    uint64 aux_stack_top;

    global_idx = read_uint32(frame_ip);
    bh_assert(global_idx < module->e->global_count);
    global = globals + global_idx;
    global_addr = get_global_addr(global_data, global);
    /* TODO: Memory64 the data type depends on mem idx type */
    aux_stack_top = (uint64)frame_lp[GET_OFFSET()];  // <-- BUG: reads only 32 bits
    if (aux_stack_top <= (uint64)exec_env->aux_stack_boundary) {
        wasm_set_exception(module, "wasm auxiliary stack overflow");
        goto got_exception;
    }
    if (aux_stack_top > (uint64)exec_env->aux_stack_bottom) {
        wasm_set_exception(module,
                           "wasm auxiliary stack underflow");
        goto got_exception;
    }
    *(int32 *)global_addr = (uint32)aux_stack_top;  // <-- Also writes only 32 bits
```

### Description

When `WASM_ENABLE_MEMORY64` is enabled, the auxiliary stack top should be a 64-bit value.
The classic interpreter correctly handles this at lines 4352-4379 of `wasm_interp_classic.c`
by checking `is_memory64` and reading 8 bytes when Memory64 is active. The fast interpreter
always reads only a 32-bit value (`frame_lp[GET_OFFSET()]`) and zero-extends it to 64-bit.

### Attack Path

A malicious WASM module compiled for Memory64 can set the aux stack pointer to a 64-bit
value whose upper 32 bits are non-zero and whose lower 32 bits, when zero-extended, pass
the boundary check. For example, if `aux_stack_boundary = 0x1000` and the real 64-bit
value is `0x00000001_00000800`, the fast interpreter reads only `0x00000800`, which is
less than the boundary, and should trap. But if the lower 32 bits are crafted to be just
above the boundary (e.g., `0x00000001_00002000`), the check passes with `aux_stack_top =
0x2000`, while the actual stack pointer should be `0x100002000`. The global is also
written as a truncated 32-bit value, corrupting the stack tracking. This can bypass
auxiliary stack overflow protections, enabling stack buffer overflows within linear memory.

### Evidence

Compare with the classic interpreter's correct handling:

```c
// wasm_interp_classic.c lines 4352-4379
#if WASM_ENABLE_MEMORY64 != 0
    if (is_memory64) {
        aux_stack_top = *(uint64 *)(frame_sp - 2);  // correctly reads 64 bits
    }
    else
#endif
    {
        aux_stack_top = (uint64)(*(uint32 *)(frame_sp - 1));
    }
    // ... boundary checks ...
#if WASM_ENABLE_MEMORY64 != 0
    if (is_memory64) {
        *(uint64 *)global_addr = aux_stack_top;  // correctly writes 64 bits
        frame_sp -= 2;
    }
    else
#endif
```

---

## Finding 2: `ARRAY_NEW_DATA` Missing `data_dropped` Check

**Severity:** MEDIUM  
**Files:** `core/iwasm/interpreter/wasm_interp_classic.c` (lines 2954-3031),
`core/iwasm/interpreter/wasm_interp_fast.c` (lines 2351-2427)

### Vulnerable Code (classic interpreter)

```c
case WASM_OP_ARRAY_NEW_DATA:
{
    // ...
    read_leb_uint32(frame_ip, frame_ip_end, data_seg_idx);
    data_seg = wasm_module->data_segments[data_seg_idx];  // no dropped check!

    // ...
    array_len = POP_I32();
    data_seg_offset = POP_I32();

    // Uses data_seg->data_length directly, even if segment was dropped
    total_size = (uint64)elem_size * array_len;
    if (data_seg_offset >= data_seg->data_length
        || total_size > data_seg->data_length - data_seg_offset) {
        // ...
    }

    // Reads from potentially dropped segment
    bh_memcpy_s(array_elem_base, (uint32)total_size,
                data_seg->data + data_seg_offset, (uint32)total_size);
```

### Description

The `WASM_OP_ARRAY_NEW_DATA` handler in both interpreters accesses data segments
without checking the `data_dropped` bitmap. Per the WebAssembly GC specification,
accessing a dropped data segment (via `data.drop`) should trap, treating the segment
as having zero length. Compare with `WASM_OP_MEMORY_INIT` (classic interp lines
5750-5760), which correctly checks:

```c
if (bh_bitmap_get_bit(module->e->common.data_dropped, segment)) {
    seg_len = 0;
    data = NULL;
} else {
    seg_len = (uint64)module->module->data_segments[segment]->data_length;
    data = module->module->data_segments[segment]->data;
}
```

### Attack Path

A WASM module can use `data.drop` on a data segment and then call `array.new_data`
referencing the same segment. Because the dropped check is missing, the operation
succeeds using the original (pre-drop) segment data and length. This violates the
spec requirement that dropped segments are treated as zero-length. The same issue
also exists in `wasm_runtime.c` `llvm_array_init_with_data` at line 4956-4976.

While the data itself is not freed (the `data.drop` only sets a bit), this allows
continued access to data that the module explicitly relinquished, which could violate
information-flow security assumptions in systems that rely on `data.drop` to prevent
further data reads.

### Evidence

The `MEMORY_INIT` handler has the proper check; `ARRAY_NEW_DATA` does not.

---

## Finding 3: `STRINGVIEW_WTF16_ENCODE` Integer Overflow in Bounds Check (32-bit platforms)

**Severity:** HIGH (on 32-bit platforms)  
**File:** `core/iwasm/interpreter/wasm_interp_classic.c`  
**Lines:** 3819

### Vulnerable Code

```c
case WASM_OP_STRINGVIEW_WTF16_ENCODE:
{
    uint32 mem_idx, addr, pos, len, offset = 0;
    int32 written_code_units = 0;

    read_leb_uint32(frame_ip, frame_ip_end, mem_idx);
    len = POP_I32();
    pos = POP_I32();
    addr = POP_I32();
    stringview_wtf16_obj = POP_REF();

    CHECK_MEMORY_OVERFLOW(len * sizeof(uint16));  // <-- integer overflow
```

### Description

`len` is a `uint32` from the WASM stack. `sizeof(uint16)` is `size_t`. On 32-bit
platforms where `size_t` is 32 bits, the expression `len * sizeof(uint16)` =
`len * 2` can overflow. For example, with `len = 0x80000001`, the product wraps
to `0x00000002`. The `CHECK_MEMORY_OVERFLOW(2)` macro then checks if
`addr + 2 <= linear_mem_size`, which easily passes.

Subsequently, `wasm_string_encode` is called with the original (non-overflowed) `len`,
which writes up to `len` WTF16 code units (each 2 bytes) into the memory buffer
starting at `maddr`. If the attacker has created a sufficiently long string (e.g.,
via repeated `string.concat`), this writes up to `0x80000001 * 2 = ~4GB` of data
past the checked boundary.

### Attack Path

1. Attacker creates a WASM module targeting a 32-bit WAMR runtime with stringref
   enabled.
2. Module creates a long string via repeated `string.concat` operations.
3. Module calls `stringview.wtf16.encode` with `len = 0x80000001` and a valid
   memory address.
4. Bounds check passes because `0x80000001 * 2 = 2` (overflow).
5. The encode writes up to the full string length past the bounds-checked region,
   causing a heap buffer overflow.

### Evidence

On 64-bit platforms, `sizeof(uint16)` returns `size_t` (64-bit), so `uint32 * uint64`
does not overflow. This issue is specific to 32-bit platforms, which are a primary
deployment target for WAMR (embedded systems). The fix should cast `len` to `uint64`
before the multiplication: `CHECK_MEMORY_OVERFLOW((uint64)len * sizeof(uint16))`.

---

## Finding 4: `wasm_func_call` NULL Dereference in Error Path

**Severity:** MEDIUM  
**File:** `core/iwasm/common/wasm_c_api.c`  
**Lines:** 3457-3464

### Vulnerable Code

```c
    if (!exec_env) {       // line 3429
        goto failed;       // exec_env is NULL here
    }
    // ...
failed:
    if (argv != argv_buf)
        wasm_runtime_free(argv);

#if WASM_ENABLE_DUMP_CALL_STACK != 0 && WASM_ENABLE_THREAD_MGR != 0
    WASMCluster *cluster = wasm_exec_env_get_cluster(exec_env);  // NULL deref!
    cluster_frames = &cluster->exception_frames;
    wasm_cluster_traverse_lock(exec_env);                        // NULL deref!
#endif
```

### Description

The `wasm_func_call` function can jump to the `failed` label from multiple locations
where `exec_env` has not yet been initialized (e.g., lines 3397-3399 when
`func_comm_rt` is NULL, or lines 3414-3416 when `params_to_argv` fails). At the
`failed` label, when both `WASM_ENABLE_DUMP_CALL_STACK` and `WASM_ENABLE_THREAD_MGR`
are enabled, the code unconditionally dereferences `exec_env` via
`wasm_exec_env_get_cluster(exec_env)` and `wasm_cluster_traverse_lock(exec_env)`.

### Attack Path

If a host application uses the C API to call a WASM function and the call fails before
`exec_env` is initialized (which can happen if internal function lookup fails or
parameter conversion fails), the NULL dereference crashes the host process. This is
a denial-of-service from the host perspective. While this requires the C API to be
used, a malicious module could be designed so that `func_comm_rt` resolution fails
in certain module configurations, or the function type causes `params_to_argv` to fail.

### Evidence

The `exec_env` variable is initialized to `NULL` at line 3358 and is only set after
the checks at lines 3418-3428. Any `goto failed` before line 3428 leaves `exec_env`
as NULL. The `failed` label code at line 3462 does not guard `exec_env` against NULL
before using it.

---

## Finding 5: `STRING_NEW_WTF16` Insufficient Memory Bounds Check

**Severity:** HIGH  
**Files:** `core/iwasm/interpreter/wasm_interp_classic.c` (line 3408),
`core/iwasm/interpreter/wasm_interp_fast.c` (line 2821)

### Vulnerable Code (classic interpreter)

```c
case WASM_OP_STRING_NEW_UTF8:
case WASM_OP_STRING_NEW_WTF16:
case WASM_OP_STRING_NEW_LOSSY_UTF8:
case WASM_OP_STRING_NEW_WTF8:
{
    uint32 mem_idx, addr, bytes_length, offset = 0;
    EncodingFlag flag = WTF8;

    read_leb_uint32(frame_ip, frame_ip_end, mem_idx);
    bytes_length = POP_I32();
    addr = POP_I32();

    CHECK_MEMORY_OVERFLOW(bytes_length);  // <-- insufficient for WTF16

    if (opcode == WASM_OP_STRING_NEW_WTF16) {
        flag = WTF16;
    }
    // ...
    str_obj = wasm_string_new_with_encoding(
        maddr, bytes_length, flag);  // reads bytes_length * 2 bytes for WTF16
```

### Description

The `string.new_wtf16` WebAssembly instruction takes two i32 parameters: a memory
offset and a code unit count. Each WTF-16 code unit is 2 bytes, so the actual memory
range accessed is `[offset, offset + codeunits * 2)`. However, the bounds check at
line 3408 (classic) and line 2821 (fast) uses `CHECK_MEMORY_OVERFLOW(bytes_length)`
where `bytes_length` is actually the code unit count. This validates only `codeunits`
bytes instead of the required `codeunits * 2` bytes.

The same bug exists in the AOT compiler's `aot_compile_op_string_new` function
(`core/iwasm/compilation/aot_emit_stringref.c` line 390-391), where
`check_bulk_memory_overflow(comp_ctx, func_ctx, offset, byte_length)` is called
with the raw code unit count.

### Attack Path

1. Attacker creates a WASM module with stringref enabled.
2. Module allocates linear memory of, say, 65536 bytes (1 page).
3. Module calls `string.new_wtf16` with `addr = 60000` and `codeunits = 4000`.
4. Bounds check validates: `60000 + 4000 = 64000 <= 65536` → passes.
5. `wasm_string_new_with_encoding(maddr, 4000, WTF16)` reads `4000 * 2 = 8000`
   bytes from `maddr`, accessing memory from offset 60000 to 68000.
6. This reads 2464 bytes past the end of linear memory, potentially leaking
   host process memory contents via the created string object.

### Evidence

For UTF-8/WTF-8 variants, `bytes_length` correctly represents byte count, so
`CHECK_MEMORY_OVERFLOW(bytes_length)` is sufficient. But for WTF-16, the parameter
is a code unit count (per the WebAssembly stringref specification), and each code
unit occupies 2 bytes. Compare with the `STRINGVIEW_WTF16_ENCODE` handler (line
3819 in classic interpreter), which correctly uses `CHECK_MEMORY_OVERFLOW(len *
sizeof(uint16))` for the WTF-16 case (though that calculation has its own integer
overflow issue documented in Finding 3). The fix should differentiate the WTF16
case: `CHECK_MEMORY_OVERFLOW(opcode == WASM_OP_STRING_NEW_WTF16 ? (uint64)bytes_length * 2 : bytes_length)`.

---

## Finding 5: `call_ref` Missing `frame_per_function` Stack Frame Allocation for Import Calls

**Severity:** HIGH  
**File:** `core/iwasm/compilation/aot_emit_function.c`  
**Lines:** 3030-3038, 3097-3145, 3230-3236

### Vulnerable Code

In `aot_compile_op_call_ref`, the frame allocation around the import call block:

```c
// Lines 3030-3038: Only handles !frame_per_function
if (comp_ctx->aux_stack_frame_type
    && !comp_ctx->call_stack_features.frame_per_function) {
#if WASM_ENABLE_AOT_STACK_FRAME != 0
    if (!call_aot_alloc_frame_func(comp_ctx, func_ctx, func_idx))
        goto fail;
#endif
}

// Lines 3097-3118: Import call block - NO frame_per_function handling
/* Translate call import block */
LLVMPositionBuilderAtEnd(comp_ctx->builder, block_call_import);
// ... calls aot_invoke_native_func directly without frame alloc ...

// Lines 3230-3236: Only handles !frame_per_function
if (comp_ctx->aux_stack_frame_type
    && !comp_ctx->call_stack_features.frame_per_function) {
#if WASM_ENABLE_AOT_STACK_FRAME != 0
    if (!free_frame_for_aot_func(comp_ctx, func_ctx))
        goto fail;
#endif
}
```

### Description

When `frame_per_function` is enabled, `aot_compile_op_call_ref` does not allocate
or free per-function stack frames for import function calls. This is inconsistent
with `aot_compile_op_call_indirect` (lines 2588-2592 and 2631-2635) and
`aot_compile_op_call` (lines 1460-1467 and 1843-1848), which both correctly handle
the `frame_per_function` case for imports.

In `aot_compile_op_call_indirect`, the import call block correctly includes:

```c
// Lines 2588-2592: Allocates frame for imports when frame_per_function is true
if (comp_ctx->aot_frame && comp_ctx->call_stack_features.frame_per_function
    && !aot_alloc_frame_per_function_frame_for_aot_func(comp_ctx, func_ctx,
                                                        func_idx)) {
    goto fail;
}

// Lines 2631-2635: Frees frame for imports when frame_per_function is true
if (comp_ctx->aot_frame && comp_ctx->call_stack_features.frame_per_function
    && !aot_free_frame_per_function_frame_for_aot_func(comp_ctx,
                                                       func_ctx)) {
    goto fail;
}
```

Neither of these blocks exists in `aot_compile_op_call_ref`.

### Attack Path

1. Attacker creates a WASM module with GC enabled (required for `call_ref`).
2. The module uses `ref.func` to obtain a reference to an imported function,
   then calls it via `call_ref`.
3. The runtime is configured with `frame_per_function = true` and
   `WASM_ENABLE_AOT_STACK_FRAME != 0`.
4. The import function call executes without a dedicated stack frame being
   allocated on the WASM operand stack.
5. If the import function triggers a callback into WASM or causes GC, the
   missing frame means:
   - Stack overflow checks may not account for the import's stack usage,
     allowing the operand stack to overflow.
   - GC root scanning may miss references held in the calling function's
     frame, leading to premature collection of live GC objects
     (use-after-free in the GC heap).
   - Stack unwinding produces incorrect call stacks, potentially confusing
     exception handling logic.

### Evidence

Direct comparison of the three call-emission functions shows the omission:

| Feature                        | `call` (direct) | `call_indirect` | `call_ref` |
|-------------------------------|:---:|:---:|:---:|
| `!frame_per_function` alloc   | ✓ (L1468-1476)  | ✓ (L2518-2526) | ✓ (L3030-3038) |
| `!frame_per_function` free    | ✓ (L1850-1858)  | ✓ (L2722-2728) | ✓ (L3230-3236) |
| `frame_per_function` alloc    | ✓ (L1460-1466)  | ✓ (L2588-2592) | **MISSING**     |
| `frame_per_function` free     | ✓ (L1843-1848)  | ✓ (L2631-2635) | **MISSING**     |

---

## Finding 6: `get_init_expr_size` Integer Overflow in AOT File Size Calculation

**Severity:** HIGH (mitigated to MEDIUM by `CHECK_BUF`)  
**File:** `core/iwasm/compilation/aot_emit_aot_file.c`  
**Lines:** 284-308

### Vulnerable Code

```c
case INIT_EXPR_TYPE_ARRAY_NEW:
case INIT_EXPR_TYPE_ARRAY_NEW_FIXED:
{
    WASMArrayNewInitValues *array_new_init_values =
        (WASMArrayNewInitValues *)expr->u.unary.v.data;
    WASMArrayType *array_type = NULL;
    uint32 value_count;

    // ...
    value_count =
        (expr->init_expr_type == INIT_EXPR_TYPE_ARRAY_NEW_FIXED)
            ? array_new_init_values->length    // attacker-controlled
            : 1;

    /* array_elem_type + type_index + len + elems */
    size += sizeof(uint32) * 3
            + (uint64)wasm_value_type_size_internal(
                  array_type->elem_type, comp_ctx->pointer_size)
                  * value_count;              // uint64 result truncated to uint32
    break;
}
```

### Description

The variable `size` is `uint32`. The multiplication is performed in 64-bit
(`(uint64) * uint32`), but the result of the entire addition is implicitly
truncated back to `uint32` when assigned via `size +=`. For
`INIT_EXPR_TYPE_ARRAY_NEW_FIXED`, `value_count` comes from
`array_new_init_values->length`, which is attacker-controlled via the WASM module.

For example, if `elem_type` is `i64` (size 8) and `length = 0x20000001`:
`8 * 0x20000001 = 0x100000008`, which truncates to `0x8` when stored in `uint32`.
This causes `aot_get_aot_file_size` to return an undersized total, leading to an
undersized heap allocation for the AOT file buffer.

### Mitigation

The `EMIT_*` macros used during actual buffer writing include `CHECK_BUF(length)`,
which validates `buf + offset + length <= buf_end`. When the buffer is too small,
the write fails gracefully before any out-of-bounds write occurs. This prevents
memory corruption but causes a confusing compilation failure rather than a clear
error message about the size overflow.

### Attack Path

1. Attacker crafts a WASM module with a global initializer using
   `array.new_fixed` with a very large `length` value.
2. During AOT compilation, `get_init_expr_size` computes a truncated (too-small)
   size for the initializer expression.
3. `aot_get_aot_file_size` returns an undersized total file size.
4. `wasm_runtime_malloc` allocates an undersized buffer.
5. During `aot_emit_aot_file_buf_ex`, `CHECK_BUF` detects the overflow and
   returns `false`, preventing memory corruption.
6. **Without `CHECK_BUF`**, this would be a heap buffer overflow. The defense
   is fragile—any new emission code that omits `CHECK_BUF` would be exploitable.

### Evidence

The `size` variable is declared as `uint32` at line 215. The cast to `(uint64)`
on line 305 only widens the multiplication operand, not the assignment target.
C's implicit truncation on `uint32 += uint64` silently loses the high bits.

---

## Finding 7: `struct_get`/`struct_set` Field Access Before Bounds Check

**Severity:** MEDIUM (mitigated by WASM loader validation)  
**File:** `core/iwasm/compilation/aot_emit_gc.c`  
**Lines:** 601-610 (`struct_get`) and 665-674 (`struct_set`)

### Vulnerable Code

```c
// aot_compile_op_struct_get, lines 601-610:
field = compile_time_struct_type->fields + field_idx;   // Use BEFORE check
field_type = field->field_type;                          // Dereference
field_offset = comp_ctx->pointer_size == sizeof(uint64)
                   ? field->field_offset_64bit           // Dereference
                   : field->field_offset_32bit;          // Dereference

if (field_idx >= compile_time_struct_type->field_count) { // Check AFTER use
    aot_set_last_error("struct field index out of bounds");
    goto fail;
}
```

### Description

Both `aot_compile_op_struct_get` and `aot_compile_op_struct_set` compute a
pointer into the `fields` array and dereference it (reading `field_type` and
`field_offset`) before checking whether `field_idx` is within bounds. If
`field_idx >= field_count`, this constitutes an out-of-bounds read from the
`fields` array.

### Mitigation

The WASM loader (`wasm_loader.c`, line 14834) validates `field_idx < field_count`
during module loading, rejecting invalid modules before AOT compilation begins.
Therefore, the out-of-bounds access is not reachable from well-validated WASM input.

### Attack Path

If the AOT compiler is ever invoked on a module that bypasses the standard WASM
loader validation (e.g., a JIT path, a fuzzing harness, or a future code path that
skips validation), the out-of-bounds read could leak sensitive data from adjacent
heap memory or cause a crash.

### Evidence

The bounds check at line 607/671 should be moved before the field access at
line 601/665 as a defense-in-depth measure. Compare with `aot_compile_op_array_get`
(line 1350) which correctly checks bounds before element access.
