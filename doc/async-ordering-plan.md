# Plan: True Asynchronous Ordering Support

**Goal:** Model JavaScript's asynchronous execution semantics (event loop,
microtasks, macrotasks, promise resolution ordering) soundly rather than
as pure sequential execution.

**Current state:** `async/await` is treated as synchronous. `Promise.resolve(x)`
returns `x` immediately. `Promise.then(fn)` calls `fn(p)` synchronously.
True async interleaving between multiple promises is not modeled.

**KNOWNBUG:** `regression/typescript/async-ordering/main.ts`

## Why It's Hard

JavaScript has a single-threaded event loop with specific ordering rules:
1. Synchronous code runs to completion
2. After each synchronous task, microtasks (Promise callbacks) run
3. After microtask queue drains, macrotasks (setTimeout) run
4. Browser/Node can interleave I/O, timers, etc.

For verification, we need to explore all possible orderings that JS could
produce. But JS is single-threaded, so we don't need general concurrency
(mutex, atomicity). We need **scheduling nondeterminism**.

## Approach: Use CBMC's Thread Modeling

CBMC has existing support for multi-threaded programs via:
- `__CPROVER_atomic_begin()` / `__CPROVER_atomic_end()` for atomic sections
- `__CPROVER_set_must()` / `assume()` for scheduling constraints
- The `--no-partial-loops` flag for interleaving exploration

For single-threaded JS, we don't want true thread concurrency (no shared
memory races). We want **task-level interleaving** where async functions
are tasks and the scheduler picks one at a time.

## Proposed Architecture

### Phase 1: Task Queue Model

Introduce a global "task queue" as a bounded array of pending async
operations. Each async function call becomes a task enqueue:

```
__ts_task_queue: Task[MAX_TASKS]
__ts_task_count: number

async function foo() { ... }  // body wrapped in task
await foo();                   // task enqueue, then yield
```

An `await` expression:
1. Enqueues the current continuation as a task
2. Yields control (synchronously picks next task nondeterministically)

### Phase 2: Promise State Tracking

Promises become first-class objects with state:
```
struct Promise<T> {
  state: Pending | Fulfilled | Rejected,
  value: T,
  handlers: Handler[]
}
```

`Promise.resolve(v)` creates a promise in `Fulfilled(v)` state.
`Promise.then(fn)` registers a handler; if already fulfilled, enqueue
a task to call `fn(value)`.

### Phase 3: Microtask/Macrotask Scheduling

Two task queues:
- Microtask queue (Promise callbacks): drain before next sync step
- Macrotask queue (setTimeout, setInterval): one per event loop tick

Nondet scheduler picks any valid ordering. For bounded verification,
limit total task count (e.g., 16).

### Phase 4: Async Function Transformation

Transform:
```
async function f() { await g(); body(); }
```

Into a state machine:
```
function f() {
  return {
    state: 0,
    step(): boolean {
      if (state == 0) { task_enqueue(g(), resume_f_1); return false; }
      if (state == 1) { body(); return true; }
    }
  };
}
```

This is similar to Babel's regenerator transform.

## Complexity Estimate

| Phase | Lines | Difficulty |
|-------|-------|-----------|
| 1. Task queue model | ~80 | Medium |
| 2. Promise state tracking | ~150 | Medium-High |
| 3. Microtask/macrotask scheduling | ~100 | Medium |
| 4. Async function transformation | ~300 | High |
| Tests | ~80 | Low |
| **Total** | **~710** | **High** |

## Alternative: Simple Interleaving

A much simpler approach that captures most bugs: treat each `await`
as a nondet yield point. At each yield, the scheduler picks any
pending task nondeterministically. This doesn't model true promise
semantics but catches ordering bugs.

```
async function foo() { await bar(); doX(); }
async function baz() { await qux(); doY(); }
```

Bounded model: try all interleavings of doX and doY up to depth N.

**Complexity for this simpler version:** ~200 lines.

## Recommendation

Start with the simple interleaving approach. Implement:

1. **Async yield points**: Each `await` becomes a `__CPROVER_nondet_schedule()`
   call that may select a different pending task.
2. **Task queue**: Bounded array of pending continuations.
3. **Scheduler**: Nondet picks from queue at each yield.
4. **Promise value tracking**: Only resolved values, no handler chains.

Test cases:
- Interleaving of two async functions
- Race conditions between promises
- Ordering of microtasks vs macrotasks (simplified)

## Risks

- **Soundness:** True async semantics are complex. A simplified model
  might miss bugs that the full model would catch. Document limitations.
- **Performance:** Nondet scheduling at every `await` can cause path
  explosion. Bound with `--unwind`.
- **Compatibility:** Existing synchronous model has 512 tests. Ensure
  the new async model doesn't break them (they use `async` but don't
  need true ordering).

## Decision

This is a large, complex feature that requires careful design. I recommend:
- Implementing the simple interleaving approach as a PROTOTYPE
- Running existing tests to ensure no regression
- Adding new tests specifically for async ordering
- Documenting what IS and ISN'T modeled

**Estimated time:** 2-3 focused sessions (~800 lines total including tests).

**Blocker for now:** Requires sustained attention that doesn't fit in the
current session context. Deferring for future work.
