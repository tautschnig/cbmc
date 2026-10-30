/// \file
/// The shell converter class, shared by the files that implement it:
/// shell_converter.cpp (initial state and entry point),
/// shell_convert_words.cpp (word expansion, XCU 2.6),
/// shell_convert_arith.cpp (arithmetic expansion, XCU 2.6.4),
/// shell_convert_test.cpp (the test utility), and
/// shell_convert_commands.cpp (commands, XCU 2.9).
///
/// Internal to src/shell; see shell_converter.h for the interface.

#ifndef CPROVER_SHELL_SHELL_CONVERT_CLASS_H
#define CPROVER_SHELL_SHELL_CONVERT_CLASS_H

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/c_types.h>
#include <util/cprover_prefix.h>
#include <util/message.h>
#include <util/namespace.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol_table_base.h>

#include "shell_arith.h"
#include "shell_ast.h"
#include "shell_converter.h"
#include "shell_runtime.h"

#include <functional>
#include <map>
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

  typet string_type() const
  {
    return struct_tag_typet{"tag-" SHELL_STRING_TAG};
  }

  const struct_typet &string_struct() const
  {
    return ns.follow_tag(struct_tag_typet{"tag-" SHELL_STRING_TAG});
  }

  const typet &component_type(const irep_idt &name) const
  {
    return string_struct().get_component(name).type();
  }

  static typet status_type()
  {
    return unsigned_char_type();
  }

  static typet integer_type()
  {
    return signed_long_long_int_type();
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

  member_exprt length_of(const exprt &string) const
  {
    return member_exprt{string, "length", component_type("length")};
  }

  exprt is_set(const exprt &string) const
  {
    return notequal_exprt{
      set_of(string), from_integer(0, component_type("set"))};
  }

  exprt is_empty(const exprt &string) const
  {
    return equal_exprt{
      length_of(string), from_integer(0, component_type("length"))};
  }

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

  static typet count_type()
  {
    return unsigned_long_long_int_type();
  }

  /// A list of at most max_arguments strings.
  array_typet field_array_type() const
  {
    return array_typet{
      string_type(), from_integer(max_arguments, c_index_type())};
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
    return result;
  }

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

  /// Call runtime function \p name, storing the result in a temporary.
  symbol_exprt call(
    const irep_idt &name,
    exprt::operandst arguments,
    code_blockt &dest,
    const source_locationt &loc)
  {
    const symbolt &function = symbol_table.lookup_ref(name);
    const typet &return_type = to_code_type(function.type).return_type();
    const symbol_exprt result = temporary(return_type, dest, loc);
    assign(
      dest,
      result,
      side_effect_expr_function_callt{
        function.symbol_expr(), std::move(arguments), return_type, loc},
      loc);
    return result;
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
    if(max < 255)
    {
      add(
        dest,
        code_assumet{binary_relation_exprt{
          status(), ID_le, from_integer(max, status_type())}},
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
  // Strings

  exprt string_literal(const std::string &text, const source_locationt &loc)
  {
    if(text.size() > max_length)
    {
      log.error().source_location = loc;
      log.error() << "string literal of " << text.size()
                  << " bytes exceeds --shell-max-string-length " << max_length
                  << messaget::eom;
      throw shell_conversion_errort{};
    }
    const array_typet &data_type = to_array_type(component_type("data"));
    exprt::operandst bytes;
    for(std::size_t i = 0; i < max_length; ++i)
    {
      const unsigned char c =
        i < text.size() ? static_cast<unsigned char>(text[i]) : 0;
      bytes.push_back(from_integer(c, data_type.element_type()));
    }
    // Follow the components of the C struct, which include padding.
    exprt::operandst operands;
    for(const auto &component : string_struct().components())
    {
      if(component.get_name() == "set")
        operands.push_back(from_integer(1, component.type()));
      else if(component.get_name() == "length")
        operands.push_back(from_integer(text.size(), component.type()));
      else if(component.get_name() == "data")
        operands.push_back(array_exprt{std::move(bytes), data_type});
      else
        operands.push_back(from_integer(0, component.type()));
    }
    return struct_exprt{std::move(operands), string_type()};
  }

  /// The unset value, as of a variable that was never assigned.
  exprt unset_string(const source_locationt &loc)
  {
    exprt result = string_literal("", loc);
    for(auto &operand : result.operands())
    {
      if(operand.type() == component_type("set"))
      {
        operand = from_integer(0, component_type("set"));
        break;
      }
    }
    return result;
  }

  /// The value of a parameter: its contents if set, else the empty string.
  exprt value_of(const exprt &parameter, const source_locationt &loc)
  {
    return if_exprt{
      is_set(parameter), parameter, string_literal("", loc), string_type()};
  }

  exprt concat(
    const exprt &a,
    const exprt &b,
    code_blockt &dest,
    const source_locationt &loc)
  {
    check(
      dest,
      binary_relation_exprt{
        plus_exprt{length_of(a), length_of(b)},
        ID_le,
        from_integer(max_length, component_type("length"))},
      "shell-string-length",
      "string exceeds --shell-max-string-length (" +
        std::to_string(max_length) + ")",
      loc);
    return call(SHELL_RT_CONCAT, {a, b}, dest, loc);
  }

  exprt equal(
    const exprt &a,
    const exprt &b,
    code_blockt &dest,
    const source_locationt &l)
  {
    const symbol_exprt result = call(SHELL_RT_EQUAL, {a, b}, dest, l);
    return notequal_exprt{result, from_integer(0, result.type())};
  }

  exprt from_integer_expr(
    const exprt &value,
    code_blockt &dest,
    const source_locationt &loc)
  {
    return call(
      SHELL_RT_FROM_INTEGER,
      {typecast_exprt::conditional_cast(value, integer_type())},
      dest,
      loc);
  }

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
    bool word_start);

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

  static bool has_side_effects(const shell_word_partt &part);
  static bool has_side_effects(const shell_wordt &word);

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
