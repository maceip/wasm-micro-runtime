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
