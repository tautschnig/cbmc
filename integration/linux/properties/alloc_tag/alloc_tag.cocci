// @@
//   SmPL rule: alloc_tag.cocci
//
//   Coccinelle prefilter for the alloc_tag property module.
//   Flags every call site of `vfree` and (optionally)
//   `kfree` — either is a candidate for allocator-mismatch
//   review.
//
//   Precision is CBMC's job: when spatch reports a hit, running
//   scan.py's alloc_tag kernel adapter pins the appropriate
//   `alloc_tag_*_ok(p)` precondition on the corresponding free
//   API and the direct-call harness exercises the mismatched-
//   allocator shape.
// @@

@ vfree_call @
expression p;
position pos;
@@

vfree@pos(p)

@ script:python vfree_report @
pos << vfree_call.pos;
@@

coccilib.report.print_report(pos[0],
    "alloc_tag: vfree call site — candidate for CBMC property "
    "scan (kmalloc/vmalloc allocator-mismatch bug class)")

@ kvfree_call @
expression p;
position pos;
@@

kvfree@pos(p)

@ script:python kvfree_report @
pos << kvfree_call.pos;
@@

coccilib.report.print_report(pos[0],
    "alloc_tag: kvfree call site — candidate for CBMC property "
    "scan (allocator-mismatch bug class; kvfree is the safe API "
    "but surrounding context may still misuse tags)")
