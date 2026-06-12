/*******************************************************************\

Module: COBOL bundled copybook library

Author: Kiro

\*******************************************************************/

/// \file
/// Bundled copybook library for the COBOL frontend.
///
/// Compiler- and subsystem-supplied copybooks (the CICS DFHAID attention
/// identifiers and DFHBMSCA BMS attribute constants; the IBM MQ CMQ*V
/// structures and named constants) are part of the runtime/SDK, not of the
/// application source tree, so they are normally absent when an application is
/// analysed in isolation. Rather than hand-build their symbol-table entries,
/// the frontend ships their text here and expands it through the ordinary
/// COPY / data-description layout path, so a bundled copybook gets exactly the
/// same byte layout, VALUE handling and REDEFINES support as an on-disk one.
///
/// The text is fixed-format (code in columns 8-72) so it tokenises with the
/// same scanner as application source.

#ifndef CPROVER_COBOL_COBOL_COPYBOOKS_H
#define CPROVER_COBOL_COBOL_COPYBOOKS_H

#include <string>

/// Return the bundled copybook text for \p name, or nullptr if no bundled
/// copybook of that name exists. \p name is matched case-insensitively against
/// the text-name a program would COPY (e.g. "DFHAID", "CMQODV").
const std::string *cobol_builtin_copybook(const std::string &name);

#endif // CPROVER_COBOL_COBOL_COPYBOOKS_H
