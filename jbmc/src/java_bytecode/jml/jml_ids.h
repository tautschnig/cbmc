/*******************************************************************\

Module: JML Expression Tree IDs

Author: Kiro (AI agent)

\*******************************************************************/

/// \file
/// Centralised string identifiers for JML-specific expression nodes.
///
/// The JML parser produces uninterpreted exprt nodes for constructs
/// that don't have a direct CBMC equivalent (\\sum, \\product,
/// \\fresh, \\nonnullelements, ...). Their string IDs were
/// previously scattered as raw string literals across the parser
/// and lowering code, which made typos silent and refactoring
/// fragile. This header collects them as `extern const irep_idt`
/// constants so the compiler enforces consistency.
///
/// We keep these JML-local rather than registering them in
/// src/util/irep_ids.def: they're internal to the JML pipeline and
/// must not appear in goto-program output (every JML node should
/// either be lowered to a CBMC-recognised form, or be kept as an
/// uninterpreted boolean that downstream falls back on
/// nondeterministically).

#ifndef CPROVER_JAVA_BYTECODE_JML_JML_IDS_H
#define CPROVER_JAVA_BYTECODE_JML_JML_IDS_H

#include <util/irep.h>

namespace jml_ids
{
extern const irep_idt jml_sum;
extern const irep_idt jml_product;
extern const irep_idt jml_min;
extern const irep_idt jml_max;
extern const irep_idt jml_num_of;
extern const irep_idt jml_aggregate;
extern const irep_idt jml_fresh;
extern const irep_idt jml_nothing;
extern const irep_idt jml_everything;
extern const irep_idt jml_nonnullelements;
extern const irep_idt jml_typeof;
extern const irep_idt jml_type;
extern const irep_idt jml_method_call;
extern const irep_idt jml_field_access;
} // namespace jml_ids

#endif // CPROVER_JAVA_BYTECODE_JML_JML_IDS_H
