# Security Audit Report — Memory Management & Networking

**Date:** 2026-07-02
**Scope:** `wasm_memory.c`, `wasi_socket_ext.c`, `posix_socket.c`, `libc_uvwasi_wrapper.c`
**Focus:** Integer overflow, bounds check bypass, buffer overflow, sandbox escape

---

## Finding 1 — `poll_oneoff` validates only one array element (both WASI backends)

| Field | Value |
|---|---|
| **Severity** | HIGH |
| **Files** | `core/iwasm/libraries/libc-uvwasi/libc_uvwasi_wrapper.c:935-938` |
| | `core/iwasm/libraries/libc-wasi/libc_wasi_wrapper.c:1101-1104` |
| **Type** | Insufficient bounds validation → Out-of-bounds read/write |

### Description

Both WASI backend wrappers for `poll_oneoff` validate the `in` and `out` buffer
pointers for only a *single element* (`sizeof(wasi_subscription_t)` and
`sizeof(wasi_event_t)`) instead of the full array size
(`nsubscriptions * sizeof(...)`). The `nsubscriptions` count is
attacker-controlled (wasm guest argument).

**libc_uvwasi_wrapper.c:935-938:**
```c
if (!validate_native_addr((void *)in, (uint64)sizeof(wasi_subscription_t))
    || !validate_native_addr(out, (uint64)sizeof(wasi_event_t))
    || !validate_native_addr(nevents_app, (uint64)sizeof(uint32)))
    return (wasi_errno_t)-1;
```

**libc_wasi_wrapper.c:1101-1104:**
```c
if (!validate_native_addr((void *)in, (uint64)sizeof(wasi_subscription_t))
    || !validate_native_addr(out, (uint64)sizeof(wasi_event_t))
    || !validate_native_addr(nevents_app, (uint64)sizeof(uint32)))
    return (wasi_errno_t)-1;
```

The correct validation should multiply by `nsubscriptions`:
```c
(uint64)sizeof(wasi_subscription_t) * nsubscriptions
(uint64)sizeof(wasi_event_t) * nsubscriptions
```

### Attack chain

1. Wasm guest allocates a 1-element subscription buffer at the end of linear
   memory.
2. Guest calls `poll_oneoff` with `nsubscriptions = N` (a large value).
3. Validation passes because only one element is checked.
4. The host-side `uvwasi_poll_oneoff` / `wasmtime_ssp_poll_oneoff`
   reads `N` subscription entries from memory beyond the linear memory boundary
   (OOB read of host memory) and writes `N` event entries beyond it (OOB write
   of host memory).

### Existing mitigations

- When `OS_ENABLE_HW_BOUND_CHECK` is enabled, access beyond the 8 GB mmap
  reservation will trigger a signal, limiting the useful OOB window to
  unmapped guard pages. Without HW bound checks, no mitigation exists.
- The `*` signature type in WAMR's native call framework does **not**
  perform automatic size-aware validation; it is the wrapper's responsibility.

### Recommended fix

Replace the single-element validation with:
```c
uint64 in_size = (uint64)sizeof(wasi_subscription_t) * nsubscriptions;
uint64 out_size = (uint64)sizeof(wasi_event_t) * nsubscriptions;
if (in_size >= UINT32_MAX || out_size >= UINT32_MAX
    || !validate_native_addr((void *)in, in_size)
    || !validate_native_addr(out, out_size)
    || !validate_native_addr(nevents_app, (uint64)sizeof(uint32)))
    return (wasi_errno_t)-1;
```

---

## Finding 2 — `wasm_runtime_shared_heap_malloc` returns wrong offset in memory64 mode

| Field | Value |
|---|---|
| **Severity** | HIGH |
| **File** | `core/iwasm/common/wasm_memory.c:799-802` |
| **Type** | Incorrect address translation → Heap corruption |

### Description

In `wasm_runtime_shared_heap_malloc`, the return value for memory64 mode is
missing the within-heap offset calculation due to C ternary operator precedence:

```c
return memory->is_memory64
           ? shared_heap->start_off_mem64
           : shared_heap->start_off_mem32
                 + ((uint8 *)native_addr - shared_heap->base_addr);
```

The `+ ((uint8 *)native_addr - shared_heap->base_addr)` addition only applies
to the mem32 (false) branch. The mem64 (true) branch returns only
`shared_heap->start_off_mem64`, discarding the allocation offset.

Meanwhile, `wasm_runtime_shared_heap_free` correctly computes the native
address for mem64:
```c
addr = shared_heap->base_addr + (ptr - shared_heap->start_off_mem64);
```

### Attack chain

1. Wasm module (memory64) attaches a shared heap and calls
   `shared_heap_malloc` multiple times.
2. Every call returns the same wasm offset (`start_off_mem64`), regardless of
   where the allocator placed the block.
3. The wasm module writes to what it believes are independent buffers, but all
   writes alias to the start of the shared heap.
4. When the wasm module frees using the returned offset, `shared_heap_free`
   computes `base_addr + (start_off_mem64 - start_off_mem64) = base_addr`,
   which frees the first allocation block regardless of which block was
   intended — corrupting the heap allocator metadata.
5. Subsequent allocations can return overlapping regions, enabling
   type-confusion and data corruption within the shared heap.

### Existing mitigations

- Only affects memory64 mode (`WASM_ENABLE_MEMORY64`), which is not the
  default configuration.
- Shared heap is an opt-in feature (`WASM_ENABLE_SHARED_HEAP`).
- Exploitation is limited to corruption within the shared heap region.

### Recommended fix

```c
return (memory->is_memory64
            ? shared_heap->start_off_mem64
            : shared_heap->start_off_mem32)
       + ((uint8 *)native_addr - shared_heap->base_addr);
```

---

## Finding 3 — `wasm_check_app_addr_and_convert` unconditional string scan in shared heap path

| Field | Value |
|---|---|
| **Severity** | MEDIUM |
| **File** | `core/iwasm/common/wasm_memory.c:1426-1449` |
| **Type** | Missing conditional → Denial of service (false OOB rejection) |

### Description

When a buffer address falls in the shared heap, `wasm_check_app_addr_and_convert`
always performs a NUL-terminator scan regardless of the `is_str` parameter.
For non-string buffers, this scan is incorrect: it rejects valid accesses when
the buffer data does not happen to contain a `\0` byte within the shared heap
boundary.

```c
if (is_app_addr_in_shared_heap(..., app_buf_addr, app_buf_size)) {
    // ... always does string scan even when is_str == false ...
    str = (const char *)native_addr;
    str_end = (const char *)shared_heap_base_addr_adj + shared_heap_end_off + 1;
    while (str < str_end && *str != '\0')
        str++;
    if (str == str_end) {
        wasm_set_exception(module_inst, "out of bounds memory access");
        return false;   // false rejection for non-string data
    }
}
```

### Attack chain

1. A wasm module stores non-string binary data (no NUL bytes) in the shared
   heap.
2. Any native function that calls `wasm_check_app_addr_and_convert` with
   `is_str = false` for that buffer will have the call rejected even though
   `is_app_addr_in_shared_heap` already validated the range.
3. This causes unexpected exceptions and crashes the wasm instance.

This is not a sandbox escape but can be used for targeted denial of service
against shared heap users.

### Existing mitigations

- Only affects code paths where shared heap is enabled and non-string buffers
  are validated through `wasm_check_app_addr_and_convert`.
- The range validation by `is_app_addr_in_shared_heap` is correct, so no
  out-of-bounds access occurs — only false rejections.

### Recommended fix

```c
if (is_app_addr_in_shared_heap(..., app_buf_addr, app_buf_size)) {
    shared_heap_base_addr_adj = get_last_used_shared_heap_base_addr_adj(...);
    shared_heap_end_off = get_last_used_shared_heap_end_offset(...);
    native_addr = shared_heap_base_addr_adj + (uintptr_t)app_buf_addr;

    if (is_str) {
        const char *str = (const char *)native_addr;
        const char *str_end =
            (const char *)shared_heap_base_addr_adj + shared_heap_end_off + 1;
        while (str < str_end && *str != '\0')
            str++;
        if (str == str_end) {
            wasm_set_exception(module_inst, "out of bounds memory access");
            return false;
        }
    }
    goto success;
}
```

---

## Areas reviewed with no confirmed exploitable findings

### `wasm_memory.c` — Memory grow / enlarge

- **Integer overflow in `total_page_count`:** Checked at line 1648
  (`total_page_count < cur_page_count` detects wraparound).
- **`total_size_new` overflow:** `num_bytes_per_page * (uint64)total_page_count`
  is computed in uint64; bounded by `max_page_count` and
  `GET_MAX_LINEAR_MEMORY_SIZE`.
- **`wasm_runtime_set_mem_bound_check_bytes` underflow:** Callers guard with
  `if (memory_data_size > 0)` before invoking (lines 474, 1164).
- **Shared heap chain `start_off_mem32` underflow:** Arithmetic can wrap for
  large chains, but the resulting offset is unreachable from valid wasm
  addresses (exceeds `UINT32_MAX` range), so it does not create a bypass.
- **`wasm_runtime_validate_app_addr`:** Correctly validates linear memory
  bounds with overflow-safe arithmetic (`size > max - app_offset`).
- **`addr_app_to_native` / `addr_native_to_app`:** Bounds checks are present
  when enabled; shared memory locking is used appropriately.

### `wasi_socket_ext.c` — WASI socket extensions (guest-side)

- This code runs *inside* the wasm sandbox (compiled to wasm), so bugs here
  cannot escape the sandbox. It is guest-side library code.
- `sockaddr_to_wasi_addr` and `wasi_addr_to_sockaddr` use `assert` for size
  checking (non-exploitable, nonfunctional in release builds, but runs
  in-sandbox).

### `posix_socket.c` — Host socket implementation

- All `sockaddr` handling uses `struct sockaddr_storage` (sufficient for both
  IPv4/IPv6), preventing buffer overflow.
- `sockaddr_to_bh_sockaddr` validates `sa_family` before casting.
- `textual_addr_to_sockaddr` uses `inet_pton` which is length-safe.
- `os_socket_addr_resolve` correctly bounds the result count with `pos` vs
  `addr_info_size`.

### `libc_uvwasi_wrapper.c` — iovec handling

- `fd_read`, `fd_write`, `fd_pread`, `fd_pwrite`, `sock_recv`, `sock_send`
  all correctly validate iovec arrays using `sizeof(iovec_app_t) * (uint64)iovs_len`
  and individually validate each `buf_offset` + `buf_len` pair via
  `validate_app_addr`. These are implemented correctly.
