// @@
//   SmPL rule: tocttou_inode_check.cocci
//
//   Coccinelle prefilter for "check-then-act" TOCTOU race bug
//   shapes.  Flags patterns where a guard test on a struct
//   field is followed (in the same function body, without an
//   intervening lock) by an action using the same field.
// @@

@ check_then_act_field @
expression x;
identifier f;
position p;
@@

if (x->f ...) {
    ...
    \( x->f ... \| ... x->f ... \)@p
    ...
}

@ script:python check_then_act_field_report @
p << check_then_act_field.p;
@@

coccilib.report.print_report(p[0],
    "tocttou_inode_check: check-then-act on the same field — "
    "concurrent mutators can change the field between the "
    "check and the act (race_or_toctoue / TOCTOU bug class). "
    "Hold a lock across the sequence, re-validate at the "
    "act site, or act on a captured value.")
