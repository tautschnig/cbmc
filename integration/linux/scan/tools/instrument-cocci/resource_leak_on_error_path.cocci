// Coccinelle instrumentation rule for resource_leak_on_error_path.
//
// Tracks an allocation x via leak_alloc_track(x); then
// asserts __assert_no_leak_at_exit(x) before each return
// statement reached without an intervening free(x) or
// reassignment.
//
// Each alloc-API has its own rule pair (assign + decl) to
// keep cocci's per-rule CFG analysis bounded — combining
// them in a single \( ... \| ... \) alternation can cause
// "inconsistent control-flow paths" errors on functions
// with many return statements, which makes cocci abandon
// the whole file rather than just skip the troublesome
// rule.
//
// Allocator → free mapping:
//   k*alloc / kmemdup / kstrdup / kasprintf / krealloc → kfree
//   kvmalloc / kvzalloc / kvmalloc_array / kvcalloc → kvfree
//   vmalloc / vzalloc / vmalloc_user → vfree
//   alloc_skb → kfree_skb / consume_skb
//   kmem_cache_alloc / kmem_cache_zalloc → kmem_cache_free
//   __get_free_pages / __get_free_page → free_pages / free_page
//   alloc_pages / alloc_page → __free_pages / __free_page
//   dma_alloc_coherent → dma_free_coherent
//   alloc_workqueue → destroy_workqueue
//   alloc_netdev / alloc_etherdev → free_netdev

// =====================================================
// k*alloc family — frees with kfree
// =====================================================

@ kmalloc_assign @
expression x;
expression e1, e2;
@@
x = kmalloc(e1, e2);
+ leak_alloc_track(x);
... when != kfree(x)
    when != kvfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kmalloc_decl @
type T;
identifier x;
expression e1, e2;
@@
T x = kmalloc(e1, e2);
+ leak_alloc_track(x);
... when != kfree(x)
    when != kvfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kzalloc_assign @
expression x;
expression e1, e2;
@@
x = kzalloc(e1, e2);
+ leak_alloc_track(x);
... when != kfree(x)
    when != kvfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kzalloc_decl @
type T;
identifier x;
expression e1, e2;
@@
T x = kzalloc(e1, e2);
+ leak_alloc_track(x);
... when != kfree(x)
    when != kvfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kcalloc_assign @
expression x;
expression e1, e2, e3;
@@
x = kcalloc(e1, e2, e3);
+ leak_alloc_track(x);
... when != kfree(x)
    when != kvfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kcalloc_decl @
type T;
identifier x;
expression e1, e2, e3;
@@
T x = kcalloc(e1, e2, e3);
+ leak_alloc_track(x);
... when != kfree(x)
    when != kvfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kmalloc_array_assign @
expression x;
expression e1, e2, e3;
@@
x = kmalloc_array(e1, e2, e3);
+ leak_alloc_track(x);
... when != kfree(x)
    when != kvfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kmalloc_array_decl @
type T;
identifier x;
expression e1, e2, e3;
@@
T x = kmalloc_array(e1, e2, e3);
+ leak_alloc_track(x);
... when != kfree(x)
    when != kvfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kmemdup_assign @
expression x;
expression e1, e2, e3;
@@
x = kmemdup(e1, e2, e3);
+ leak_alloc_track(x);
... when != kfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kmemdup_decl @
type T;
identifier x;
expression e1, e2, e3;
@@
T x = kmemdup(e1, e2, e3);
+ leak_alloc_track(x);
... when != kfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kstrdup_assign @
expression x;
expression e1, e2;
@@
x = kstrdup(e1, e2);
+ leak_alloc_track(x);
... when != kfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kstrdup_decl @
type T;
identifier x;
expression e1, e2;
@@
T x = kstrdup(e1, e2);
+ leak_alloc_track(x);
... when != kfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kasprintf_assign @
expression x;
expression e1, e2, list;
@@
x = kasprintf(e1, e2, list);
+ leak_alloc_track(x);
... when != kfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kasprintf_decl @
type T;
identifier x;
expression e1, e2, list;
@@
T x = kasprintf(e1, e2, list);
+ leak_alloc_track(x);
... when != kfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

// =====================================================
// kv*alloc family — frees with kvfree
// =====================================================

@ kvmalloc_assign @
expression x;
expression e1, e2;
@@
x = kvmalloc(e1, e2);
+ leak_alloc_track(x);
... when != kvfree(x)
    when != kfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kvmalloc_decl @
type T;
identifier x;
expression e1, e2;
@@
T x = kvmalloc(e1, e2);
+ leak_alloc_track(x);
... when != kvfree(x)
    when != kfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kvzalloc_assign @
expression x;
expression e1, e2;
@@
x = kvzalloc(e1, e2);
+ leak_alloc_track(x);
... when != kvfree(x)
    when != kfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kvzalloc_decl @
type T;
identifier x;
expression e1, e2;
@@
T x = kvzalloc(e1, e2);
+ leak_alloc_track(x);
... when != kvfree(x)
    when != kfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kvmalloc_array_assign @
expression x;
expression e1, e2, e3;
@@
x = kvmalloc_array(e1, e2, e3);
+ leak_alloc_track(x);
... when != kvfree(x)
    when != kfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kvmalloc_array_decl @
type T;
identifier x;
expression e1, e2, e3;
@@
T x = kvmalloc_array(e1, e2, e3);
+ leak_alloc_track(x);
... when != kvfree(x)
    when != kfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kvcalloc_assign @
expression x;
expression e1, e2, e3;
@@
x = kvcalloc(e1, e2, e3);
+ leak_alloc_track(x);
... when != kvfree(x)
    when != kfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kvcalloc_decl @
type T;
identifier x;
expression e1, e2, e3;
@@
T x = kvcalloc(e1, e2, e3);
+ leak_alloc_track(x);
... when != kvfree(x)
    when != kfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

// =====================================================
// vmalloc family — frees with vfree
// =====================================================

@ vmalloc_assign @
expression x;
expression e1;
@@
x = vmalloc(e1);
+ leak_alloc_track(x);
... when != vfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ vmalloc_decl @
type T;
identifier x;
expression e1;
@@
T x = vmalloc(e1);
+ leak_alloc_track(x);
... when != vfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ vzalloc_assign @
expression x;
expression e1;
@@
x = vzalloc(e1);
+ leak_alloc_track(x);
... when != vfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ vzalloc_decl @
type T;
identifier x;
expression e1;
@@
T x = vzalloc(e1);
+ leak_alloc_track(x);
... when != vfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

// =====================================================
// alloc_skb family — frees with kfree_skb / consume_skb
// =====================================================

@ alloc_skb_assign @
expression x;
expression e1, e2;
@@
x = alloc_skb(e1, e2);
+ leak_alloc_track(x);
... when != kfree_skb(x)
    when != consume_skb(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ alloc_skb_decl @
type T;
identifier x;
expression e1, e2;
@@
T x = alloc_skb(e1, e2);
+ leak_alloc_track(x);
... when != kfree_skb(x)
    when != consume_skb(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

// =====================================================
// kmem_cache_alloc family — frees with kmem_cache_free
// =====================================================

@ kmem_cache_alloc_assign @
expression x;
expression e1, e2;
@@
x = kmem_cache_alloc(e1, e2);
+ leak_alloc_track(x);
... when != kmem_cache_free(...)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kmem_cache_alloc_decl @
type T;
identifier x;
expression e1, e2;
@@
T x = kmem_cache_alloc(e1, e2);
+ leak_alloc_track(x);
... when != kmem_cache_free(...)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kmem_cache_zalloc_assign @
expression x;
expression e1, e2;
@@
x = kmem_cache_zalloc(e1, e2);
+ leak_alloc_track(x);
... when != kmem_cache_free(...)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kmem_cache_zalloc_decl @
type T;
identifier x;
expression e1, e2;
@@
T x = kmem_cache_zalloc(e1, e2);
+ leak_alloc_track(x);
... when != kmem_cache_free(...)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

// =====================================================
// alloc_workqueue — frees with destroy_workqueue
// =====================================================

@ alloc_workqueue_assign @
expression x;
expression e1, e2, e3, list;
@@
x = alloc_workqueue(e1, e2, e3, list);
+ leak_alloc_track(x);
... when != destroy_workqueue(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ alloc_workqueue_decl @
type T;
identifier x;
expression e1, e2, e3, list;
@@
T x = alloc_workqueue(e1, e2, e3, list);
+ leak_alloc_track(x);
... when != destroy_workqueue(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

// =====================================================
// NOTE: devm_* family is intentionally NOT instrumented.
// devm_kmalloc / devm_kzalloc / devm_kcalloc /
// devm_kmemdup are devres-managed; the kernel auto-frees
// the memory when the parent device is unbound.  Treating
// them as ordinary allocations produces false positives
// because callers reasonably never call devm_kfree
// explicitly.  See doc/fp-measurement-2026-05.md for
// the FP analysis that motivated this exclusion.
// =====================================================
