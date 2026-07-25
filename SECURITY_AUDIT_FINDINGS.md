# WAMR Security Audit: Memory Management & GC Findings

## Finding 1: `gc_migrate` Fails to Adjust `kfc_normal_list` Head Pointers — Use-After-Free

**Severity:** HIGH  
**File:** `core/shared/mem-alloc/ems/ems_kfc.c`, lines 236–329  

### Vulnerable Code

```c
int
gc_migrate(gc_handle_t handle, char *pool_buf_new, gc_size_t pool_buf_size)
{
    ...
    intptr_t offset = (uint8 *)base_addr_new - (uint8 *)heap->base_addr;
    ...

    heap->base_addr = (uint8 *)base_addr_new;

    // Adjusts kfc_tree_root pointers (lines 273-281)
    adjust_ptr(p_left, offset);
    adjust_ptr(p_right, offset);
    adjust_ptr(p_parent, offset);

    // Walks heap and adjusts large (tree) FC node pointers (lines 283-316)
    while (cur < end) {
        ...
        if (hmu_get_ut(cur) == HMU_FC && !HMU_IS_FC_NORMAL(size)) {
            // Only adjusts tree nodes (large free chunks)
            // MISSING: kfc_normal_list[i].next pointers are NOT adjusted
        }
        ...
    }
    return 0;
}
```

The `gc_heap_t` structure contains `kfc_normal_list[HMU_NORMAL_NODE_CNT]`, an array of linked-list heads for small free chunks (size < 248 bytes). Each `kfc_normal_list[i].next` is an **absolute pointer** into the pool buffer. When `gc_migrate` is called with a nonzero offset (pool buffer relocated), these head pointers still reference the **old** pool address and become dangling.

### Attack Path

1. A WASM module allocates and frees small objects from its app heap, populating `kfc_normal_list` entries.
2. The WASM module calls `memory.grow`.
3. If `os_mremap` cannot extend in-place, it returns a new address, triggering `gc_migrate` via `wasm_enlarge_memory_internal` → `mem_allocator_migrate` → `gc_migrate`.
4. After migration, `kfc_normal_list[i].next` pointers are stale (pointing to now-unmapped or reused memory).
5. Next app heap allocation of a normal-sized block dereferences the stale pointer in `alloc_hmu` (line 383 of `ems_alloc.c`): `p = normal_head->next;`
6. This is a use-after-free: the runtime reads from (and potentially writes to) freed/unmapped memory, causing a crash or potential code execution.

### Evidence

- `kfc_normal_list` head pointers are absolute (`hmu_normal_node_t *next`), defined at `ems_gc_internal.h:261`.
- `gc_migrate` iterates the pool and only adjusts `HMU_FC` tree nodes (`!HMU_IS_FC_NORMAL(size)`), explicitly skipping normal-sized free chunks (line 297).
- `alloc_hmu` (ems_alloc.c:373–383) and `unlink_hmu` (ems_alloc.c:201) dereference `kfc_normal_list[].next` directly.
- `gci_add_fc` (ems_alloc.c:290) uses `kfc_normal_list[node_idx].next` as the next pointer for new nodes, propagating the corruption.

---

## Finding 2: `wasm_obj_is_instance_of_type_idx` Passes `type_count = 0` — Assertion Bypass / Type Confusion

**Severity:** MEDIUM  
**File:** `core/iwasm/common/gc/gc_common.c`, lines 826–851  

### Vulnerable Code

```c
bool
wasm_obj_is_instance_of_type_idx(WASMObjectRef obj, uint32 type_idx,
                                 WASMModuleCommon *const module)
{
    WASMType **types = NULL;
    uint32 type_count = 0;  // <-- initialized to 0, never updated

#if WASM_ENABLE_INTERP != 0
    if (module->module_type == Wasm_Module_Bytecode) {
        WASMModule *wasm_module = (WASMModule *)module;
        types = wasm_module->types;
        // BUG: type_count = wasm_module->type_count; is MISSING
    }
#endif
#if WASM_ENABLE_AOT != 0
    if (module->module_type == Wasm_Module_AoT) {
        AOTModule *aot_module = (AOTModule *)module;
        types = (WASMType **)aot_module->types;
        // BUG: type_count = aot_module->type_count; is MISSING
    }
#endif

    bh_assert(types);
    return wasm_obj_is_instance_of(obj, type_idx, types, type_count);
    //                                                    ^^^^^^^^^^^ always 0
}
```

Compare with the correct implementation in `wasm_obj_is_instance_of_defined_type` (lines 792–824), which properly assigns `type_count = wasm_module->type_count`.

### Attack Path

1. An embedder calls the public API `wasm_obj_is_instance_of_type_idx()` (exported via `gc_export.h` line 864) to validate a GC object's type.
2. `type_count` is always 0, so `wasm_obj_is_instance_of` receives `type_count = 0`.
3. In `wasm_obj_is_instance_of` (gc_object.c:660), `bh_assert(type_idx < type_count)` fails in debug builds but is compiled out in release builds.
4. In release builds, `types[type_idx]` accesses the `types` array at an arbitrary index without bounds checking, enabling out-of-bounds read and type confusion.
5. This can cause the runtime to treat an object of one type as another, bypassing type safety in the GC type system.

### Evidence

- `type_count` is declared and initialized to 0 at line 831 and never modified.
- The function is a public API (`WASM_RUNTIME_API_EXTERN` in `gc_export.h:864`).
- The correct pattern is visible in the sibling function `wasm_obj_is_instance_of_defined_type` at lines 800–804 where `type_count = wasm_module->type_count` is properly assigned.

---

## Finding 3: `gc_set_finalizer` / `gc_unset_finalizer` Set Object Flag Outside Heap Lock — Race Condition

**Severity:** MEDIUM  
**File:** `core/shared/mem-alloc/ems/ems_alloc.c`, lines 1112–1141 (set) and 1143–1168 (unset)  

### Vulnerable Code

**gc_set_finalizer (lines 1131–1140):**
```c
    LOCK_HEAP(vheap);
    if (!insert_extra_info_node(vheap, node)) {
        BH_FREE(node);
        UNLOCK_HEAP(vheap);
        return GC_FALSE;
    }
    UNLOCK_HEAP(vheap);
    // RACE WINDOW: node is in the list, but the object header flag is not set
    gct_vm_set_extra_info_flag(obj, true);  // modifies obj->header OUTSIDE lock
```

**gc_unset_finalizer (lines 1150–1167):**
```c
    LOCK_HEAP(vheap);
    node = gc_search_extra_info_node(vheap, obj, &index);
    ...
    BH_FREE(node);
    bh_memmove_s(...);
    vheap->extra_info_node_cnt -= 1;
    UNLOCK_HEAP(vheap);
    // RACE WINDOW: node is removed, but the object header flag is still set
    gct_vm_set_extra_info_flag(obj, false);  // modifies obj->header OUTSIDE lock
```

### Attack Path

**gc_set_finalizer race:**
1. Thread A calls `gc_set_finalizer`: inserts the finalizer node into `extra_info_nodes`, then unlocks.
2. Before Thread A sets the flag, Thread B triggers GC.
3. GC sweep (`sweep_instance_heap`, ems_gc.c:107) checks `gct_vm_get_extra_info_flag(cur_obj)` — returns false (flag not yet set).
4. GC frees the object without calling the finalizer.
5. The `extra_info_nodes` array now contains a stale `obj` pointer to freed memory.
6. Thread A then sets the extra-info flag on the freed object's header, corrupting whatever now occupies that memory.
7. On heap destruction (`gc_destroy_with_pool`, ems_kfc.c:148), the stale finalizer is called with a dangling `node->obj` pointer → use-after-free.

**gc_unset_finalizer race:**
1. Thread A calls `gc_unset_finalizer`: removes the node, unlocks.
2. Before Thread A clears the flag, GC runs.
3. GC sweep sees the flag is still set, calls `gc_search_extra_info_node` → the node has been removed → `bh_assert(node)` triggers in debug, undefined behavior in release.

### Evidence

- `gct_vm_set_extra_info_flag` modifies `obj->header` (gc_common.c:988–993), which is a non-atomic read-modify-write.
- The GC sweep reads this flag at ems_gc.c:107 while holding the heap lock.
- The flag modification occurs after `UNLOCK_HEAP`, creating a TOCTOU window.

---

## Finding 4: `insert_extra_info_node` Integer Overflow in Capacity Growth

**Severity:** MEDIUM  
**File:** `core/shared/mem-alloc/ems/ems_alloc.c`, lines 1071–1090  

### Vulnerable Code

```c
static bool
insert_extra_info_node(gc_heap_t *vheap, extra_info_node_t *node)
{
    ...
    /* extend array */
    if (vheap->extra_info_node_cnt == vheap->extra_info_node_capacity) {
        gc_size_t new_capacity = vheap->extra_info_node_capacity * 3 / 2;
        gc_size_t total_size = sizeof(extra_info_node_t *) * new_capacity;
        //                     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
        // Both multiplications can overflow gc_size_t (uint32)

        new_nodes = (extra_info_node_t **)BH_MALLOC(total_size);
        ...
        bh_memcpy_s(new_nodes, total_size, vheap->extra_info_nodes,
                    sizeof(extra_info_node_t *) * vheap->extra_info_node_cnt);
        //          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
        // Source size may exceed dest capacity if total_size overflowed
    }
    ...
}
```

### Attack Path

1. `gc_size_t` is `uint32`. `extra_info_node_capacity` starts at 32 and grows by factor 3/2 each expansion.
2. When `capacity` reaches ~2^30.4 (~1.43 billion), `capacity * 3` overflows `uint32`, making `new_capacity` much smaller than expected.
3. `sizeof(extra_info_node_t *) * new_capacity` then allocates a tiny buffer.
4. `bh_memcpy_s` copies `sizeof(extra_info_node_t *) * extra_info_node_cnt` bytes (the full old array) into the undersized new buffer → heap buffer overflow.
5. Alternatively, at lower capacities, `sizeof(extra_info_node_t *) * new_capacity` overflows on 64-bit (pointer size 8) when `new_capacity > UINT32_MAX / 8 ≈ 536M`, producing a truncated allocation size.

### Evidence

- `gc_size_t` is `uint32` (ems_gc.h:62).
- No overflow check exists on either the `capacity * 3` computation or the `sizeof(extra_info_node_t *) * new_capacity` computation.
- While reaching the overflow threshold requires a very large number of finalizer registrations, the GC heap can theoretically be configured with `GC_MAX_HEAP_SIZE` up to 256KB, and each finalizer requires only a small GC object (~16 bytes), allowing up to ~16K objects per heap. With repeated alloc/set-finalizer/free/alloc cycles (where freed objects' finalizer nodes may not be cleaned up due to Finding 3's race), the node count can grow unboundedly via the `BH_MALLOC`-allocated external array.
