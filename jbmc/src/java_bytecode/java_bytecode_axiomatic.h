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

#endif
