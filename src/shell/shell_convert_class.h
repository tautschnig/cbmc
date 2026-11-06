/// \file
/// The shell converter class, shared by the files that implement it:
/// shell_converter.cpp (initial state and entry point),
/// shell_convert_words.cpp (word expansion, XCU 2.6),
/// shell_convert_arith.cpp (arithmetic expansion, XCU 2.6.4),
/// shell_convert_test.cpp (the test utility),
/// shell_convert_commands.cpp (commands, XCU 2.9), and
/// shell_convert_strings.cpp (the representations of strings), among
/// others named after their topic.
///
/// Internal to src/shell; see shell_converter.h for the interface.

#ifndef CPROVER_SHELL_SHELL_CONVERT_CLASS_H
#define CPROVER_SHELL_SHELL_CONVERT_CLASS_H

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/c_types.h>
#include <util/cprover_prefix.h>
#include <util/mathematical_types.h>
#include <util/message.h>
#include <util/namespace.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol_table_base.h>

#include "shell_arith.h"
#include "shell_ast.h"
#include "shell_converter.h"
#include "shell_pattern.h"

#include <bitset>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>

/// Thrown to abandon the conversion after an error has been reported.
struct shell_conversion_errort
{
};

/// How the value of an expanded word is used, which decides the parts of
/// word expansion (XCU 2.6) that must be modelled.
enum class shell_word_contextt
{
  /// A variable assignment: no field splitting or pathname expansion;
  /// tilde expansion after "=" and unquoted ":" (XCU 2.6.1).
  ASSIGNMENT,
  /// A word whose value is used and that must yield exactly one field:
  /// unquoted expansions, which field splitting with a nondeterministic
  /// IFS could split, and unquoted pattern characters are unsupported.
  FIELD,
  /// The subject of case: no field splitting or pathname expansion.
  CASE_SUBJECT,
  /// A word whose value is not used, such as an argument of an external
  /// command: only expansions with side effects are evaluated.
  IGNORED
};

/// Whether \p name is a builtin in all shells (shell_convert_command.cpp).
bool is_builtin_everywhere(const std::string &name);

/// What a simple command's name refers to.
enum class shell_command_kindt
{
  COLON,
  TRUE,
  FALSE,
  TEST,
  BRACKET,
  EXIT,
  EXPORT,
  UNSET,
  BREAK,
  CONTINUE,
  SHIFT,
  SET,
  RETURN,
  EXEC,
  DOT,
  /// read, which assigns variables values that are not modelled.
  READ,
  /// trap: see shell_convert_trap.cpp.
  TRAP,
  /// (( expression )) and let (bash, mksh and zsh).
  ARITHMETIC_COMMAND,
  /// [[ expression ]]: see shell_convert_double_bracket.cpp.
  DOUBLE_BRACKET,
  /// getopts: see shell_convert_getopts.cpp.
  GETOPTS,
  /// cd, umask and type: see shell_convert_builtins.cpp.
  CD,
  UMASK,
  TYPE,
  KILL,
  ASSERT,
  ASSUME,
  /// echo and printf: output only; their status is nondeterministic, as
  /// writing the output can fail.
  OUTPUT,
  /// A builtin that changes the shell's state in ways not modelled yet.
  UNSUPPORTED_BUILTIN,
  /// A utility run as a separate process: it cannot change the shell's
  /// variables, and its exit status is nondeterministic.
  EXTERNAL
};

/// Labels of an enclosing loop, for break and continue.
struct shell_loop_labelst
{
  irep_idt break_label;
  irep_idt continue_label;
  symbol_exprt loop_status;
};

class shell_convertert
{
public:
  shell_convertert(
    const shell_programt &program,
    const shell_convert_optionst &options,
    symbol_table_baset &symbol_table,
    message_handlert &message_handler)
    : program(program),
      max_length(options.max_string_length),
      max_arguments(options.max_arguments),
      strings(options.strings),
      resolutions(options.resolutions),
      symbol_table(symbol_table),
      ns(symbol_table),
      log(message_handler),
      arith_options(::arith_options(program.dialect))
  {
  }

  void operator()();

private:
  const shell_programt &program;
  const std::size_t max_length;
  const std::size_t max_arguments;
  const shell_string_representationt strings;
  /// The candidates of dynamic sites.
  const dynamic_resolutionst &resolutions;
  symbol_table_baset &symbol_table;
  namespacet ns;
  messaget log;
  const shell_arith_optionst arith_options;

  std::size_t temporary_count = 0;
  std::size_t label_count = 0;
  std::vector<shell_loop_labelst> loops;

  /// Variables and special parameters that the script uses, for the
  /// initial state.
  std::set<std::string> variables;
  std::set<std::string> special_parameters;

  /// The literal values of variables where the command being converted
  /// runs: a variable is here if, on every path there, its last assignment
  /// was of this literal. Writes update it (note_assignment), compound
  /// commands forget the variables they may assign before and after them
  /// (forget_assigned), and function calls, dot scripts and command names
  /// that are expansions forget everything.
  std::map<std::string, std::string> known_literals;

  /// The function whose body is being converted, for naming temporaries.
  std::string current_function = SHELL_MAIN_FUNCTION;
  /// Whether a shell function's body is being converted.
  bool in_function = false;

  /// The function definitions in the script, by name, in source order.
  std::map<std::string, std::vector<const shell_commandt *>>
    function_definitions;

  // ----------------------------------------------------------------
  // Errors

  [[noreturn]] void
  unsupported(const source_locationt &location, const std::string &what)
  {
    log.error().source_location = location;
    log.error() << "unsupported shell construct: " << what << messaget::eom;
    throw shell_conversion_errort{};
  }

  // ----------------------------------------------------------------
  // Types and symbols

  /// The type of shell values: a struct of a `set` flag and a `value` of
  /// payload_type().
  typet string_type() const
  {
    return struct_tag_typet{"tag-" SHELL_VALUE_TAG};
  }

  const struct_typet &string_struct() const
  {
    return ns.follow_tag(to_struct_tag_type(string_type()));
  }

  const typet &component_type(const irep_idt &name) const
  {
    return string_struct().get_component(name).type();
  }

  bool native() const
  {
    return strings == shell_string_representationt::NATIVE;
  }

  /// The string itself, without the `set` flag: string_typet for native
  /// strings, and a refined string {length, content} for refinement.
  typet payload_type() const;

  /// Add the type symbol of string_type().
  void add_value_type();

  member_exprt payload_of(const exprt &string) const
  {
    return member_exprt{string, "value", payload_type()};
  }

  /// A set value whose string is \p payload.
  exprt make_value(exprt payload) const;
  /// A set value whose string \p payload is the decimal representation of
  /// \p value.
  exprt make_int_value(exprt payload, const exprt &value) const;
  /// \p value with the set flag set.
  exprt set_value(const exprt &value) const;
  /// Whether \p value is known to be the representation of its int_value.
  exprt known_int(const exprt &value) const;

  /// Exit statuses, counts and indices are mathematical integers with
  /// native strings, as integer_type() is, so that converting them to
  /// strings does not mix Int and bit-vectors.
  typet status_type() const
  {
    if(native())
      return integer_typet{};
    return unsigned_char_type();
  }

  /// Integers: 64-bit, or mathematical integers with native strings, whose
  /// conversions to and from strings are of the SMT-LIB Int sort. Mixing
  /// Int and bit-vectors there is slow in z3 and cvc5; arithmetic then
  /// checks that its values are in range instead (arith_min, arith_max).
  typet integer_type() const
  {
    if(native())
      return integer_typet{};
    return signed_long_long_int_type();
  }

  /// The range of arithmetic values.
  mp_integer arith_min() const
  {
    return -power(2, arith_width() - 1);
  }
  mp_integer arith_max() const
  {
    return power(2, arith_width() - 1) - 1;
  }

  /// The width of arithmetic: mksh uses 32-bit integers.
  std::size_t arith_width() const
  {
    return program.dialect == shell_dialectt::MKSH ? 32 : 64;
  }

  member_exprt set_of(const exprt &string) const
  {
    return member_exprt{string, "set", component_type("set")};
  }

  exprt is_set(const exprt &string) const
  {
    return notequal_exprt{
      set_of(string), from_integer(0, component_type("set"))};
  }

  exprt is_empty(const exprt &string) const;

  symbol_exprt global_symbol(
    const std::string &identifier,
    const std::string &base_name,
    const typet &type)
  {
    if(!symbol_table.has_symbol(identifier))
    {
      symbolt symbol{identifier, type, shell_mode()};
      symbol.base_name = base_name;
      symbol.pretty_name = base_name;
      symbol.is_lvalue = true;
      symbol.is_state_var = true;
      symbol.is_static_lifetime = true;
      symbol_table.add(symbol);
      state_symbols.insert(identifier);
    }
    return symbol_table.lookup_ref(identifier).symbol_expr();
  }

  symbol_exprt status()
  {
    return global_symbol("shell::status", "?", status_type());
  }

  /// $#, the number of positional parameters.
  symbol_exprt argument_count()
  {
    return global_symbol("shell::argc", "#", count_type());
  }

  typet count_type() const
  {
    if(native())
      return integer_typet{};
    return unsigned_long_long_int_type();
  }

  /// The index type of arrays of values.
  typet index_type() const
  {
    if(native())
      return integer_typet{};
    return c_index_type();
  }

  /// A list of at most max_arguments strings.
  array_typet field_array_type() const
  {
    return array_typet{
      string_type(), from_integer(max_arguments, index_type())};
  }

  /// The positional parameters: element i is $(i+1). Elements at and after
  /// $# are unset.
  symbol_exprt positional_array()
  {
    return global_symbol("shell::args", "@", field_array_type());
  }

  /// The symbol of shell variable \p name. Every access to a variable
  /// goes through here, which rejects the variables that the shell
  /// maintains (check_parameter_name): field splitting, in particular,
  /// relies on IFS keeping its default value.
  symbol_exprt variable(const std::string &name, const source_locationt &loc)
  {
    check_parameter_name(name, loc);
    // IFS has no symbol (see shell_convert_ifs.cpp).
    if(name == "IFS")
      unsupported(loc, "this use of IFS");
    // RANDOM has no symbol either (see expand_parameter).
    if(name == "RANDOM")
      unsupported(loc, "this use of RANDOM");
    // Nor do arrays (see shell_convert_arrays.cpp).
    if(arrays.count(name) != 0)
      unsupported(loc, "this use of the array " + name);
    variables.insert(name);
    return global_symbol("shell::var::" + name, name, string_type());
  }

  /// UID, EUID and PPID: read-only variables in bash, which sets them to
  /// decimal numbers at startup.
  static bool is_bash_readonly_id(const std::string &name);
  /// The value of UID, EUID or PPID, in bash or sh, which may be bash.
  symbol_exprt bash_readonly_id(const std::string &name)
  {
    variables.insert(name);
    return global_symbol("shell::var::" + name, name, string_type());
  }

  symbol_exprt special(const std::string &name)
  {
    special_parameters.insert(name);
    return global_symbol("shell::special::" + name, name, string_type());
  }

  /// A new local variable, declared in \p dest.
  symbol_exprt
  temporary(const typet &type, code_blockt &dest, const source_locationt &loc)
  {
    const std::string base_name = "tmp" + std::to_string(temporary_count++);
    symbolt symbol{current_function + "::" + base_name, type, shell_mode()};
    symbol.base_name = base_name;
    symbol.is_lvalue = true;
    symbol.is_state_var = true;
    symbol.is_auxiliary = true;
    symbol.location = loc;
    symbol_table.add(symbol);
    const symbol_exprt result = symbol.symbol_expr();
    code_frontend_declt decl{result};
    decl.add_source_location() = loc;
    dest.add(std::move(decl));
    live_temporaries.push_back(result);
    return result;
  }

  /// Temporaries declared by the commands being converted, which end with
  /// the command that declared them (see convert_command).
  std::vector<symbol_exprt> live_temporaries;

  irep_idt new_label(const std::string &purpose)
  {
    return "shell_" + purpose + "_" + std::to_string(label_count++);
  }

  static void add(code_blockt &dest, codet code, const source_locationt &loc)
  {
    code.add_source_location() = loc;
    dest.add(std::move(code));
  }

  /// Assignments to temporaries are hidden from traces.
  void assign(
    code_blockt &dest,
    const exprt &lhs,
    exprt rhs,
    const source_locationt &loc)
  {
    source_locationt assign_loc = loc;
    if(
      lhs.id() == ID_symbol &&
      symbol_table.lookup_ref(to_symbol_expr(lhs).get_identifier())
        .is_auxiliary)
    {
      assign_loc.set_hide();
    }
    add(dest, code_frontend_assignt{lhs, std::move(rhs)}, assign_loc);
  }

  exprt nondet(const typet &type, const source_locationt &loc)
  {
    return side_effect_expr_nondett{type, loc};
  }

  /// A nondeterministic status in [0, max].
  void
  nondet_status(code_blockt &dest, unsigned max, const source_locationt &loc)
  {
    assign(dest, status(), nondet(status_type(), loc), loc);
    if(max < 255 || native())
    {
      add(
        dest,
        code_assumet{and_exprt{
          binary_relation_exprt{
            status(), ID_ge, from_integer(0, status_type())},
          binary_relation_exprt{
            status(), ID_le, from_integer(max, status_type())}}},
        loc);
    }
  }

  void set_status(code_blockt &dest, unsigned value, const source_locationt &l)
  {
    assign(dest, status(), from_integer(value, status_type()), l);
  }

  /// Assert \p condition with the given property, then assume it, so
  /// that paths violating it are reported once and not explored further.
  void check(
    code_blockt &dest,
    const exprt &condition,
    const std::string &property_class,
    const std::string &comment,
    const source_locationt &loc)
  {
    source_locationt check_loc = loc;
    check_loc.set_property_class(property_class);
    check_loc.set_comment(comment);
    code_assertt assertion{condition};
    assertion.add_source_location() = check_loc;
    dest.add(std::move(assertion));
    add(dest, code_assumet{condition}, loc);
  }

  // ----------------------------------------------------------------
  // Strings (shell_convert_strings.cpp)
  //
  // The operations below are written for each representation. Refined
  // strings emit `cprover_string_*` applications whose results are fresh
  // temporaries, so that each execution of an operation (in a loop or a
  // function called twice) constrains new symbols; native strings are
  // SMT-LIB String terms.

  exprt string_literal(const std::string &text, const source_locationt &loc);

  /// The unset value, as of a variable that was never assigned.
  exprt unset_string(const source_locationt &loc);

  /// The value of a parameter: its contents if set, else the empty string.
  /// Unset unbounded values have an empty payload (see
  /// environment_string), so their value is a copy with the flag set.
  exprt value_of(const exprt &parameter, const source_locationt &)
  {
    return set_value(parameter);
  }

  exprt concat(
    const exprt &a,
    const exprt &b,
    code_blockt &dest,
    const source_locationt &loc);

  exprt equal(
    const exprt &a,
    const exprt &b,
    code_blockt &dest,
    const source_locationt &loc);

  /// The decimal representation of \p value.
  exprt from_integer_expr(
    const exprt &value,
    code_blockt &dest,
    const source_locationt &loc);

  /// The length of \p string, of integer_type().
  exprt string_length(
    const exprt &string,
    code_blockt &dest,
    const source_locationt &loc);

  /// A set value of nondeterministic contents without null bytes, as in the
  /// environment, arguments, or command output.
  exprt nondet_string(code_blockt &dest, const source_locationt &loc);

  /// A variable as inherited from the environment: set or unset.
  exprt environment_string(code_blockt &dest, const source_locationt &loc);

  /// The white space that may follow an integer operand of the test
  /// utility, which may always start with isspace() bytes: none (zsh),
  /// space and tab (bash), or isspace() bytes (dash).
  enum class trailing_blankst
  {
    NONE,
    SPACE_TAB,
    ANY
  };

  /// The integer that \p string denotes, stored in \p value, and whether it
  /// is valid, for the test utility's integer operands (followed by
  /// \p trailing blanks) and for exit and return.
  exprt test_integer(
    const exprt &string,
    trailing_blankst trailing,
    const symbol_exprt &value,
    code_blockt &dest,
    const source_locationt &loc);

  /// arith_integer for strings whose int_value is not known.
  exprt parse_arith_integer(
    const exprt &string,
    bool leading_zero_octal,
    bool any_space,
    const symbol_exprt &value,
    code_blockt &dest,
    const source_locationt &loc);

  /// test_integer for strings whose int_value is not known.
  /// For refined strings: the number of blanks at the start of \p payload,
  /// or at its end if \p at_end, if at most refined_blanks, and
  /// refined_blanks + 1 if more. Blanks are isspace() bytes, or only space
  /// and tab if \p space_tab.
  exprt refined_blank_count(
    const exprt &payload,
    bool at_end,
    bool space_tab,
    code_blockt &dest,
    const source_locationt &loc);
  exprt parse_test_integer(
    const exprt &string,
    trailing_blankst trailing,
    const symbol_exprt &value,
    code_blockt &dest,
    const source_locationt &loc);

  /// The same for the value of a variable in arithmetic expansion.
  exprt arith_integer(
    const exprt &string,
    bool leading_zero_octal,
    bool any_space,
    const symbol_exprt &value,
    code_blockt &dest,
    const source_locationt &loc);

  // Primitive operations on payloads.

  /// Apply string function \p id to \p arguments; the result, of type
  /// \p type (a payload, Boolean, or integer_type()), is a temporary.
  exprt string_function(
    const irep_idt &id,
    const exprt::operandst &arguments,
    const typet &type,
    code_blockt &dest,
    const source_locationt &loc);
  /// Give the refined string \p payload, whose content is a temporary, a
  /// fresh array of its length, associated with it for the solver.
  void back_refined_string(
    const exprt &payload,
    code_blockt &dest,
    const source_locationt &loc);
  exprt payload_literal(const std::string &text) const;
  exprt payload_length(
    const exprt &payload,
    code_blockt &dest,
    const source_locationt &loc);
  exprt payload_concat(
    const exprt &a,
    const exprt &b,
    code_blockt &dest,
    const source_locationt &loc);
  /// payload[start, end), where 0 <= start <= end <= length.
  exprt payload_substring(
    const exprt &payload,
    const exprt &start,
    const exprt &end,
    code_blockt &dest,
    const source_locationt &loc);
  exprt payload_has_prefix(
    const exprt &payload,
    const exprt &prefix,
    code_blockt &dest,
    const source_locationt &loc);
  exprt payload_has_suffix(
    const exprt &payload,
    const exprt &suffix,
    code_blockt &dest,
    const source_locationt &loc);
  /// The least index at or after \p from where \p needle occurs, or -1.
  exprt payload_index_of(
    const exprt &payload,
    const exprt &needle,
    const exprt &from,
    code_blockt &dest,
    const source_locationt &loc);
  /// The greatest index at or before \p upto where \p needle occurs, or -1.
  /// \p anywhere: \p upto is the length of \p payload minus that of
  /// \p needle, so that any occurrence counts.
  exprt payload_last_index_of(
    const exprt &payload,
    const exprt &needle,
    const exprt &upto,
    bool anywhere,
    code_blockt &dest,
    const source_locationt &loc);

  // ----------------------------------------------------------------
  // Words (XCU 2.6)

  exprt expand_word(
    const shell_wordt &word,
    shell_word_contextt context,
    code_blockt &dest,
    const source_locationt &loc);

  exprt expand_parts(
    const std::vector<shell_word_partt> &parts,
    shell_word_contextt context,
    bool quoted,
    bool word_start,
    code_blockt &dest);

  exprt expand_part(
    const shell_word_partt &part,
    shell_word_contextt context,
    bool quoted,
    bool word_start,
    code_blockt &dest);

  std::string unquoted_literal(
    const shell_word_partt &part,
    shell_word_contextt context,
    bool word_start,
    code_blockt &dest);

  static std::string double_quoted_literal(const std::string &text);

  exprt expand_parameter(
    const shell_word_partt &part,
    shell_word_contextt context,
    bool quoted,
    code_blockt &dest);

  exprt substring_expansion(
    const shell_word_partt &part,
    const exprt &value,
    code_blockt &dest);
  exprt substitute_pattern(
    const shell_word_partt &part,
    const exprt &value,
    code_blockt &dest);
  exprt expand_star_operator(
    const shell_word_partt &part,
    shell_word_contextt context,
    bool quoted,
    code_blockt &dest);
  exprt expand_parameter_operator(
    const shell_word_partt &part,
    const exprt &parameter,
    bool assignable,
    shell_word_contextt context,
    bool quoted,
    code_blockt &dest);

  exprt parameter(const shell_word_partt &part);
  exprt
  expand_command_substitution(const shell_word_partt &part, code_blockt &dest);

  bool has_side_effects(const shell_word_partt &part) const;
  bool has_side_effects(const shell_wordt &word) const;
  /// Whether \p word has side effects other than command substitutions,
  /// which change nothing in the shell.
  bool has_state_side_effects(const shell_wordt &word) const;

  /// The text of \p word if it has no expansions, after quote removal.
  static std::optional<std::string>
  static_text(const shell_wordt &word, bool quoted = false);

  /// The word as written, for property descriptions.
  static std::string display(const shell_wordt &word);
  /// Whether \p word may yield no field: it has only unquoted
  /// expansions, which may all be empty.
  static bool may_vanish(const shell_wordt &word);

  // ----------------------------------------------------------------
  // Positional parameters and lists of fields (XCU 2.5.1, 2.5.2)

  /// A list of fields being built, such as the arguments of a function.
  struct field_listt
  {
    symbol_exprt array;
    symbol_exprt count;
  };

  exprt positional_parameter(std::size_t n, const source_locationt &loc);
  field_listt new_field_list(code_blockt &dest, const source_locationt &loc);
  void append_field(
    const field_listt &list,
    const exprt &value,
    code_blockt &dest,
    const source_locationt &loc);
  /// Check that \p list fits within max_arguments; fields beyond it were
  /// not stored.
  void check_field_list(
    const field_listt &list,
    code_blockt &dest,
    const source_locationt &loc);
  void append_positional_parameters(
    const field_listt &list,
    code_blockt &dest,
    const source_locationt &loc);
  /// Expand \p words into a list of fields; each word yields one field,
  /// except "$@", which yields one per positional parameter.
  field_listt expand_fields(
    std::vector<shell_wordt>::const_iterator begin,
    std::vector<shell_wordt>::const_iterator end,
    code_blockt &dest,
    const source_locationt &loc);
  /// Whether \p word is a single unquoted expansion, which field
  /// splitting may split into several fields (or none).
  static bool is_splittable(const shell_wordt &word);
  /// Append the fields of the unquoted expansion \p part to \p list.
  void split_fields(
    const field_listt &list,
    const shell_word_partt &part,
    code_blockt &dest,
    const source_locationt &loc);
  void split_fields_searching(
    const field_listt &list,
    const exprt &s,
    const std::bitset<256> &blanks,
    code_blockt &dest,
    const source_locationt &loc);
  /// The fields of \p value, the expansion of unquoted \p part, split
  /// with \p ifs, after pathname expansion checks.
  void split_value(
    const field_listt &list,
    const exprt &value,
    const shell_word_partt &part,
    std::string ifs,
    code_blockt &dest,
    const source_locationt &loc);
  /// For a word in a list of fields that pathname expansion applies to:
  /// the pattern, with its unquoted parameter expansions quoted (see
  /// shell_convert_pathnames.cpp).
  std::optional<shell_wordt> pathname_pattern(const shell_wordt &word) const;
  /// The fields of \p word, whose pattern is \p pattern, after pathname
  /// expansion.
  void expand_pathnames(
    const field_listt &list,
    const shell_wordt &word,
    const shell_wordt &pattern,
    code_blockt &dest,
    const source_locationt &loc);
  /// The fields of unquoted $@ as a whole word, \p part.
  void split_positional_parameters(
    const field_listt &list,
    const shell_word_partt &part,
    code_blockt &dest,
    const source_locationt &loc);
  void split_fields_delimited(
    const field_listt &list,
    const exprt &s,
    const std::bitset<256> &delimiters,
    code_blockt &dest,
    const source_locationt &loc);
  /// Report the values of unquoted \p part that pathname expansion could
  /// change.
  void check_no_pattern_characters(
    const exprt &value,
    const shell_word_partt &part,
    code_blockt &dest);
  static bool is_quoted_at(const shell_wordt &word);
  static bool contains_at(const shell_wordt &word);
  /// The text before and after "$@" in \p word, for words with one $@,
  /// which is double-quoted, such as x"a $@ b"y.
  static std::optional<std::pair<shell_wordt, shell_wordt>>
  split_at(const shell_wordt &word);
  /// The fields of x"$@"y for the expansions of \p before (x) and
  /// \p after (y): x$1, $2, ..., ${n}y, or the one field xy if n is 0.
  void append_positional_parameters_between(
    const field_listt &list,
    const shell_wordt &before,
    const shell_wordt &after,
    code_blockt &dest,
    const source_locationt &loc);
  /// "$*" outside field splitting: the positional parameters joined with
  /// spaces (the first character of the default IFS).
  exprt
  join_positional_parameters(code_blockt &dest, const source_locationt &loc);
  exprt join_positional_parameters(
    const std::string &separator,
    code_blockt &dest,
    const source_locationt &loc);
  /// "$@" in an assignment in bash and mksh.
  exprt join_with_spaces(code_blockt &dest, const source_locationt &loc);
  void set_positional_parameters(
    const field_listt &list,
    code_blockt &dest,
    const source_locationt &loc);
  void convert_shift(const shell_commandt &command, code_blockt &dest);
  void convert_set(const shell_commandt &command, code_blockt &dest);
  /// Convert the option argument at \p arg of set.
  /// \return the next argument
  void check_option_possible(
    const std::string &name,
    bool on,
    const source_locationt &loc);
  /// Set by convert_set_option where bash rejects the name of an option
  /// that it has on other platforms only.
  bool invalid_set_option = false;
  std::vector<shell_wordt>::const_iterator convert_set_option(
    std::vector<shell_wordt>::const_iterator arg,
    std::vector<shell_wordt>::const_iterator end,
    code_blockt &dest,
    const source_locationt &loc);
  void initialize_positional_parameters(
    code_blockt &init,
    const source_locationt &loc);

  // ----------------------------------------------------------------
  // Dot scripts (XCU 2.15 dot)

  /// The dot scripts with literal absolute paths, by path; null if the
  /// file does not exist.
  std::map<std::string, std::unique_ptr<shell_programt>> dot_scripts;
  /// Whether a dot script's body is being converted.
  bool in_dot_script = false;
  /// See shell_convert_dot.cpp.
  std::vector<std::string>
  dot_script_paths(const shell_commandt &command) const;
  /// The literal values that each variable is assigned anywhere in the
  /// script and its dot scripts.
  std::map<std::string, std::set<std::string>> dot_candidates;
  void collect_dot_candidates(const shell_command_listt &list);
  bool is_dot_command(const std::string &name) const;
  void collect_dot_scripts(const shell_command_listt &list);
  void collect_dot_scripts(const shell_word_partt &part);
  void load_dot_script(const std::string &path, const source_locationt &loc);
  static std::string dot_identifier(const std::string &path);
  void declare_dot_scripts();
  void convert_dot_scripts();
  void convert_dot(const shell_commandt &command, code_blockt &dest);
  /// Run the dot script \p path, which was read, for \p command.
  void convert_dot_script(
    const shell_commandt &command,
    const std::string &path,
    code_blockt &dest);
  /// The identifier of the dynamic site of the file name of the dot
  /// command at \p loc.
  static irep_idt dot_site(const source_locationt &loc);

  // ----------------------------------------------------------------
  // Patterns (XCU 2.14)

  shell_patternt compile_pattern(const shell_wordt &, const source_locationt &);
  /// \p word with its unquoted parameter expansions, whose values are
  /// patterns, replaced by their known values, or by quoted expansions
  /// whose values are checked to have no pattern characters.
  shell_wordt prepare_pattern(
    const shell_wordt &word,
    code_blockt &dest,
    const source_locationt &loc);
  /// Report \p value of unquoted \p part if it has pattern characters or
  /// backslashes.
  void check_literal_in_pattern(
    const exprt &value,
    const shell_word_partt &part,
    code_blockt &dest);
  exprt literal_pattern_string(
    const shell_patternt &pattern,
    code_blockt &dest,
    const source_locationt &loc);
  exprt pattern_matches(
    const shell_wordt &pattern,
    const exprt &subject,
    code_blockt &dest,
    const source_locationt &loc);
  /// ${x#p}, ${x##p}, ${x%p}, ${x%%p} for \p op and the pattern \p word.
  exprt remove_pattern(
    const exprt &value,
    const std::string &op,
    const shell_wordt &word,
    code_blockt &dest,
    const source_locationt &loc);

  // Patterns. A pattern S0*S1*...*Sn
  // of segments Si without stars matches s if S0 is at the start of s, Sn
  // at its end, and S1, ..., Sn-1 occur in order in between: the leftmost
  // occurrence of each is the best choice.

  /// A segment of a pattern between stars: `lead` ?s, a core of
  /// characters, ?s, bracket expressions and quoted expansions that does
  /// not start or end with ? (unless it is empty), and `trail` ?s.
  struct pattern_segmentt
  {
    std::size_t lead = 0;
    std::vector<shell_pattern_elementt> core;
    std::size_t trail = 0;
    /// The lengths of the core and of the whole segment.
    exprt core_width;
    exprt width;

    bool empty() const
    {
      return lead == 0 && core.empty() && trail == 0;
    }
  };
  /// What the matching of one pattern against one payload shares.
  struct pattern_contextt
  {
    /// The payload that is matched, and its length.
    exprt s;
    exprt length;
    /// The values of the quoted expansions of the pattern, as payloads.
    std::vector<exprt> dynamic;
    code_blockt &dest;
    const source_locationt &loc;
  };
  /// Expand the quoted expansions of \p pattern into \p context, once each,
  /// and split it into its segments.
  std::vector<pattern_segmentt>
  pattern_segments(const shell_patternt &pattern, pattern_contextt &context);
  /// The payload of \p elements, which are characters and quoted
  /// expansions.
  exprt elements_payload(
    const std::vector<shell_pattern_elementt> &elements,
    const pattern_contextt &context);
  /// Whether \p segment matches at the start (or the end) of the payload.
  exprt segment_at(
    const pattern_segmentt &segment,
    bool at_start,
    const pattern_contextt &context);
  /// The start of the first occurrence of \p segment at or after \p from,
  /// or -1 if there is none.
  exprt segment_index_of(
    const pattern_segmentt &segment,
    const exprt &from,
    const pattern_contextt &context);
  /// The start of the last occurrence of \p segment at or before \p upto,
  /// anywhere if there is no \p upto, or -1 if there is none.
  exprt segment_last_index_of(
    const pattern_segmentt &segment,
    const std::optional<exprt> &upto,
    const pattern_contextt &context);
  exprt core_index_of(
    const std::vector<shell_pattern_elementt> &core,
    const exprt &from,
    const pattern_contextt &context);
  exprt core_last_index_of(
    const std::vector<shell_pattern_elementt> &core,
    const exprt &upto,
    bool anywhere,
    const pattern_contextt &context);
  /// For native strings: the first (or last) occurrence of \p core, which
  /// has no quoted expansions, by regular expression membership.
  exprt regex_index_of(
    const std::vector<shell_pattern_elementt> &core,
    const exprt &from,
    const pattern_contextt &context);
  exprt regex_last_index_of(
    const std::vector<shell_pattern_elementt> &core,
    const exprt &upto,
    const pattern_contextt &context);
  /// The ends of the occurrences of the segments \p begin to \p end, which
  /// occur in order from \p from on, each at its first occurrence (or, if
  /// \p backward, in reverse order, each at its last occurrence and ending
  /// at or before \p from). The result is whether they occur, and the end
  /// (the start, if \p backward) of the last one.
  std::pair<exprt, exprt> segment_chain(
    std::vector<pattern_segmentt>::const_iterator begin,
    std::vector<pattern_segmentt>::const_iterator end,
    exprt from,
    bool backward,
    const pattern_contextt &context);
  exprt unbounded_pattern_matches(
    const shell_patternt &pattern,
    const exprt &subject,
    code_blockt &dest,
    const source_locationt &loc);
  exprt unbounded_remove_pattern(
    const shell_patternt &pattern,
    const exprt &value,
    bool prefix,
    bool longest,
    code_blockt &dest,
    const source_locationt &loc);
  /// The byte of payload \p s at \p index, as an integer.
  exprt payload_byte(
    const exprt &s,
    const exprt &index,
    code_blockt &dest,
    const source_locationt &loc);
  /// Whether \p string is 1 to 15 hexadecimal digits, whose value is then
  /// assigned to \p value.
  exprt parse_hex_digits(
    const exprt &string,
    const symbol_exprt &value,
    code_blockt &dest,
    const source_locationt &loc);
  /// Whether payload \p s contains a byte of \p set.
  exprt payload_contains_byte(
    const exprt &s,
    const std::bitset<256> &set,
    code_blockt &dest,
    const source_locationt &loc);
  /// For native strings: the regular expression of \p elements, which have
  /// no quoted expansions.
  exprt pattern_regex(const std::vector<shell_pattern_elementt> &elements);

  // ----------------------------------------------------------------
  // Shell options (XCU 2.15 set)

  /// Options that set may enable somewhere, by name.
  std::set<std::string> possible_options;
  bool nounset_possible = false;
  /// Whether -e is ignored where the code being converted runs: in the
  /// condition of if, while, and until, before && and ||, and after !.
  bool errexit_ignored = false;
  symbol_exprt option(const std::string &name);
  bool set_option(
    const std::string &name,
    bool on,
    code_blockt &dest,
    const source_locationt &loc);
  void collect_options(const shell_command_listt &list);
  void collect_options(const shell_word_partt &part);
  void initialize_options(code_blockt &init, const source_locationt &loc);
  bool option_possible(const std::string &name) const;
  /// Whether -e was ignored where the current function was called.
  symbol_exprt errexit_ignored_in_caller()
  {
    return global_symbol(
      "shell::errexit_ignored", "errexit_ignored", bool_typet{});
  }
  /// Exit if -e is on and the last command failed.
  void check_errexit(code_blockt &dest, const source_locationt &loc);
  /// Exit if -u is on and \p parameter is unset.
  void check_nounset(
    const exprt &parameter,
    const std::string &name,
    code_blockt &dest,
    const source_locationt &loc);

  // ----------------------------------------------------------------
  // Functions (XCU 2.9.5)

  void collect_function_definitions(const shell_command_listt &list);
  void collect_function_definitions(const std::vector<shell_word_partt> &);
  void declare_functions();
  static std::string function_identifier(const std::string &name, std::size_t);
  static typet current_definition_type()
  {
    return unsigned_int_type();
  }
  symbol_exprt current_definition(const std::string &name);
  std::size_t definition_number(const shell_commandt &definition) const;
  void
  convert_function_definition(const shell_commandt &command, code_blockt &);
  void convert_function_body(const shell_commandt &definition);
  void convert_function_bodies();
  void convert_function_call(const shell_commandt &command, code_blockt &);
  void convert_return(const shell_commandt &command, code_blockt &dest);
  void status_from_operand(
    const shell_wordt &word,
    const std::string &builtin,
    bool modulo,
    code_blockt &dest,
    const source_locationt &loc);

  // ----------------------------------------------------------------
  // Arithmetic (XCU 2.6.4)

  exprt expand_arithmetic(const shell_word_partt &part, code_blockt &dest);
  exprt arith(
    const shell_arith_exprt &expr,
    code_blockt &dest,
    const source_locationt &loc);
  exprt arith_read(
    const std::string &name,
    const exprt &string,
    code_blockt &dest,
    const source_locationt &loc);
  void arith_store(
    const std::string &name,
    const exprt &value,
    code_blockt &dest,
    const source_locationt &loc);
  exprt arith_binary(
    const std::string &op,
    const exprt &lhs,
    const exprt &rhs,
    code_blockt &dest,
    const source_locationt &loc);
  exprt arith_logical(
    const shell_arith_exprt &expr,
    code_blockt &dest,
    const source_locationt &loc);
  exprt arith_conditional(
    const shell_arith_exprt &expr,
    code_blockt &dest,
    const source_locationt &loc);
  /// With -u, the error of reading \p name in arithmetic while it is
  /// unset, in the shells that report it.
  void arith_nounset(
    const std::string &name,
    code_blockt &dest,
    const source_locationt &loc);
  exprt arith_assignment(
    const shell_arith_exprt &expr,
    code_blockt &dest,
    const source_locationt &loc);
  exprt arith_increment(
    const shell_arith_exprt &expr,
    code_blockt &dest,
    const source_locationt &loc);
  void arith_overflow_check(
    const exprt &overflow,
    const std::string &op,
    code_blockt &dest,
    const source_locationt &loc);
  exprt arith_type_cast(const exprt &value) const;
  /// Whether \p value is in [arith_min(), arith_max()].
  exprt arith_in_range(const exprt &value) const;
  /// \p operation on bit-vectors of the width of arithmetic.
  exprt arith_bitwise(
    const std::function<exprt(const exprt &, const exprt &)> &operation,
    const exprt &lhs,
    const exprt &rhs) const;

  // ----------------------------------------------------------------
  // The test utility

  struct test_argumentt
  {
    exprt value;
    std::optional<std::string> text;
  };

  symbol_exprt test_expression(
    const std::vector<test_argumentt> &args,
    std::size_t &i,
    std::size_t end,
    int level,
    code_blockt &dest,
    const source_locationt &loc);
  void test(
    std::vector<test_argumentt> args,
    code_blockt &dest,
    const source_locationt &loc);
  void test1(
    const test_argumentt &a,
    code_blockt &dest,
    const source_locationt &loc);
  void test2(
    const test_argumentt &a,
    const test_argumentt &b,
    code_blockt &dest,
    const source_locationt &loc);
  void test3(
    const test_argumentt &a,
    const test_argumentt &b,
    const test_argumentt &c,
    code_blockt &dest,
    const source_locationt &loc);
  void test4(
    const std::vector<test_argumentt> &args,
    code_blockt &dest,
    const source_locationt &loc);
  void negate_status(code_blockt &dest, const source_locationt &loc);
  void status_from(
    const exprt &condition,
    code_blockt &dest,
    const source_locationt &loc);
  bool is_unary_primary(const std::string &op) const;
  bool is_binary_primary(const std::string &op) const;
  void unary_test(
    const std::string &op,
    const exprt &operand,
    code_blockt &dest,
    const source_locationt &loc);
  void binary_test(
    const exprt &lhs,
    const std::string &op,
    const exprt &rhs,
    code_blockt &dest,
    const source_locationt &loc);
  void integer_test(
    const exprt &lhs,
    const std::string &op,
    const exprt &rhs,
    code_blockt &dest,
    const source_locationt &loc);
  [[noreturn]] void dynamic_operator(const source_locationt &loc);

  // ----------------------------------------------------------------
  // Commands (XCU 2.9)

  void convert_list(const shell_command_listt &list, code_blockt &dest);
  void convert_condition(const shell_command_listt &list, code_blockt &dest);
  void convert_command(const shell_commandt &command, code_blockt &dest);
  /// Convert \p command, ignoring a trailing &.
  void convert_foreground(const shell_commandt &command, code_blockt &dest);

  // ----------------------------------------------------------------
  // Subshells (XCU 2.13)

  struct subshellt
  {
    std::string identifier;
    source_locationt location;
  };
  std::vector<subshellt> subshells;
  /// Identifiers of all static-lifetime symbols of the shell state,
  /// saved and restored around subshells.
  std::set<std::string> state_symbols;
  /// Run the code that \p convert produces in a subshell.
  void run_in_subshell(
    const source_locationt &loc,
    code_blockt &dest,
    const std::function<void(code_blockt &)> &convert);
  void build_subshell_wrappers();
  void convert_subshell(const shell_commandt &command, code_blockt &dest);
  /// The status of the last command substitution of the current command.
  symbol_exprt substitution_status()
  {
    return global_symbol("shell::substitution_status", "?", status_type());
  }
  /// Whether a command substitution ran in the current command.
  symbol_exprt substitution_ran()
  {
    return global_symbol("shell::substitution_ran", "?", bool_typet{});
  }
  void convert_compound(const shell_commandt &command, code_blockt &dest);
  void convert_simple(const shell_commandt &command, code_blockt &dest);
  void convert_dynamic_simple(const shell_commandt &command, code_blockt &dest);
  /// Run \p convert into \p dest, and return whether it converted: an
  /// unsupported construct leaves \p dest, and the state of the
  /// conversion, as they were, without reporting it.
  /// The command builtin (XCU command): -v and -V, and running a builtin
  /// or utility rather than a function.
  void
  convert_command_builtin(const shell_commandt &command, code_blockt &dest);
  /// getopts: see shell_convert_getopts.cpp.
  void convert_getopts(const shell_commandt &command, code_blockt &dest);
  symbol_exprt getopts_ind();
  symbol_exprt getopts_char();
  symbol_exprt getopts_optind();
  void initialize_getopts(code_blockt &init, const source_locationt &loc);
  bool getopts_used = false;
  void
  convert_arithmetic_command(const shell_commandt &command, code_blockt &dest);
  /// [[ expression ]]: see shell_convert_double_bracket.cpp.
  void convert_double_bracket(const shell_commandt &command, code_blockt &dest);
  /// Whether \p test holds, with its side effects in \p dest.
  exprt double_bracket(const shell_testt &test, code_blockt &dest);
  exprt double_bracket_integer(
    const shell_wordt &word,
    code_blockt &dest,
    const source_locationt &loc);
  /// trap: see shell_convert_trap.cpp.
  void convert_trap(const shell_commandt &command, code_blockt &dest);
  symbol_exprt exit_trap();
  symbol_exprt in_exit_trap();
  symbol_exprt exit_trap_status();
  /// The number of the EXIT trap action \p text, parsed when it is first
  /// set.
  unsigned
  register_exit_trap(const std::string &text, const source_locationt &loc);
  void convert_exit_traps();
  /// shell::run_exit_trap, which runs the current EXIT trap.
  void add_run_exit_trap();
  struct exit_trapt
  {
    std::string text;
    std::string identifier;
    source_locationt location;
    const shell_command_listt *commands = nullptr;
  };
  std::vector<exit_trapt> exit_traps;
  source_locationt exit_trap_location;
  /// Indexed arrays: see shell_convert_arrays.cpp.
  std::set<std::string> arrays;
  std::set<std::string> array_names_used;
  void collect_arrays(const shell_command_listt &list);
  void collect_arrays(const shell_wordt &word);
  field_listt array_symbols(const std::string &name);
  void initialize_arrays(code_blockt &init, const source_locationt &loc);
  exprt array_index(
    const shell_wordt &index,
    code_blockt &dest,
    const source_locationt &loc);
  exprt array_element(
    const field_listt &array,
    const exprt &index,
    code_blockt &dest,
    const source_locationt &loc);
  void assign_array_element(
    const std::string &name,
    const exprt &index,
    const exprt &value,
    code_blockt &dest,
    const source_locationt &loc);
  void convert_array_assignment(
    const shell_assignmentt &assignment,
    code_blockt &dest);
  /// Whether \p word is "${name[@]}", or unquoted ${name[@]} or ${name[*]},
  /// and whether it is quoted.
  static bool is_array_at(const shell_wordt &word, bool &quoted);
  void append_array_elements(
    const field_listt &list,
    const shell_wordt &word,
    bool quoted,
    code_blockt &dest,
    const source_locationt &loc);
  exprt expand_array_parameter(
    const shell_word_partt &part,
    shell_word_contextt context,
    bool quoted,
    code_blockt &dest);
  /// The number of commands in \p list, including nested ones.
  static std::size_t command_size(const shell_command_listt &list);
  /// "$@" as a whole command: $1 runs with the others as arguments.
  void convert_at_command(const shell_commandt &command, code_blockt &dest);
  /// cd, umask and type: see shell_convert_builtins.cpp.
  void convert_cd(const shell_commandt &command, code_blockt &dest);
  void convert_umask(const shell_commandt &command, code_blockt &dest);
  void convert_type(const shell_commandt &command, code_blockt &dest);
  void convert_kill(const shell_commandt &command, code_blockt &dest);
  /// read: see shell_convert_read.cpp.
  void convert_read(const shell_commandt &command, code_blockt &dest);
  /// Whether read has -r, and its names, if its operands are -r, --, and
  /// names, all literal text.
  static std::optional<std::pair<bool, std::vector<std::string>>>
  read_operands(const shell_commandt &command);
  /// A value that read assigns: to the last of the variables if \p last,
  /// with -r if \p raw, splitting with \p ifs where it is known.
  exprt read_value(
    bool raw,
    bool last,
    const std::optional<std::string> &ifs,
    code_blockt &dest,
    const source_locationt &loc);
  /// The status of command -v and type for a name that is not found.
  exprt not_found_status(code_blockt &dest, const source_locationt &loc);
  /// local: see shell_convert_local.cpp.
  void convert_local(const shell_commandt &command, code_blockt &dest);
  /// NAME and the value of NAME=value, if \p word has this form with a
  /// literal NAME.
  static std::optional<std::pair<std::string, std::optional<shell_wordt>>>
  local_operand(const shell_wordt &word);
  void collect_local_names(
    const shell_command_listt &list,
    std::set<std::string> &names);
  /// Save the variables that \p body makes local, at the start of the
  /// function's body.
  void save_locals(
    const shell_command_listt &body,
    code_blockt &dest,
    const source_locationt &loc);
  /// Restore the variables that were made local, at the end of the body.
  void restore_locals(code_blockt &dest, const source_locationt &loc);
  /// In a function's body: the saved value of each variable that it may
  /// make local, and whether it did.
  std::optional<std::map<std::string, std::pair<symbol_exprt, symbol_exprt>>>
    function_locals;
  /// In a function's body: the label of its end, which return jumps to.
  irep_idt function_end_label;
  // IFS (shell_convert_ifs.cpp)
  /// Whether IFS has its default value, where it is not known.
  symbol_exprt ifs_default();
  /// The value of IFS at startup.
  std::string initial_ifs() const;
  /// Whether IFS is unset, where it is not known.
  symbol_exprt ifs_unset();
  /// The value of IFS, if it is known here; the default if it is unset.
  std::optional<std::string> known_ifs() const;
  /// The value of IFS for field splitting here: the known value, or the
  /// default, which is then checked.
  std::string ifs_for_splitting(code_blockt &dest, const source_locationt &loc);
  void assign_ifs(
    const exprt &value,
    code_blockt &dest,
    const source_locationt &loc);
  void unset_ifs(code_blockt &dest, const source_locationt &loc);
  /// Forget the value of IFS, and whether it is unset.
  void forget_ifs();
  /// The value of IFS here, or nothing if it is unset. Where it is not
  /// known, the default value, which is then checked.
  std::optional<std::string>
  ifs_value(code_blockt &dest, const source_locationt &loc);
  /// The fields of \p value split with \p ifs (XCU 2.6.5).
  static std::vector<std::string>
  split_literal(const std::string &value, const std::string &ifs);
  /// The fields of the known \p value of an unquoted parameter expansion
  /// in a list of fields, split with \p ifs where the shell splits them.
  std::vector<std::string>
  split_known_parameter(const std::string &value, const std::string &ifs) const;
  /// Whether \p ifs has only space, tab and newline (or is empty).
  static bool ifs_is_white(const std::string &ifs);
  static bool ifs_has_white(const std::string &ifs);
  /// The bytes of \p ifs.
  static std::bitset<256> ifs_set(const std::string &ifs);
  /// eval (XCU 2.15 eval).
  void convert_eval(const shell_commandt &command, code_blockt &dest);
  /// The value of \p word if it is known during conversion: literal text,
  /// and the known values of variables. An unquoted expansion yields its
  /// fields joined with spaces, as eval joins them.
  std::optional<std::string> known_text(const shell_wordt &word) const;
  /// The programs that eval parsed, which the converted code refers to.
  std::vector<std::unique_ptr<shell_programt>> eval_programs;
  /// The number of command substitutions in arithmetic so far.
  std::size_t arithmetic_substitutions = 0;
  /// The commands of \p text, known during conversion, for eval or trap
  /// (\p what): it must not define functions, enable options that set
  /// does not enable elsewhere, or run other dot scripts.
  const shell_command_listt &parse_known_text(
    const std::string &text,
    const std::string &what,
    const source_locationt &loc);
  bool try_convert(
    const std::function<void(code_blockt &)> &convert,
    code_blockt &dest);
  void
  convert_assignments_only(const shell_commandt &command, code_blockt &dest);
  void convert_builtin(
    shell_command_kindt kind,
    const shell_commandt &command,
    code_blockt &dest);
  void convert_test_command(
    const shell_commandt &command,
    bool bracket,
    code_blockt &dest);
  void convert_verification_command(
    const shell_commandt &command,
    bool assertion,
    code_blockt &dest);
  void convert_exit(const shell_commandt &command, code_blockt &dest);
  void convert_exec(const shell_commandt &command, code_blockt &dest);
  /// Leave the shell with the current status, or a nondeterministic
  /// non-zero status if \p failure, as after an error that makes a
  /// non-interactive shell exit (XCU 2.8.1).
  void exit_shell(code_blockt &dest, bool failure, const source_locationt &);
  /// shell::exited: set once the shell has exited, so that each function
  /// returns to its caller in turn.
  /// shell::posix_increments: in sh, whether the shell has the increment
  /// operators of bash and busybox in arithmetic, rather than none, as
  /// dash; chosen at startup.
  symbol_exprt posix_increments()
  {
    posix_increments_used = true;
    return global_symbol(
      "shell::posix_increments", "posix_increments", bool_typet{});
  }
  /// Whether posix_increments() was used: looking the symbol up would
  /// intern its name, which changes the order of identifiers, to which
  /// the refined string solver is sensitive.
  bool posix_increments_used = false;
  symbol_exprt exited()
  {
    return global_symbol("shell::exited", "exited", bool_typet{});
  }
  void convert_export(const shell_commandt &command, code_blockt &dest);
  void convert_unset(const shell_commandt &command, code_blockt &dest);
  /// Whether \p word has an = in its literal text.
  static bool contains_equals(const shell_wordt &word);
  /// Whether \p value is a name (XBD 3.216).
  exprt
  is_name(const exprt &value, code_blockt &dest, const source_locationt &loc);
  void convert_break_continue(
    const shell_commandt &command,
    bool is_break,
    code_blockt &dest);
  void convert_and_or(const shell_commandt &command, code_blockt &dest);
  void convert_pipeline(const shell_commandt &command, code_blockt &dest);
  void convert_background(const shell_commandt &command, code_blockt &dest);
  void convert_if(const shell_commandt &command, code_blockt &dest);
  void convert_body(
    const shell_command_listt &body,
    code_blockt &dest,
    const source_locationt &loc);
  void convert_while(const shell_commandt &command, code_blockt &dest);
  void convert_for(const shell_commandt &command, code_blockt &dest);
  void convert_for_loop(
    const shell_commandt &command,
    const field_listt &items,
    code_blockt &dest,
    std::size_t bound);
  void convert_case(const shell_commandt &command, code_blockt &dest);
  exprt case_pattern_matches(
    const shell_wordt &pattern,
    const exprt &subject,
    code_blockt &dest,
    const source_locationt &loc);
  /// Expand the redirections of \p command for their side effects; \p kind
  /// is that of the command, which is a special builtin if \p special.
  void check_redirections(
    const shell_commandt &command,
    shell_command_kindt kind,
    bool special,
    code_blockt &dest);
  /// Whether a redirection of \p command can fail: opening a file,
  /// rather than duplicating a standard descriptor or /dev/null.
  static bool redirection_may_fail(const shell_commandt &command);
  /// Model the failure of a redirection of \p command, which skips the
  /// command (XCU 2.8.1); \p run converts it.
  void with_redirections(
    const shell_commandt &command,
    bool special,
    code_blockt &dest,
    const std::function<void(code_blockt &)> &run);
  /// Convert a simple command that is not a call of a shell function.
  void convert_non_function(
    const shell_commandt &command,
    shell_command_kindt kind,
    code_blockt &dest);

  shell_command_kindt classify(const std::string &name) const;
  void check_parameter_name(const std::string &name, const source_locationt &);

  // ----------------------------------------------------------------
  // Literal values of variables (see known_literals)

  /// The text of \p value if it is a literal, set value.
  std::optional<std::string> literal_of(const exprt &value) const;
  /// The text of \p payload if it is a literal.
  std::optional<std::string> literal_payload(const exprt &payload) const;
  /// The literal value of \p part, if it is $name or ${name} of a variable
  /// whose value is known.
  std::optional<std::string> known_literal(const shell_word_partt &part) const;
  /// Record that \p name is assigned \p value.
  void note_assignment(const std::string &name, const exprt &value);
  /// Forget the variables that \p command may assign.
  void forget_assigned(const shell_commandt &command);
  /// Add the variables that \p list may assign to \p names; set \p any if
  /// it may assign variables that are not known statically.
  void collect_assigned(
    const shell_command_listt &list,
    std::set<std::string> &names,
    bool &any) const;
  void collect_assigned(
    const shell_wordt &word,
    std::set<std::string> &names,
    bool &any) const;
  void assign_variable(
    const std::string &name,
    const exprt &value,
    code_blockt &dest,
    const source_locationt &loc);

  code_ifthenelset if_status_zero(bool zero, code_blockt then_block);
  void goto_label(code_blockt &dest, const irep_idt &label);
  void label(code_blockt &dest, const irep_idt &label);

  // ----------------------------------------------------------------
  // Initial state

  void add_main(code_blockt body);
  void add_initialize();
};

#endif // CPROVER_SHELL_SHELL_CONVERT_CLASS_H
