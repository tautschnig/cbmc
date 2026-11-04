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

  symbol_exprt variable(const std::string &name)
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
  // The operations below are written for each representation. Bounded
  // strings call the C runtime; refined strings emit `cprover_string_*`
  // applications whose results are fresh temporaries, so that each
  // execution of an operation (in a loop or a function called twice)
  // constrains new symbols; native strings are SMT-LIB String terms.

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

  /// The text of \p word if it has no expansions, after quote removal.
  static std::optional<std::string>
  static_text(const shell_wordt &word, bool quoted = false);

  /// The word as written, for property descriptions.
  static std::string display(const shell_wordt &word);

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
  static bool is_quoted_at(const shell_wordt &word);
  static bool contains_at(const shell_wordt &word);
  /// "$*" outside field splitting: the positional parameters joined with
  /// spaces (the first character of the default IFS).
  exprt
  join_positional_parameters(code_blockt &dest, const source_locationt &loc);
  void set_positional_parameters(
    const field_listt &list,
    code_blockt &dest,
    const source_locationt &loc);
  void convert_shift(const shell_commandt &command, code_blockt &dest);
  void convert_set(const shell_commandt &command, code_blockt &dest);
  /// Convert the option argument at \p arg of set.
  /// \return the next argument
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
  std::optional<std::string>
  dot_script_path(const shell_commandt &command) const;
  bool is_dot_command(const std::string &name) const;
  void collect_dot_scripts(const shell_command_listt &list);
  void collect_dot_scripts(const shell_word_partt &part);
  void load_dot_script(const std::string &path, const source_locationt &loc);
  static std::string dot_identifier(const std::string &path);
  void declare_dot_scripts();
  void convert_dot_scripts();
  void convert_dot(const shell_commandt &command, code_blockt &dest);

  // ----------------------------------------------------------------
  // Patterns (XCU 2.14)

  shell_patternt compile_pattern(const shell_wordt &, const source_locationt &);
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
  symbol_exprt exited()
  {
    return global_symbol("shell::exited", "exited", bool_typet{});
  }
  void convert_export(const shell_commandt &command, code_blockt &dest);
  void convert_unset(const shell_commandt &command, code_blockt &dest);
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
    code_blockt &dest);
  void convert_case(const shell_commandt &command, code_blockt &dest);
  exprt case_pattern_matches(
    const shell_wordt &pattern,
    const exprt &subject,
    code_blockt &dest,
    const source_locationt &loc);
  void
  check_redirections(const shell_commandt &command, shell_command_kindt kind);
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
