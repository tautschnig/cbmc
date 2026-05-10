/// \file
/// Test-only definitions: minimal completions of kernel types that
/// property-module headers intentionally leave opaque.  Real-kernel
/// builds include the kernel's own headers and do NOT see this file.
///
/// Include this file *before* any `properties/*/*.h` in standalone
/// tests so the forward declarations in those headers unify with a
/// definition that tests can actually instantiate.

#ifndef LINUX_PROPERTIES_TEST_SUPPORT_H
#define LINUX_PROPERTIES_TEST_SUPPORT_H

/// Minimal definition of `struct page`.  The size is irrelevant; only
/// distinct storage-class identity matters for use as a pointer-based
/// key in the `page_provenance` side table.  A single byte keeps the
/// type trivially instantiable on the stack.
struct page
{
  char __marker;
};

#endif // LINUX_PROPERTIES_TEST_SUPPORT_H
