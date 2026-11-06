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

class code_function_callt;
class exprt;
class goto_modelt;
class message_handlert;
class source_locationt;
class symbol_table_baset;
class ui_message_handlert;

/// The function that marks a dynamic site: its arguments are a constant
/// of string type, the site's identifier, the value, and optionally a
/// constant of string type, the text of the language of all strings that
/// contain it, which the front-end can use as candidates for the site (see
/// dynamic_resolutiont::containing).
#define DYNAMIC_SITE_FUNCTION CPROVER_PREFIX "dynamic_site"

#define OPT_DYNAMIC_SITES                                                      \
  "(dynamic-resolutions):(show-dynamic-sites)(resolve-dynamic-sites)"

#define HELP_DYNAMIC_SITES                                                     \
  " {y--show-dynamic-sites} \t show the places where a string's value "        \
  "decides the structure of the program\n"                                     \
  " {y--resolve-dynamic-sites} \t find the candidates of dynamic sites by "    \
  "analysing the program, and check that the values are among them\n"          \
  " {y--dynamic-resolutions} {ufile} \t read the candidates of dynamic sites " \
  "from the JSON object {ufile}, which maps site identifiers to arrays of "    \
  "strings, or to objects with such an array \"values\" and a string "         \
  "\"containing\"; the values at the sites are checked to be among them\n"

/// The candidates of a dynamic site: \ref values, and, if
/// \ref containing is set, all strings that contain it. A front-end that
/// cannot use a language of the form of \ref containing for a site
/// converts that site as unresolved.
struct dynamic_resolutiont
{
  std::set<std::string> values;
  std::optional<std::string> containing;

  /// Whether no value is a candidate (the site is not reached).
  bool empty() const
  {
    return values.empty() && !containing.has_value();
  }

  /// Whether \p value is one of the candidates.
  bool contains(const std::string &value) const
  {
    return values.count(value) != 0 ||
           (containing.has_value() &&
            value.find(*containing) != std::string::npos);
  }

  bool operator==(const dynamic_resolutiont &other) const
  {
    return values == other.values && containing == other.containing;
  }
  bool operator!=(const dynamic_resolutiont &other) const
  {
    return !(*this == other);
  }
};

/// The candidates of each dynamic site, by its identifier.
using dynamic_resolutionst = std::map<irep_idt, dynamic_resolutiont>;

/// Add the candidates of \p other to \p resolution, dropping values that
/// its language contains.
/// \return false if the result cannot be represented: the languages of
///   both differ.
bool join_dynamic_resolution(
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
/// the front-end can use the strings that contain \p containing as
/// candidates, unless it is empty.
/// Declares DYNAMIC_SITE_FUNCTION in \p symbol_table, without a body, with
/// a parameter for the identifier followed by an ellipsis, so that each
/// front-end can pass values of its own type.
code_function_callt dynamic_site_marker(
  const irep_idt &site_id,
  const exprt &value,
  const irep_idt &mode,
  symbol_table_baset &symbol_table,
  const source_locationt &location,
  const std::string &containing = "");

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
