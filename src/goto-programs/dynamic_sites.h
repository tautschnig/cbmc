/*******************************************************************\

Module: Dynamic sites

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Dynamic sites: places where a string's value decides the structure of
/// the program, such as a shell command name that is an expansion. A
/// front-end marks each one with a call of DYNAMIC_SITE_FUNCTION, whose
/// arguments are the site's identifier and the string. A resolution gives
/// a site's candidates; the front-end converts the site for those, and
/// asserts that the value is one of them. See
/// doc/ADR/dynamic_structure_resolution.md.

#ifndef CPROVER_GOTO_PROGRAMS_DYNAMIC_SITES_H
#define CPROVER_GOTO_PROGRAMS_DYNAMIC_SITES_H

#include <util/cprover_prefix.h>
#include <util/irep.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

class code_function_callt;
class exprt;
class goto_modelt;
class message_handlert;
class source_locationt;
class symbol_table_baset;
class ui_message_handlert;

/// The function that marks a dynamic site: its arguments are a constant
/// of string type, the site's identifier, the value, and optionally the
/// constant DYNAMIC_SITE_TEMPLATES if the front-end can use templates with
/// holes as candidates for the site (see dynamic_resolutiont).
#define DYNAMIC_SITE_FUNCTION CPROVER_PREFIX "dynamic_site"

/// See DYNAMIC_SITE_FUNCTION.
#define DYNAMIC_SITE_TEMPLATES "templates"

#define OPT_DYNAMIC_SITES                                                      \
  "(dynamic-resolutions):(show-dynamic-sites)(resolve-dynamic-sites)"

#define HELP_DYNAMIC_SITES                                                     \
  " {y--show-dynamic-sites} \t show the places where a string's value "        \
  "decides the structure of the program\n"                                     \
  " {y--resolve-dynamic-sites} \t find the candidates of dynamic sites by "    \
  "analysing the program, and check that the values are among them\n"          \
  " {y--dynamic-resolutions} {ufile} \t read the candidates of dynamic sites " \
  "from the JSON object {ufile}, which maps site identifiers to arrays of "    \
  "candidates: strings, or arrays of strings for the parts of a template "     \
  "with any strings between them; the values at the sites are checked to "     \
  "be among them\n"

/// A template L0 Σ* L1 ... Σ* Ln, by its literal parts L0, ..., Ln: the
/// strings that are the parts with any strings, its holes, between them.
/// A template of one part is just that string.
using dynamic_templatet = std::vector<std::string>;

/// Whether \p value is in the language of \p pattern.
bool template_matches(
  const dynamic_templatet &pattern,
  const std::string &value);

/// The candidates of a dynamic site: a regular language of a restricted
/// shape, the finite union of \ref templates. A front-end that cannot use
/// templates with holes for a site converts that site as unresolved.
struct dynamic_resolutiont
{
  std::set<dynamic_templatet> templates;

  /// Whether no value is a candidate (the site is not reached).
  bool empty() const
  {
    return templates.empty();
  }

  /// Whether \p value is one of the candidates.
  bool contains(const std::string &value) const;

  /// The templates without holes: the strings that are candidates.
  std::set<std::string> values() const;

  /// The templates with holes.
  std::set<dynamic_templatet> with_holes() const;

  bool has_holes() const
  {
    return !with_holes().empty();
  }

  /// Add \p value as a candidate.
  void insert(const std::string &value)
  {
    templates.insert(dynamic_templatet{value});
  }

  bool operator==(const dynamic_resolutiont &other) const
  {
    return templates == other.templates;
  }
  bool operator!=(const dynamic_resolutiont &other) const
  {
    return !(*this == other);
  }
};

/// \p pattern as text: its parts in quotes, with * for each hole.
std::string template_to_string(const dynamic_templatet &pattern);

/// The candidates of each dynamic site, by its identifier.
using dynamic_resolutionst = std::map<irep_idt, dynamic_resolutiont>;

/// Add the candidates of \p other to \p resolution, dropping the strings
/// that a template with holes contains.
void join_dynamic_resolution(
  dynamic_resolutiont &resolution,
  const dynamic_resolutiont &other);

/// Read \p resolutions from the JSON file \p filename.
/// \return true on error
bool read_dynamic_resolutions(
  const std::string &filename,
  message_handlert &message_handler,
  dynamic_resolutionst &resolutions);

/// Read \p resolutions from the JSON text \p text.
/// \return true on error
bool parse_dynamic_resolutions(
  const std::string &text,
  message_handlert &message_handler,
  dynamic_resolutionst &resolutions);

/// \p resolutions as JSON text, as read by parse_dynamic_resolutions.
std::string dynamic_resolutions_to_json(
  const dynamic_resolutionst &resolutions);

/// The call that marks dynamic site \p site_id, whose value is \p value;
/// \p templates: whether the front-end can use templates with holes as
/// candidates for it.
/// Declares DYNAMIC_SITE_FUNCTION in \p symbol_table, without a body, with
/// a parameter for the identifier followed by an ellipsis, so that each
/// front-end can pass values of its own type.
code_function_callt dynamic_site_marker(
  const irep_idt &site_id,
  const exprt &value,
  const irep_idt &mode,
  symbol_table_baset &symbol_table,
  const source_locationt &location,
  bool templates = false);

/// Show the dynamic sites of \p goto_model: the identifier and location of
/// each, and its candidates in \p resolutions, if given.
void show_dynamic_sites(
  const goto_modelt &goto_model,
  const dynamic_resolutionst *resolutions,
  ui_message_handlert &message_handler);

/// Replace the markers of dynamic sites by skips, and remove
/// DYNAMIC_SITE_FUNCTION.
void remove_dynamic_site_markers(goto_modelt &goto_model);

#endif // CPROVER_GOTO_PROGRAMS_DYNAMIC_SITES_H
