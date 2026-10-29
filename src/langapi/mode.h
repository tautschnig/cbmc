/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@cs.cmu.edu

\*******************************************************************/


#ifndef CPROVER_LANGAPI_MODE_H
#define CPROVER_LANGAPI_MODE_H

#include <util/irep.h>

#include <memory> // unique_ptr

class languaget;
class namespacet;

std::unique_ptr<languaget> get_language_from_mode(const irep_idt &mode);
const irep_idt &
get_mode_from_identifier(const namespacet &ns, const irep_idt &identifier);
std::unique_ptr<languaget>
get_language_from_identifier(const namespacet &ns, const irep_idt &identifier);
std::unique_ptr<languaget> get_language_from_filename(
  const std::string &filename);
std::unique_ptr<languaget> get_default_language();

/// The interpreter named by a `#!` line at the start of \p head, the first
/// bytes of a file: the last path component of the first word, or of the
/// first operand when that word is `env` (as in `#!/usr/bin/env -S bash
/// -e`). A byte order mark for UTF-8, UTF-16, or UTF-32 before the `#!` is
/// accepted.
/// \return the interpreter, or the empty string if there is no `#!` line
std::string shebang_interpreter(const std::string &head);

typedef std::unique_ptr<languaget> (*language_factoryt)();
void register_language(language_factoryt factory);

#endif // CPROVER_LANGAPI_MODE_H
