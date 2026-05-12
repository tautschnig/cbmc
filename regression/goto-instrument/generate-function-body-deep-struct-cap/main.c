// Regression test for LIM-008: --generate-function-body havoc on a
// function whose return type transitively reaches wide, deep, but
// *non-recursive* struct hierarchies used to hang (and then OOM)
// because the `max_nondet_tree_depth` cap only fires when the same
// struct tag re-appears on the pointer chain.  Kernel-style struct
// hierarchies are rarely self-referential in the first few levels,
// so the cap never fires and the object factory generates an
// exponentially large init body.
//
// The new `max_dynamic_object_instances` parameter hard-caps the
// total number of dynamic objects the factory will emit, making
// the body-generation terminate in linear time regardless of
// struct-hierarchy topology.
//
// This test wires up a moderate-depth, branching, non-recursive
// struct hierarchy and asserts that body generation completes
// quickly (no hang) and that the resulting goto binary is cbmc-
// verifiable in reasonable time.

typedef struct l4
{
  int a, b, c, d;
} l4_t;

typedef struct l3
{
  l4_t *p1;
  l4_t *p2;
  l4_t *p3;
  int x;
} l3_t;

typedef struct l2
{
  l3_t *p1;
  l3_t *p2;
  l3_t *p3;
  int x;
} l2_t;

typedef struct l1
{
  l2_t *p1;
  l2_t *p2;
  l2_t *p3;
  int x;
} l1_t;

// Function with an l1_t * return type: without the cap, havoc-body
// generation recursively nondet-inits l1 -> l2 -> l3 -> l4,
// branching 3-way at each level, producing an exponentially large
// init body.  With the cap, generation terminates after a bounded
// number of allocations.
l1_t *make_l1(void);

int main(void)
{
  l1_t *p = make_l1();
  // No substantive property under test; the point of the regression
  // is that goto-instrument completes in bounded time / space and
  // cbmc then trivially verifies.
  return p != (l1_t *)0 ? 1 : 0;
}
