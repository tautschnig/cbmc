// Coccinelle instrumentation rule for use_after_free_generic.
//
// Phase 1 (intra-function):
//   Inserts __assert_not_freed(x) before x->fld = E
//   assignment-statements reached after a kfree-family
//   call without intervening reassignment of x.  Detects
//   classic single-function UAF bugs.
//
// Phase 2 (cross-function):
//   Inserts mark_freed(x) AFTER each kfree-family call so
//   the property-module's shared static ghost table records
//   the freed state.  When a caller's TU is instrumented
//   independently and includes a deref of x, the assert in
//   the caller fires because the ghost was set by the
//   callee's instrumented kfree (the table is `static` in
//   use_after_free_generic.c, which is linked into the
//   final goto binary together with both kernel TUs).
//
// We deliberately do NOT match arbitrary user-defined
// freeing wrappers — that would require an inter-procedural
// call-graph analysis cocci doesn't perform.  Instead we
// cover the curated set of kernel freeing APIs below, which
// constitute the leaves of most freeing call chains.

// =====================================================
// Phase 2: insert mark_freed(x) after each free-API call.
// =====================================================

@ track_kfree @
expression x;
@@
kfree(x);
+ mark_freed(x);

@ track_kvfree @
expression x;
@@
kvfree(x);
+ mark_freed(x);

@ track_kfree_skb @
expression x;
@@
kfree_skb(x);
+ mark_freed(x);

@ track_consume_skb @
expression x;
@@
consume_skb(x);
+ mark_freed(x);

@ track_kfree_sensitive @
expression x;
@@
kfree_sensitive(x);
+ mark_freed(x);

@ track_vfree @
expression x;
@@
vfree(x);
+ mark_freed(x);

@ track_kmem_cache_free @
expression cache, x;
@@
kmem_cache_free(cache, x);
+ mark_freed(x);

// =====================================================
// Phase 1: assert before x->fld = E reached after free.
//
// Cocci's `... when != x = ...` ensures the var hasn't
// been re-assigned between free and use.
// =====================================================

@@
expression x, e;
identifier fld;
@@

\( kfree(x); \| kvfree(x); \| kfree_skb(x); \|
   consume_skb(x); \| kfree_sensitive(x); \| vfree(x); \)
... when != x = ...
+ __assert_not_freed(x);
x->fld = e;

// =====================================================
// Phase 2 (cross-function): always-emit __assert_not_freed
// before x->fld = E when x is a pointer parameter of the
// enclosing function.  The ghost may have been set by a
// callee in another TU; the contract holds vacuously when
// the ghost says "not freed", and fails when it says
// "freed".  This catches the cross-function UAF pattern
// at the cost of more contract checks.
//
// Restricted to pointer-typed function parameters to keep
// the assertion fan-out manageable on functions that
// handle many pointers.
// =====================================================

@ phase2_check_param_field_write @
identifier fn;
type T;
identifier p;
identifier fld;
expression e;
@@
fn(..., T *p, ...) {
  ...
+ __assert_not_freed(p);
  p->fld = e;
  ...
}
