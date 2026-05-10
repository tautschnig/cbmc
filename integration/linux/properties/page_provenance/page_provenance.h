/// \file
/// Ghost-state primitive: provenance tag on a kernel `struct page`.
///
/// Many kernel bugs turn on the provenance of the page backing a
/// kernel buffer: a page derived from a user iovec is attacker-writable
/// and kernel writes through it are safe; a page brought in from the
/// page cache of a file the attacker can only read is attacker-readable
/// and kernel writes through it are a capability violation.  The
/// provenance is an extrinsic property of the page that a regular C
/// build cannot express.
///
/// This module tracks provenance as CBMC ghost state, without modifying
/// `struct page`.  The backing store is a small pointer-keyed side
/// table; its capacity `PAGE_PROV_TABLE_SIZE` defaults to 16 and may
/// be raised via `-DPAGE_PROV_TABLE_SIZE=<n>` on the compile command.
///
/// Harnesses (and, eventually, annotated versions of the kernel
/// allocators that bring pages into the kernel) use `set_page_prov`
/// to tag a page.  Contracts use `page_prov_of` inside
/// `__CPROVER_requires` / `__CPROVER_ensures` to express invariants.
///
/// See the parent `properties/README.md` for module conventions.

#ifndef LINUX_PROPERTIES_PAGE_PROVENANCE_H
#define LINUX_PROPERTIES_PAGE_PROVENANCE_H

#ifndef PAGE_PROV_TABLE_SIZE
#  define PAGE_PROV_TABLE_SIZE 16
#endif

/// Provenance classifier for a `struct page`.
///
/// New values must keep `PAGE_PROV_UNSET` at zero so that default-zero
/// table entries read as "unknown."
typedef enum
{
  /// Default: no annotation has been recorded for this page.
  /// Contracts should not draw conclusions from this value.
  PAGE_PROV_UNSET = 0,
  /// Page is backed by user-iovec memory; writes on behalf of the
  /// caller are permitted.
  PAGE_USER_WRITABLE,
  /// Page is part of the page cache of a file the caller only has
  /// read access to; writes on behalf of the caller are a capability
  /// violation.
  PAGE_CACHE_RO,
  /// Page is internal kernel memory; it should never surface to
  /// userspace at all.
  PAGE_KERNEL_ONLY,
} page_provenance_t;

/// Opaque handle; kept as a forward declaration so the module does not
/// require `linux/mm.h` to be available during verification of the
/// property-module library itself.  A real-kernel harness `#include`s
/// the kernel's own `struct page` definition first, and this file's
/// forward declaration then merges with it.
struct page;

// ----------------------------------------------------------------------------
// Backing store.  Exposed so `set_page_prov`'s assigns clause can
// reference it.  Clients never touch these directly; `set_page_prov`
// and `page_prov_of` are the only sanctioned accessors.
// ----------------------------------------------------------------------------

struct __page_prov_entry
{
  struct page *page;
  page_provenance_t prov;
};

extern struct __page_prov_entry __page_prov_table[PAGE_PROV_TABLE_SIZE];
extern unsigned int __page_prov_table_size;

// ----------------------------------------------------------------------------
// Public API.
// ----------------------------------------------------------------------------

/// Pure: return the provenance tag most recently recorded for `p`, or
/// `PAGE_PROV_UNSET` if no tag is on file.  Safe to call from
/// `__CPROVER_requires` / `__CPROVER_ensures` / `__CPROVER_assert`.
page_provenance_t page_prov_of(struct page *p) __CPROVER_assigns();

/// Record that `p` has provenance `prov`.  Last write wins.  Silently
/// succeeds if the backing table is full, which manifests at
/// verification time as the affected page reading back as
/// `PAGE_PROV_UNSET`; a harness that needs more capacity raises
/// `PAGE_PROV_TABLE_SIZE`.
void set_page_prov(struct page *p, page_provenance_t prov)
  __CPROVER_requires(p != (struct page *)0) __CPROVER_assigns(
    __CPROVER_object_whole(__page_prov_table),
    __page_prov_table_size);

#endif // LINUX_PROPERTIES_PAGE_PROVENANCE_H
