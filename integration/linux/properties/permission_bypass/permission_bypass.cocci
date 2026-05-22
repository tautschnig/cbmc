// @@
//   SmPL rule: permission_bypass.cocci
//
//   Cocci flags candidate privileged-operation patterns where
//   the function body does NOT contain a capable() / ns_capable()
//   gate.  Crude — will produce false positives on functions
//   that delegate the check to a caller — but the cocci hits
//   are useful for triage.
// @@

@ exposes_state_no_cap @
identifier fn;
position p;
@@

fn(...) @p {
    ...
    \(setattr_(?:prepare|copy_to_inode)\(...\)
    \| netlink_(?:send|broadcast)\(...\)
    \| sock_setsockopt(...)
    \| inet_csk_get_port(...)
    \| __dev_(?:set_mtu|change_flags|set_promiscuity)\(...\)
    \)
    ...
}

@ script:python exposes_state_no_cap_report @
p << exposes_state_no_cap.p;
fn << exposes_state_no_cap.fn;
@@

coccilib.report.print_report(p[0],
    "permission_bypass: %s appears to perform privileged ops "
    "(setattr / netlink_send / sock_setsockopt / __dev_*).  "
    "Verify capable() / ns_capable() check is on every entry "
    "path (CVE class: permission_bypass)." % fn)
