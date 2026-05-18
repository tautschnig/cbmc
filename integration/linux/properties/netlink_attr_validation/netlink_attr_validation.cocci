// @@
//   SmPL rule: netlink_attr_validation.cocci
//
//   Coccinelle prefilter for "nla_get_<type>(attr) without
//   prior length validation".
//
//   Three call-site rules:
//
//   1. nla_get_u32: reads 4 bytes; bug if attr's payload < 4.
//   2. nla_get_u64: reads 8 bytes.
//   3. nla_get_u16: reads 2 bytes.
//
//   We don't flag nla_get_u8 (1 byte) — the bug class still
//   applies but the false-positive rate for any attribute that
//   has any payload at all is too high for a useful prefilter.
//
//   Motivating CVE class: 649 'out_of_bounds' CVEs in the
//   2023-2026 kernel CVE survey, with a substantial fraction
//   in netlink/netfilter handlers.
// @@

@ nla_get_u32_call @
expression attr;
position p;
@@

nla_get_u32@p(attr)

@ script:python nla_get_u32_report @
p << nla_get_u32_call.p;
@@

coccilib.report.print_report(p[0],
    "netlink_attr_validation: nla_get_u32 call site — candidate "
    "for CBMC property scan (OOB-read bug class on struct nlattr)")

@ nla_get_u64_call @
expression attr;
position p;
@@

nla_get_u64@p(attr)

@ script:python nla_get_u64_report @
p << nla_get_u64_call.p;
@@

coccilib.report.print_report(p[0],
    "netlink_attr_validation: nla_get_u64 call site — candidate "
    "for CBMC property scan (OOB-read bug class on struct nlattr)")

@ nla_get_u16_call @
expression attr;
position p;
@@

nla_get_u16@p(attr)

@ script:python nla_get_u16_report @
p << nla_get_u16_call.p;
@@

coccilib.report.print_report(p[0],
    "netlink_attr_validation: nla_get_u16 call site — candidate "
    "for CBMC property scan (OOB-read bug class on struct nlattr)")
