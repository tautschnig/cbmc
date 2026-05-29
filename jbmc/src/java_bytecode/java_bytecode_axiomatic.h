/*******************************************************************\

Module: Axiomatic collection lowering for JBMC

Author: Kiro <kiro-agent@users.noreply.github.com>

Description: Replace calls to java.util.HashMap and HashSet
methods (`<init>`, get, put, containsKey, size, isEmpty,
remove, clear) with direct CBMC IR using global SMT-LIB
associative arrays. Avoids the hash-collision soundness gap
of the pure-Java axiomatic-models.jar and avoids the array-
flattening explosion of giant `Object[]` arrays.

Activated by --axiomatic-collections.

\*******************************************************************/

#ifndef CPROVER_JAVA_BYTECODE_JAVA_BYTECODE_AXIOMATIC_H
#define CPROVER_JAVA_BYTECODE_JAVA_BYTECODE_AXIOMATIC_H

#include <util/symbol_table_base.h>

class goto_modelt;

/// Walk every function in `goto_model` and replace recognised
/// HashMap / HashSet method calls with direct CBMC IR backed
/// by global SMT-LIB associative arrays. Adds the backing
/// global symbols to `goto_model.symbol_table` on first use.
///
/// Returns the number of calls rewritten. Idempotent on a
/// rewritten model (subsequent runs find no CALLs to lower).
std::size_t lower_axiomatic_collections(goto_modelt &goto_model);

/// Build the axiomatic-collections inline expression for
/// `receiver.size()` on a HashMap- or HashSet-shaped Java
/// receiver. The expression reads the per-receiver entry
/// in the global `_sz` array and is suitable for embedding
/// inside JML predicates (and thus inside ASSERT / ASSUME
/// instructions, where direct method calls are not legal).
///
/// `receiver` should be a pointer-typed expression naming the
/// Java object whose `size()` is being queried. The function
/// inserts the global `_sz` symbol into `symbol_table` if it
/// is not yet present.
exprt jml_axiomatic_size_expr(
  class symbol_table_baset &symbol_table,
  const exprt &receiver);

/// Build the axiomatic-collections inline expression for
/// `receiver.isEmpty()`. Equivalent to `size() == 0`. See
/// `jml_axiomatic_size_expr` for argument semantics.
exprt jml_axiomatic_is_empty_expr(
  class symbol_table_baset &symbol_table,
  const exprt &receiver);

#endif
