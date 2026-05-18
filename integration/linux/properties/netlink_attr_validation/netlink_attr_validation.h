/// \file
/// netlink_attr_validation.h — property module for "read of a
/// netlink attribute payload before validating that the
/// attribute is large enough".
///
/// ## Bug class
///
/// `<net/netlink.h>` exposes `nla_get_u8/u16/u32/u64(attr)` and
/// friends as `static inline` accessors that simply cast
/// `nla_data(attr)` (the payload pointer) to the target type
/// and dereference.  If the attribute's payload is shorter
/// than the target type, the read goes out of bounds.
///
/// The standard kernel idiom is to call `nla_parse(...)` (or
/// `nla_validate_*`) with a policy specifying minimum sizes
/// per attribute type before any payload read.  Bugs occur
/// when:
///
///   1. A handler reads `nla_get_u32(attr)` without going
///      through nla_parse / nla_validate first.
///   2. A handler reads with a wider type than was validated
///      (e.g. policy says NLA_U32, code reads nla_get_u64).
///   3. A nested attribute's contents are read without
///      validating the nest itself.
///
/// Motivating CVE class: the `out_of_bounds` bucket from the
/// 2023-2026 kernel CVE survey (649 CVEs, 7.4% of the
/// classified volume).  Concrete recent examples in this
/// shape: CVE-2026-43450 (nfnetlink_cthelper OOB read),
/// CVE-2026-43453 (nft_set_pipapo OOB), and the netfilter
/// nft_set / cthelper / queue family.
///
/// ## Abstraction
///
/// Per-`struct nlattr *` ghost integer
/// `validated_min_size`: the minimum payload size known to be
/// safe to read at this point.  Default 0 (unvalidated).
///
/// `nla_validate_min_size(attr, n)` raises the ghost to at
/// least n.  Property contract on each typed accessor:
/// `nla_get_u32(attr)` requires
/// `nla_size_at_least(attr, 4) == 1`; etc.
///
/// ## What this module covers
///
/// * **Direct-call harness** with vuln/fix shapes for
///   `nla_get_u32` (and family).
/// * **Coccinelle prefilter** flagging every `nla_get_*`
///   call site as a candidate for triage.
/// * **Per-file synthesis**: harness initialises each
///   `struct nlattr *` parameter with `validated_min_size=0`.
///   Functions that read the attribute without first
///   validating it via `nla_validate_min_size` will fire the
///   contract precondition.
///
/// ## What this module does NOT cover (yet)
///
/// * **Modeling `nla_parse` / `nla_validate`** — these set
///   the ghost based on the policy, and we don't yet emit
///   contracts that propagate policy info into the ghost.
///   Functions that DO validate via the standard API
///   produce false-positive verdicts under per-file
///   synthesis until v2 adds policy-aware contracts.
/// * **Nested attributes**, type-mismatch (NLA_U32 policy
///   read as nla_get_u64), and string/binary attributes.

#ifndef INTEGRATION_LINUX_PROPERTIES_NETLINK_ATTR_VALIDATION_H
#define INTEGRATION_LINUX_PROPERTIES_NETLINK_ATTR_VALIDATION_H

#include <stddef.h>

struct nlattr;

// Ghost state API.
void nla_validate_min_size(struct nlattr *attr, unsigned int min_size);
void nla_validate_clear(struct nlattr *attr);
unsigned int nla_validated_size(struct nlattr *attr);

// Predicate for use in `__CPROVER_requires` clauses.
int nla_size_at_least(struct nlattr *attr, unsigned int size);

#endif
