/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_UTIL_SOURCE_LOCATION_H
#define CPROVER_UTIL_SOURCE_LOCATION_H

#include "deprecate.h"
#include "irep.h"

#include <optional>
#include <string>

class source_locationt : public irept
{
public:
  source_locationt()
  {
  }

  std::string as_string() const
  {
    return as_string(false);
  }

  std::string as_string_with_cwd() const
  {
    return as_string(true);
  }

  const irep_idt &get_file() const
  {
    return get(ID_file);
  }

  const irep_idt &get_working_directory() const
  {
    return get(ID_working_directory);
  }

  const irep_idt &get_line() const
  {
    return get(ID_line);
  }

  const irep_idt &get_column() const
  {
    return get(ID_column);
  }

  // This method is problematic for the following reasons:
  // 1) There is ambiguity whether
  //    the returned string is an identifier or human-readable.
  // 2) Furthermore, the linker renames functions, and is unable
  //    to adjust all source locations.
  // 3) The name of the function is not strictly a source location.
  // It will be removed.
  DEPRECATED(SINCE(2022, 10, 13, "use identifier of containing function"))
  const irep_idt &get_function() const
  {
    return get(ID_function);
  }

  const irep_idt &get_property_id() const
  {
    return get(ID_property_id);
  }

  const irep_idt &get_property_class() const
  {
    return get(ID_property_class);
  }

  const irep_idt &get_comment() const
  {
    return get(ID_comment);
  }

  /// Source-artifact provenance tag for this location, set by
  /// JBMC's contract-lowering passes (java_bytecode_contracts.cpp,
  /// jml_lowering.cpp). Read by proof_explanation::classify_step()
  /// to drive Tomb & Joshi static-coverage warnings (form 3 of the
  /// explaining-proofs deliverable). The value is one of:
  ///   precondition / postcondition / loop_invariant /
  ///   decreases / old_capture
  /// Empty for instructions with no contract provenance; callers
  /// then fall back to property_class or the structural step kind.
  const irep_idt &get_step_kind() const
  {
    return get(ID_step_kind);
  }

  const irep_idt &get_case_number() const
  {
    return get(ID_switch_case_number);
  }

  const irep_idt &get_java_bytecode_index() const
  {
    return get(ID_java_bytecode_index);
  }

  const irept &get_basic_block_source_lines() const
  {
    return find(ID_basic_block_source_lines);
  }

  bool property_fatal() const
  {
    return get_bool(ID_property_fatal);
  }

  void set_file(const irep_idt &file)
  {
    set(ID_file, file);
  }

  void set_working_directory(const irep_idt &cwd)
  {
    set(ID_working_directory, cwd);
  }

  void set_line(const irep_idt &line)
  {
    set(ID_line, line);
  }

  void set_line(unsigned line)
  {
    set(ID_line, line);
  }

  void set_column(const irep_idt &column)
  {
    set(ID_column, column);
  }

  void set_column(unsigned column)
  {
    set(ID_column, column);
  }

  DEPRECATED(SINCE(2022, 10, 13, "use identifier of containing function"))
  void set_function(const irep_idt &function)
  {
    PRECONDITION(!function.empty());
    set(ID_function, function);
  }

  void clear_function()
  {
    remove(ID_function);
  }

  void set_property_id(const irep_idt &property_id)
  {
    set(ID_property_id, property_id);
  }

  void set_property_class(const irep_idt &property_class)
  {
    set(ID_property_class, property_class);
  }

  void set_comment(const irep_idt &comment)
  {
    set(ID_comment, comment);
  }

  /// Tag this location with a contract-element provenance kind.
  /// See get_step_kind() for the recognised values.
  void set_step_kind(const irep_idt &step_kind)
  {
    set(ID_step_kind, step_kind);
  }

  // for switch case number
  void set_case_number(const irep_idt &number)
  {
    set(ID_switch_case_number, number);
  }

  void set_java_bytecode_index(const irep_idt &index)
  {
    set(ID_java_bytecode_index, index);
  }

  void set_basic_block_source_lines(irept source_lines)
  {
    add(ID_basic_block_source_lines, std::move(source_lines));
  }

  void property_fatal(bool _property_fatal)
  {
    if(_property_fatal)
      set(ID_property_fatal, true);
    else
      remove(ID_property_fatal);
  }

  void set_hide()
  {
    set(ID_hide, true);
  }

  bool get_hide() const
  {
    return get_bool(ID_hide);
  }

  static bool is_built_in(const std::string &s);

  bool is_built_in() const
  {
    return is_built_in(id2string(get_file()));
  }

  /// Set all unset source-location fields in this object to their values in
  /// 'from'. Leave set fields in this object alone.
  void merge(const source_locationt &from);

  static const source_locationt &nil()
  {
    return static_cast<const source_locationt &>(get_nil_irep());
  }

  std::optional<std::string> full_path() const;

  void add_pragma(const irep_idt &pragma)
  {
    add(ID_pragma).add(pragma);
  }

  const irept::named_subt &get_pragmas() const
  {
    return find(ID_pragma).get_named_sub();
  }

protected:
  std::string as_string(bool print_cwd) const;
};

std::ostream &operator<<(std::ostream &, const source_locationt &);

template <>
struct diagnostics_helpert<source_locationt>
{
  static std::string
  diagnostics_as_string(const source_locationt &source_location)
  {
    return "source location: " + source_location.as_string();
  }
};

#endif // CPROVER_UTIL_SOURCE_LOCATION_H
