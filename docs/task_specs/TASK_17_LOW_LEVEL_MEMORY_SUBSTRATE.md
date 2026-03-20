# Task 17 — Low-Level Memory Substrate

## Objective

Introduce the minimum language and runtime primitives needed so that
generic heap-backed containers (e.g. `Vector<T>`) can be implemented
as ordinary Dao library code, without compiler-privileged container
semantics.

## Governing doctrine

No compiler privilege for stdlib containers. Every container must be
expressible using the same language surface available to user code.
This task builds the substrate; it does not build containers.

## Governing contracts

- `CONTRACT_TYPE_SYSTEM_FOUNDATIONS.md` — pointer types are a
  foundational semantic type category
- `CONTRACT_C_ABI_INTEROP.md` — pointer semantics at the ABI boundary
- `CONTRACT_RUNTIME_ABI.md` — runtime hook naming and ownership rules
- `CONTRACT_EXECUTION_CONTEXTS.md` — `mode unsafe =>` semantics

## What already exists

| Primitive | Status |
|-----------|--------|
| Pointer types (`*T`) | ✅ Full — AST, types, HIR, MIR, LLVM |
| Dereference for read (`*ptr`) | ✅ Full — gated by `mode unsafe =>` |
| Address-of (`&x`) | ✅ Full — unrestricted |
| `mode unsafe =>` tracking | ✅ Full — type checker enforces |
| MirStore / MirLoad / MirAddrOf | ✅ Full — LLVM lowering |
| GEP for struct field through pointer | ✅ Full |
| C ABI pointer interop | ✅ Full |

## Deliverables

### 1. Store through pointer

**Verify and complete** assignment through dereference on the
left-hand side of `=`:

```dao
mode unsafe =>
  *ptr = value
```

The MIR already has `MirStore` with `Deref` projection. The type
checker and HIR builder must accept dereference as an assignment
target. If this already works end-to-end, document it with a test.
If it doesn't, fix it.

**Safety:** requires `mode unsafe =>`.

### 2. Pointer offset

Add a builtin generic function for computing a typed pointer offset:

```dao
fn ptr_offset<T>(ptr: *T, index: i64): *T
```

Semantics: returns a pointer to `*(ptr + index)` — equivalent to
C `ptr + index` on a `T*`. The offset is in units of `T`, not bytes.

**Implementation:** the compiler recognizes `ptr_offset` as a builtin
and lowers it to an LLVM `getelementptr` instruction after
monomorphization, when `T` is concrete.

**Safety:** callable anywhere (computing an offset is not unsafe).
**Dereferencing** the result still requires `mode unsafe =>`.

**Rationale:** this is the irreducible typed pointer arithmetic
primitive. It is not container-specific — it is how you express
indexed access to any contiguous allocation.

### 3. `size_of<T>()` and `align_of<T>()`

Add builtin generic functions that return the size and alignment of
a type in bytes:

```dao
fn size_of<T>(): i64
fn align_of<T>(): i64
```

**Implementation:** the compiler recognizes these as builtins. After
monomorphization, `T` is concrete. The backend emits the result of
LLVM's `DataLayout::getTypeAllocSize()` and
`DataLayout::getABITypeAlign()` as an integer constant.

**Safety:** callable anywhere (querying type layout is not unsafe).

**Rationale:** these are required to call allocation hooks with
correct size and alignment. They are general-purpose language
intrinsics, not container-specific.

### 4. Null pointer

Add a builtin for producing a typed null pointer:

```dao
fn null_ptr<T>(): *T
```

**Implementation:** lowers to LLVM `null` of the appropriate pointer
type.

**Safety:** callable anywhere (constructing a null pointer is not
unsafe; dereferencing it is).

Also required: **pointer equality** (`==` and `!=`) for `*T` operands,
so that null checks are expressible:

```dao
if ptr != null_ptr<i32>():
  // ...
```

**Implementation:** pointer equality lowers to LLVM `icmp eq` /
`icmp ne` on pointer values. Both operands must be the same pointer
type.

### 5. Runtime allocation hooks

Add three runtime hooks under the `mem` domain:

| Hook | Signature | Semantics |
|------|-----------|-----------|
| `__dao_mem_alloc` | `(size: i64, align: i64): *void` | Allocate `size` bytes with `align` alignment. Trap on failure. |
| `__dao_mem_realloc` | `(ptr: *void, old_size: i64, new_size: i64, align: i64): *void` | Resize allocation. Copies `min(old_size, new_size)` bytes. Trap on failure. `ptr` may be null (acts as alloc). |
| `__dao_mem_free` | `(ptr: *void): void` | Free allocation. Null is a no-op. |

**C implementation:** in `runtime/memory/alloc.c`. The allocation
hooks live under `runtime/memory/`, not `runtime/core/`, because
`runtime/core/` is reserved for the minimal always-linked runtime
slice and `runtime/memory/` is the designated home for allocation-
domain support per `ARCH_INDEX.md`.

Implementation constraints:

- `__dao_mem_alloc` must return memory aligned to at least `align`.
  Note that C11 `aligned_alloc` requires `size` to be a multiple of
  `align` — the implementation must round `size` up to satisfy this
  or use a platform-appropriate alternative (e.g. `posix_memalign`
  on POSIX, `_aligned_malloc` on Windows).
- `__dao_mem_realloc` must preserve the alignment guarantee of the
  original allocation. Standard C `realloc` does not accept an
  alignment parameter and only guarantees `max_align_t` alignment.
  The implementation must handle stronger alignments explicitly
  (e.g. allocate new aligned block, `memcpy`, free old block).
- `__dao_mem_free` must correctly free allocations made by the
  corresponding alloc/realloc. If the implementation uses
  platform-specific aligned allocation, the free path must match
  (e.g. `_aligned_free` on Windows).
- All three hooks trap (abort) on allocation failure rather than
  returning null.

**Dao declarations:** in `stdlib/core/memory.dao`:

```dao
extern fn __dao_mem_alloc(size: i64, align: i64): *void
extern fn __dao_mem_realloc(ptr: *void, old_size: i64, new_size: i64, align: i64): *void
extern fn __dao_mem_free(ptr: *void): void
```

**Note on `*void`:** this requires the type checker to accept `void`
as a pointer pointee type, producing `TypePointer(TypeVoid)`. This is
already implied by the C ABI contract (opaque pointers) but may need
explicit type checker support.

### 6. Pointer cast

Add a builtin for casting between pointer types:

```dao
fn ptr_cast<T>(ptr: *void): *T
```

Semantics: reinterpret a `*void` as `*T`. No runtime cost (LLVM
pointers are opaque).

**Safety:** requires `mode unsafe =>`.

**Rationale:** allocation returns `*void`. Library code needs to cast
it to `*T` to use it. This is the typed bridge between the
type-erased allocation surface and typed pointer operations.

### 7. Trap / panic hook

Add a runtime hook for aborting with a message:

| Hook | Signature | Semantics |
|------|-----------|-----------|
| `__dao_panic` | `(msg: string): void` | Print message to stderr and abort. Does not return. |

**Dao declaration:** in `stdlib/core/panic.dao`:

```dao
extern fn __dao_panic(msg: string): void
```

Or a user-facing wrapper:

```dao
fn panic(msg: string): void -> __dao_panic(msg)
```

**Rationale:** bounds checking in library containers needs a way to
trap on violation. This is not container-specific — it is a general
program-abort primitive.

## Proof of concept

After all deliverables are complete, write a **single proof-of-concept
example** (`examples/raw_memory.dao`) that demonstrates the full
substrate without implementing a container:

```dao
fn main(): i32
  // Allocate space for 4 i32 values
  let raw = __dao_mem_alloc(size_of<i32>() * 4, align_of<i32>())
  mode unsafe =>
    let data = ptr_cast<i32>(raw)

    // Store values
    *data = 10
    *ptr_offset(data, 1) = 20
    *ptr_offset(data, 2) = 30
    *ptr_offset(data, 3) = 40

    // Read them back
    print(*data)
    print(*ptr_offset(data, 1))
    print(*ptr_offset(data, 2))
    print(*ptr_offset(data, 3))

  __dao_mem_free(raw)
  return 0
```

This proves the substrate is sufficient for typed heap access without
any container abstraction.

## What this task does NOT include

- `Vector<T>` or any container type
- Container APIs, methods, or literals
- Collection iteration protocols
- Maps, sets, or other data structures
- Ownership, destructor, or drop semantics
- Borrow checking or lifetime tracking
- Syntax sugar for pointer operations
- `memcpy` / bulk copy intrinsic (can be expressed as a loop over
  the substrate for now; a dedicated intrinsic is a future
  optimization)

## What is deliberately deferred

| Topic | Reason |
|-------|--------|
| `memcpy` / bulk memory ops | Expressible as loop; optimize later |
| Destructor / drop semantics | Separate design task |
| Borrow checker / lifetimes | Separate design task |
| Pointer arithmetic beyond offset | `ptr_offset` is sufficient |
| Array types (`[T; N]`) | Separate type system extension |
| Slice types (`[]T`) | Depends on containers/arrays |
| Custom allocator abstraction | Premature; raw hooks are enough |

## Exit criteria

1. `*ptr = value` works inside `mode unsafe =>` — end-to-end
2. `ptr_offset<T>(ptr, i)` compiles and produces correct GEP
3. `size_of<T>()` and `align_of<T>()` return correct constants
   after monomorphization
4. `null_ptr<T>()` produces a typed null; pointer `==` / `!=` works
5. `__dao_mem_alloc` / `__dao_mem_realloc` / `__dao_mem_free` are
   callable from Dao code
6. `ptr_cast<T>(ptr)` works inside `mode unsafe =>`
7. `__dao_panic` aborts with a message
8. `examples/raw_memory.dao` compiles, runs, and produces correct
   output
9. The substrate is sufficient to implement `Vector<T>` as ordinary
   library code in a subsequent task (no compiler-privileged
   container ops needed)
