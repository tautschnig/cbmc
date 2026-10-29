/// \file
/// Conversion of the shell IR into symbols and code for CBMC.
///
/// Every shell value is a bounded string (see shell_runtime.h). The script
/// becomes the function shell::main; shell variables are static-lifetime
/// symbols initialised from a nondeterministic environment. The exit
/// status of the last command is the symbol shell::status.
///
/// Throughout, "unsupported" constructs are reported as errors: they are
/// never dropped, so verification results are not claimed for scripts
/// whose behaviour was not modelled.

#include "shell_converter.h"

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
#include "shell_runtime.h"

#include <map>
#include <optional>
#include <set>

const irep_idt &shell_mode()
{
  static const irep_idt mode{"shell"};
  return mode;
}

namespace
{
/// Thrown to abandon the conversion after an error has been reported.
struct conversion_errort
{
};

/// How the value of an expanded word is used, which decides the parts of
/// word expansion (XCU 2.6) that must be modelled.
enum class word_contextt
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
enum class command_kindt
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
struct loop_labelst
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
    std::size_t max_length,
    symbol_table_baset &symbol_table,
    message_handlert &message_handler)
    : program(program),
      max_length(max_length),
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
  symbol_table_baset &symbol_table;
  namespacet ns;
  messaget log;
  const shell_arith_optionst arith_options;

  std::size_t temporary_count = 0;
  std::size_t label_count = 0;
  std::vector<loop_labelst> loops;
  /// Set while expanding a word when a command substitution is expanded,
  /// which decides the status of a command without a command name.
  bool expanded_command_substitution = false;

  /// Variables, positional parameters, and special parameters that the
  /// script uses, for the initial state.
  std::set<std::string> variables;
  std::set<std::size_t> positional_parameters;
  std::set<std::string> special_parameters;

  // ----------------------------------------------------------------
  // Errors

  [[noreturn]] void
  unsupported(const source_locationt &location, const std::string &what)
  {
    log.error().source_location = location;
    log.error() << "unsupported shell construct: " << what << messaget::eom;
    throw conversion_errort{};
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
    }
    return symbol_table.lookup_ref(identifier).symbol_expr();
  }

  symbol_exprt status()
  {
    return global_symbol("shell::status", "?", status_type());
  }

  symbol_exprt argument_count()
  {
    return global_symbol("shell::argc", "#", unsigned_long_long_int_type());
  }

  symbol_exprt variable(const std::string &name)
  {
    variables.insert(name);
    return global_symbol("shell::var::" + name, name, string_type());
  }

  symbol_exprt positional(std::size_t n)
  {
    positional_parameters.insert(n);
    const std::string number = std::to_string(n);
    return global_symbol("shell::arg::" + number, number, string_type());
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
    symbolt symbol{SHELL_MAIN_FUNCTION "::" + base_name, type, shell_mode()};
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
      throw conversion_errort{};
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
    word_contextt context,
    code_blockt &dest,
    const source_locationt &loc);

  exprt expand_parts(
    const std::vector<shell_word_partt> &parts,
    word_contextt context,
    bool quoted,
    bool word_start,
    code_blockt &dest);

  exprt expand_part(
    const shell_word_partt &part,
    word_contextt context,
    bool quoted,
    bool word_start,
    code_blockt &dest);

  std::string unquoted_literal(
    const shell_word_partt &part,
    word_contextt context,
    bool word_start);

  static std::string double_quoted_literal(const std::string &text);

  exprt expand_parameter(
    const shell_word_partt &part,
    word_contextt context,
    bool quoted,
    code_blockt &dest);

  exprt expand_parameter_operator(
    const shell_word_partt &part,
    const exprt &parameter,
    bool assignable,
    word_contextt context,
    bool quoted,
    code_blockt &dest);

  exprt parameter(const shell_word_partt &part);
  exprt
  expand_command_substitution(const shell_word_partt &part, code_blockt &dest);

  static bool has_side_effects(const shell_word_partt &part);
  static bool has_side_effects(const shell_wordt &word);
  static bool contains_verification_builtin(const shell_command_listt &list);

  /// The text of \p word if it has no expansions, after quote removal.
  static std::optional<std::string>
  static_text(const shell_wordt &word, bool quoted = false);

  /// The word as written, for property descriptions.
  static std::string display(const shell_wordt &word);

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
  void convert_compound(const shell_commandt &command, code_blockt &dest);
  void convert_simple(const shell_commandt &command, code_blockt &dest);
  void
  convert_assignments_only(const shell_commandt &command, code_blockt &dest);
  void convert_builtin(
    command_kindt kind,
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
  void convert_case(const shell_commandt &command, code_blockt &dest);
  exprt case_pattern_matches(
    const shell_wordt &pattern,
    const exprt &subject,
    code_blockt &dest,
    const source_locationt &loc);
  void check_redirections(const shell_commandt &command, command_kindt kind);
  void check_pure_command(const shell_commandt &command);

  command_kindt classify(const std::string &name) const;
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

// ------------------------------------------------------------------
// Words

exprt shell_convertert::expand_word(
  const shell_wordt &word,
  word_contextt context,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(context == word_contextt::IGNORED)
  {
    // Evaluate only the parts with side effects, for those effects.
    for(const auto &part : word)
    {
      if(has_side_effects(part))
        expand_part(part, context, false, false, dest);
    }
    return string_literal("", loc);
  }
  return expand_parts(word, context, false, true, dest);
}

exprt shell_convertert::expand_parts(
  const std::vector<shell_word_partt> &parts,
  word_contextt context,
  bool quoted,
  bool word_start,
  code_blockt &dest)
{
  std::optional<exprt> result;
  std::string pending_text;
  source_locationt pending_loc;
  bool first = word_start;

  auto flush_text = [&]()
  {
    if(pending_text.empty() && result.has_value())
      return;
    exprt literal = string_literal(pending_text, pending_loc);
    result = result.has_value() ? concat(*result, literal, dest, pending_loc)
                                : literal;
    pending_text.clear();
  };

  for(const auto &part : parts)
  {
    using kindt = shell_word_partt::kindt;
    if(part.kind == kindt::LITERAL || part.kind == kindt::QUOTED_LITERAL)
    {
      if(pending_text.empty())
        pending_loc = part.location;
      pending_text += part.kind == kindt::QUOTED_LITERAL ? part.text
                      : quoted ? double_quoted_literal(part.text)
                               : unquoted_literal(part, context, first);
    }
    else
    {
      if(!pending_text.empty())
        flush_text();
      exprt value = expand_part(part, context, quoted, first, dest);
      result = result.has_value() ? concat(*result, value, dest, part.location)
                                  : value;
    }
    first = false;
  }
  if(!pending_text.empty() || !result.has_value())
    flush_text();
  return *result;
}

exprt shell_convertert::expand_part(
  const shell_word_partt &part,
  word_contextt context,
  bool quoted,
  bool word_start,
  code_blockt &dest)
{
  using kindt = shell_word_partt::kindt;
  if(
    !quoted && context == word_contextt::FIELD && part.kind != kindt::LITERAL &&
    part.kind != kindt::QUOTED_LITERAL && part.kind != kindt::DOUBLE_QUOTED)
  {
    unsupported(
      part.location,
      "unquoted expansion where field splitting would matter (XCU 2.6.5); "
      "quote it");
  }
  switch(part.kind)
  {
  case kindt::LITERAL:
  case kindt::QUOTED_LITERAL:
    return expand_parts({part}, context, quoted, word_start, dest);
  case kindt::DOUBLE_QUOTED:
    return expand_parts(part.parts, context, true, false, dest);
  case kindt::PARAMETER:
    return expand_parameter(part, context, quoted, dest);
  case kindt::COMMAND_SUBSTITUTION:
    return expand_command_substitution(part, dest);
  case kindt::ARITHMETIC:
    return expand_arithmetic(part, dest);
  }
  UNREACHABLE;
}

/// Quote removal for unquoted text (XCU 2.2.1, 2.6.7), rejecting text
/// that tilde expansion (XCU 2.6.1) or pathname expansion (XCU 2.6.6)
/// would change.
std::string shell_convertert::unquoted_literal(
  const shell_word_partt &part,
  word_contextt context,
  bool word_start)
{
  const std::string &text = part.text;
  const bool globbing = context == word_contextt::FIELD;
  std::string result;
  for(std::size_t i = 0; i < text.size(); ++i)
  {
    const char c = text[i];
    const bool tilde_position =
      c == '~' &&
      ((i == 0 && word_start && context != word_contextt::IGNORED) ||
       (context == word_contextt::ASSIGNMENT && i > 0 && text[i - 1] == ':'));
    if(tilde_position)
      unsupported(part.location, "tilde expansion (XCU 2.6.1)");
    if(globbing && (c == '*' || c == '?' || c == '['))
    {
      unsupported(
        part.location,
        "unquoted pattern character '" + std::string{c} +
          "' (pathname expansion, XCU 2.6.6); quote it");
    }
    if(c == '\\' && i + 1 < text.size())
    {
      ++i;
      if(text[i] != '\n')
        result += text[i];
    }
    else
      result += c;
  }
  return result;
}

/// Inside double quotes a backslash only escapes $ ` " \ and newline
/// (XCU 2.2.3).
std::string shell_convertert::double_quoted_literal(const std::string &text)
{
  std::string result;
  for(std::size_t i = 0; i < text.size(); ++i)
  {
    const char c = text[i];
    if(c == '\\' && i + 1 < text.size())
    {
      const char next = text[i + 1];
      if(next == '$' || next == '`' || next == '"' || next == '\\')
      {
        result += next;
        ++i;
        continue;
      }
      if(next == '\n')
      {
        ++i;
        continue;
      }
    }
    result += c;
  }
  return result;
}

/// The symbol holding the parameter named in \p part.
exprt shell_convertert::parameter(const shell_word_partt &part)
{
  const std::string &name = part.text;
  if(!name.empty() && std::isdigit(static_cast<unsigned char>(name[0])))
  {
    const std::size_t n = std::stoul(name);
    return n == 0 ? special("0") : exprt{positional(n)};
  }
  if(name == "$" || name == "-")
    return special(name);
  if(name == "@" || name == "*" || name == "!")
    unsupported(part.location, "special parameter $" + name);
  check_parameter_name(name, part.location);
  return variable(name);
}

exprt shell_convertert::expand_parameter(
  const shell_word_partt &part,
  word_contextt context,
  bool quoted,
  code_blockt &dest)
{
  const source_locationt &loc = part.location;
  exprt value;
  bool assignable = false;
  if(part.text == "?")
    value = from_integer_expr(status(), dest, loc);
  else if(part.text == "#")
    value = from_integer_expr(argument_count(), dest, loc);
  else
  {
    value = parameter(part);
    assignable = value.id() == ID_symbol &&
                 id2string(to_symbol_expr(value).get_identifier())
                     .rfind("shell::var::", 0) == 0;
  }

  if(part.parameter_length)
  {
    if(!part.parameter_operator.empty())
      unsupported(loc, "${#parameter} with an operator");
    return from_integer_expr(
      typecast_exprt{length_of(value), integer_type()}, dest, loc);
  }
  if(part.parameter_operator.empty())
    return value_of(value, loc);
  return expand_parameter_operator(
    part, value, assignable, context, quoted, dest);
}

/// ${parameter<op>word} for the operators of XCU 2.6.2 except pattern
/// removal.
exprt shell_convertert::expand_parameter_operator(
  const shell_word_partt &part,
  const exprt &parameter,
  bool assignable,
  word_contextt context,
  bool quoted,
  code_blockt &dest)
{
  const source_locationt &loc = part.location;
  const std::string &op = part.parameter_operator;
  if(op == "%" || op == "%%" || op == "#" || op == "##")
    unsupported(loc, "pattern removal ${parameter" + op + "word}");

  const bool colon = op[0] == ':';
  const char kind = op.back();
  // Whether the parameter counts as unset (or null, with a colon).
  const exprt unset =
    colon ? exprt{or_exprt{not_exprt{is_set(parameter)}, is_empty(parameter)}}
          : exprt{not_exprt{is_set(parameter)}};

  const symbol_exprt result = temporary(string_type(), dest, loc);
  code_blockt use_word;
  const word_contextt word_context =
    context == word_contextt::IGNORED ? word_contextt::ASSIGNMENT : context;
  exprt word = expand_parts(part.parts, word_context, quoted, false, use_word);
  code_blockt use_value;
  assign(use_value, result, value_of(parameter, loc), loc);

  if(kind == '-' || kind == '=')
  {
    if(kind == '=')
    {
      if(!assignable)
        unsupported(loc, "assigning to a special or positional parameter");
      // Expansion results are always set.
      assign(use_word, parameter, word, loc);
    }
    assign(use_word, result, word, loc);
    add(
      dest,
      code_ifthenelset{unset, std::move(use_word), std::move(use_value)},
      loc);
  }
  else if(kind == '+')
  {
    assign(use_word, result, word, loc);
    code_blockt use_empty;
    assign(use_empty, result, string_literal("", loc), loc);
    add(
      dest,
      code_ifthenelset{unset, std::move(use_empty), std::move(use_word)},
      loc);
  }
  else
  {
    // '?': write the word as a message and exit, as non-interactive
    // shells do; the status differs between shells.
    nondet_status(use_word, 255, loc);
    add(
      use_word,
      code_assumet{notequal_exprt{status(), from_integer(0, status_type())}},
      loc);
    add(use_word, code_frontend_returnt{}, loc);
    add(
      dest,
      code_ifthenelset{unset, std::move(use_word), std::move(use_value)},
      loc);
  }
  return result;
}

exprt shell_convertert::expand_command_substitution(
  const shell_word_partt &part,
  code_blockt &dest)
{
  // The commands run in a subshell environment, so they cannot change
  // this shell's state; their output is not modelled yet.
  if(part.commands != nullptr && contains_verification_builtin(*part.commands))
  {
    unsupported(
      part.location,
      CPROVER_PREFIX "assert or " CPROVER_PREFIX
                     "assume in a command substitution");
  }
  expanded_command_substitution = true;
  return call(SHELL_RT_NONDET_STRING, {}, dest, part.location);
}

bool shell_convertert::has_side_effects(const shell_word_partt &part)
{
  using kindt = shell_word_partt::kindt;
  switch(part.kind)
  {
  case kindt::LITERAL:
  case kindt::QUOTED_LITERAL:
  case kindt::COMMAND_SUBSTITUTION:
    return false;
  case kindt::ARITHMETIC:
    // Assignments, and errors such as division by zero.
    return true;
  case kindt::DOUBLE_QUOTED:
    for(const auto &p : part.parts)
    {
      if(has_side_effects(p))
        return true;
    }
    return false;
  case kindt::PARAMETER:
  {
    const std::string &op = part.parameter_operator;
    if(!op.empty() && (op.back() == '=' || op.back() == '?'))
      return true;
    for(const auto &p : part.parts)
    {
      if(has_side_effects(p))
        return true;
    }
    return false;
  }
  }
  UNREACHABLE;
}

bool shell_convertert::has_side_effects(const shell_wordt &word)
{
  for(const auto &part : word)
  {
    if(has_side_effects(part))
      return true;
  }
  return false;
}

std::optional<std::string>
shell_convertert::static_text(const shell_wordt &word, bool quoted)
{
  using kindt = shell_word_partt::kindt;
  std::string result;
  for(const auto &part : word)
  {
    if(part.kind == kindt::QUOTED_LITERAL)
      result += part.text;
    else if(part.kind == kindt::LITERAL && quoted)
      result += double_quoted_literal(part.text);
    else if(part.kind == kindt::LITERAL)
    {
      for(std::size_t i = 0; i < part.text.size(); ++i)
      {
        if(part.text[i] == '\\' && i + 1 < part.text.size())
          ++i;
        result += part.text[i];
      }
    }
    else if(part.kind == kindt::DOUBLE_QUOTED)
    {
      auto inner = static_text(part.parts, true);
      if(!inner.has_value())
        return {};
      result += *inner;
    }
    else
      return {};
  }
  return result;
}

std::string shell_convertert::display(const shell_wordt &parts)
{
  using kindt = shell_word_partt::kindt;
  std::string result;
  for(const auto &part : parts)
  {
    switch(part.kind)
    {
    case kindt::LITERAL:
      result += part.text;
      break;
    case kindt::QUOTED_LITERAL:
      result += "'" + part.text + "'";
      break;
    case kindt::DOUBLE_QUOTED:
      result += "\"" + display(part.parts) + "\"";
      break;
    case kindt::PARAMETER:
      result += "${" + std::string{part.parameter_length ? "#" : ""} +
                part.text + part.parameter_operator + display(part.parts) + "}";
      break;
    case kindt::COMMAND_SUBSTITUTION:
      result += "$(...)";
      break;
    case kindt::ARITHMETIC:
      result += "$((" + part.text + "))";
      break;
    }
  }
  return result;
}

bool shell_convertert::contains_verification_builtin(
  const shell_command_listt &list)
{
  for(const auto &command : list)
  {
    if(!command.words.empty())
    {
      auto name = static_text(command.words.front());
      if(name.has_value() && name->rfind(CPROVER_PREFIX, 0) == 0)
        return true;
    }
    for(const auto *sub :
        {&command.body, &command.condition, &command.else_body})
    {
      if(contains_verification_builtin(*sub))
        return true;
    }
    for(const auto &item : command.case_items)
    {
      if(contains_verification_builtin(item.body))
        return true;
    }
  }
  return false;
}

// ------------------------------------------------------------------
// Arithmetic

exprt shell_convertert::expand_arithmetic(
  const shell_word_partt &part,
  code_blockt &dest)
{
  const source_locationt &loc = part.location;
  const auto parsed = parse_shell_arithmetic(part.text, arith_options);
  if(!parsed.expression.has_value())
  {
    log.error().source_location = loc;
    log.error() << "arithmetic expansion $((" << part.text
                << ")): " << parsed.error << messaget::eom;
    throw conversion_errort{};
  }
  if(parsed.contains_increment_sequence)
  {
    log.warning().source_location = loc;
    log.warning() << "'++' and '--' in $((" << part.text
                  << ")) are two sign operators in POSIX mode (as in dash), "
                     "but increment operators in other shells"
                  << messaget::eom;
  }
  return from_integer_expr(arith(*parsed.expression, dest, loc), dest, loc);
}

exprt shell_convertert::arith_type_cast(const exprt &value) const
{
  const typet type = arith_width() == 32 ? typet{signed_int_type()}
                                         : typet{signed_long_long_int_type()};
  return typecast_exprt::conditional_cast(value, type);
}

exprt shell_convertert::arith(
  const shell_arith_exprt &expr,
  code_blockt &dest,
  const source_locationt &loc)
{
  using kindt = shell_arith_exprt::kindt;
  const exprt zero = arith_type_cast(from_integer(0, integer_type()));
  const typet &type = zero.type();
  switch(expr.kind)
  {
  case kindt::CONSTANT:
  {
    const auto &bv_type = to_signedbv_type(type);
    if(expr.value > bv_type.largest())
    {
      log.error().source_location = loc;
      log.error() << "arithmetic constant " << integer2string(expr.value)
                  << " is out of range" << messaget::eom;
      throw conversion_errort{};
    }
    return from_integer(expr.value, type);
  }
  case kindt::VARIABLE:
    check_parameter_name(expr.name, loc);
    return arith_read(expr.name, value_of(variable(expr.name), loc), dest, loc);
  case kindt::PARAMETER:
  {
    // $x is expanded before the expression is evaluated (XCU 2.6.4); an
    // empty expansion would leave a syntax error.
    shell_word_partt part;
    part.kind = shell_word_partt::kindt::PARAMETER;
    part.text = expr.name;
    part.location = loc;
    const exprt string =
      expand_parameter(part, word_contextt::FIELD, true, dest);
    check(
      dest,
      not_exprt{is_empty(string)},
      "shell-arithmetic",
      "$" + expr.name + " is empty in arithmetic expansion",
      loc);
    return arith_read("$" + expr.name, string, dest, loc);
  }
  case kindt::UNARY:
  {
    if(expr.op == "++" || expr.op == "--")
      return arith_increment(expr, dest, loc);
    const exprt operand = arith(expr.operands[0], dest, loc);
    if(expr.op == "+")
      return operand;
    if(expr.op == "-")
    {
      arith_overflow_check(unary_minus_overflow_exprt{operand}, "-", dest, loc);
      return unary_minus_exprt{operand};
    }
    if(expr.op == "~")
      return bitnot_exprt{operand};
    return if_exprt{equal_exprt{operand, zero}, from_integer(1, type), zero};
  }
  case kindt::POSTFIX:
    return arith_increment(expr, dest, loc);
  case kindt::BINARY:
    if(expr.op == "&&" || expr.op == "||")
      return arith_logical(expr, dest, loc);
    if(expr.op == ",")
    {
      arith(expr.operands[0], dest, loc);
      return arith(expr.operands[1], dest, loc);
    }
    {
      // Operands are evaluated left to right, as in dash and bash.
      const exprt lhs = arith(expr.operands[0], dest, loc);
      const symbol_exprt lhs_value = temporary(type, dest, loc);
      assign(dest, lhs_value, lhs, loc);
      const exprt rhs = arith(expr.operands[1], dest, loc);
      return arith_binary(expr.op, lhs_value, rhs, dest, loc);
    }
  case kindt::CONDITIONAL:
    return arith_conditional(expr, dest, loc);
  case kindt::ASSIGNMENT:
    return arith_assignment(expr, dest, loc);
  }
  UNREACHABLE;
}

/// The integer value of \p string, the value of \p name.
exprt shell_convertert::arith_read(
  const std::string &name,
  const exprt &string,
  code_blockt &dest,
  const source_locationt &loc)
{
  const symbol_exprt value = temporary(integer_type(), dest, loc);
  const bool octal = arith_options.leading_zero_octal;
  // dash accepts any isspace() character around the number; bash only
  // space, tab, and newline.
  const bool any_space = program.dialect == shell_dialectt::POSIX;
  const typet &bool_type = c_bool_type();
  const symbol_exprt valid = call(
    SHELL_RT_ARITH_INTEGER,
    {string,
     from_integer(octal ? 1 : 0, bool_type),
     from_integer(any_space ? 1 : 0, bool_type),
     address_of_exprt{value}},
    dest,
    loc);
  check(
    dest,
    notequal_exprt{valid, from_integer(0, valid.type())},
    "shell-arithmetic",
    "value of " + name +
      " is an integer constant in arithmetic expansion (other values are "
      "not supported)",
    loc);
  if(arith_width() == 32)
  {
    check(
      dest,
      and_exprt{
        binary_relation_exprt{
          value, ID_ge, from_integer(-2147483648LL, integer_type())},
        binary_relation_exprt{
          value, ID_le, from_integer(2147483647LL, integer_type())}},
      "overflow",
      "value of " + name + " fits in 32-bit arithmetic",
      loc);
  }
  return arith_type_cast(value);
}

void shell_convertert::arith_store(
  const std::string &name,
  const exprt &value,
  code_blockt &dest,
  const source_locationt &loc)
{
  assign_variable(name, from_integer_expr(value, dest, loc), dest, loc);
}

void shell_convertert::arith_overflow_check(
  const exprt &overflow,
  const std::string &op,
  code_blockt &dest,
  const source_locationt &loc)
{
  check(
    dest,
    not_exprt{overflow},
    "overflow",
    "arithmetic overflow on " + op + " in arithmetic expansion",
    loc);
}

exprt shell_convertert::arith_binary(
  const std::string &op,
  const exprt &lhs,
  const exprt &rhs,
  code_blockt &dest,
  const source_locationt &loc)
{
  const typet &type = lhs.type();
  const exprt zero = from_integer(0, type);
  const exprt one = from_integer(1, type);
  auto relation = [&](const irep_idt &id) -> exprt {
    return if_exprt{binary_relation_exprt{lhs, id, rhs}, one, zero};
  };
  if(op == "+" || op == "-" || op == "*")
  {
    arith_overflow_check(
      binary_overflow_exprt{
        lhs,
        op == "+"   ? ID_plus
        : op == "-" ? ID_minus
                    : ID_mult,
        rhs},
      op,
      dest,
      loc);
    if(op == "+")
      return plus_exprt{lhs, rhs};
    if(op == "-")
      return minus_exprt{lhs, rhs};
    return mult_exprt{lhs, rhs};
  }
  if(op == "/" || op == "%")
  {
    check(
      dest,
      notequal_exprt{rhs, zero},
      "division-by-zero",
      "division by zero in arithmetic expansion",
      loc);
    const auto &bv_type = to_signedbv_type(type);
    arith_overflow_check(
      and_exprt{
        equal_exprt{lhs, from_integer(bv_type.smallest(), type)},
        equal_exprt{rhs, from_integer(-1, type)}},
      op,
      dest,
      loc);
    if(op == "/")
      return div_exprt{lhs, rhs};
    return mod_exprt{lhs, rhs};
  }
  if(op == "<<" || op == ">>")
  {
    const std::size_t width = to_signedbv_type(type).get_width();
    check(
      dest,
      and_exprt{
        binary_relation_exprt{rhs, ID_ge, zero},
        binary_relation_exprt{rhs, ID_lt, from_integer(width, type)}},
      "undefined-shift",
      "shift distance is in range in arithmetic expansion",
      loc);
    if(op == ">>")
      return ashr_exprt{lhs, rhs};
    arith_overflow_check(shl_overflow_exprt{lhs, rhs}, op, dest, loc);
    return shl_exprt{lhs, rhs};
  }
  if(op == "<")
    return relation(ID_lt);
  if(op == "<=")
    return relation(ID_le);
  if(op == ">")
    return relation(ID_gt);
  if(op == ">=")
    return relation(ID_ge);
  if(op == "==")
    return if_exprt{equal_exprt{lhs, rhs}, one, zero};
  if(op == "!=")
    return if_exprt{notequal_exprt{lhs, rhs}, one, zero};
  if(op == "&")
    return bitand_exprt{lhs, rhs};
  if(op == "^")
    return bitxor_exprt{lhs, rhs};
  if(op == "|")
    return bitor_exprt{lhs, rhs};
  UNREACHABLE;
}

/// && and ||: the right operand is only evaluated if needed.
exprt shell_convertert::arith_logical(
  const shell_arith_exprt &expr,
  code_blockt &dest,
  const source_locationt &loc)
{
  const exprt lhs = arith(expr.operands[0], dest, loc);
  const typet &type = lhs.type();
  const symbol_exprt result = temporary(type, dest, loc);
  const bool is_and = expr.op == "&&";
  assign(dest, result, from_integer(is_and ? 0 : 1, type), loc);
  code_blockt evaluate_rhs;
  const exprt rhs = arith(expr.operands[1], evaluate_rhs, loc);
  assign(
    evaluate_rhs,
    result,
    if_exprt{
      notequal_exprt{rhs, from_integer(0, type)},
      from_integer(1, type),
      from_integer(0, type)},
    loc);
  const exprt lhs_true = notequal_exprt{lhs, from_integer(0, type)};
  add(
    dest,
    code_ifthenelset{
      is_and ? lhs_true : not_exprt{lhs_true}, std::move(evaluate_rhs)},
    loc);
  return result;
}

exprt shell_convertert::arith_conditional(
  const shell_arith_exprt &expr,
  code_blockt &dest,
  const source_locationt &loc)
{
  const exprt condition = arith(expr.operands[0], dest, loc);
  const symbol_exprt result = temporary(condition.type(), dest, loc);
  code_blockt then_block, else_block;
  assign(then_block, result, arith(expr.operands[1], then_block, loc), loc);
  assign(else_block, result, arith(expr.operands[2], else_block, loc), loc);
  add(
    dest,
    code_ifthenelset{
      notequal_exprt{condition, from_integer(0, condition.type())},
      std::move(then_block),
      std::move(else_block)},
    loc);
  return result;
}

exprt shell_convertert::arith_assignment(
  const shell_arith_exprt &expr,
  code_blockt &dest,
  const source_locationt &loc)
{
  check_parameter_name(expr.name, loc);
  const exprt rhs = arith(expr.operands[0], dest, loc);
  const symbol_exprt value = temporary(rhs.type(), dest, loc);
  if(expr.op == "=")
    assign(dest, value, rhs, loc);
  else
  {
    const symbol_exprt rhs_value = temporary(rhs.type(), dest, loc);
    assign(dest, rhs_value, rhs, loc);
    const exprt current =
      arith_read(expr.name, value_of(variable(expr.name), loc), dest, loc);
    const std::string op = expr.op.substr(0, expr.op.size() - 1);
    assign(dest, value, arith_binary(op, current, rhs_value, dest, loc), loc);
  }
  arith_store(expr.name, value, dest, loc);
  return value;
}

exprt shell_convertert::arith_increment(
  const shell_arith_exprt &expr,
  code_blockt &dest,
  const source_locationt &loc)
{
  const std::string &name = expr.operands[0].name;
  check_parameter_name(name, loc);
  const exprt current =
    arith_read(name, value_of(variable(name), loc), dest, loc);
  const symbol_exprt old_value = temporary(current.type(), dest, loc);
  assign(dest, old_value, current, loc);
  const exprt one = from_integer(1, current.type());
  const exprt new_value =
    arith_binary(expr.op == "++" ? "+" : "-", old_value, one, dest, loc);
  const symbol_exprt result = temporary(current.type(), dest, loc);
  assign(dest, result, new_value, loc);
  arith_store(name, result, dest, loc);
  return expr.kind == shell_arith_exprt::kindt::POSTFIX ? exprt{old_value}
                                                        : exprt{result};
}

// ------------------------------------------------------------------
// The test utility (XCU test)

void shell_convertert::status_from(
  const exprt &condition,
  code_blockt &dest,
  const source_locationt &loc)
{
  assign(
    dest,
    status(),
    if_exprt{
      condition,
      from_integer(0, status_type()),
      from_integer(1, status_type())},
    loc);
}

/// ! applied to a test result: 0 and 1 are swapped, errors (2) stay.
void shell_convertert::negate_status(
  code_blockt &dest,
  const source_locationt &l)
{
  assign(
    dest,
    status(),
    if_exprt{
      equal_exprt{status(), from_integer(0, status_type())},
      from_integer(1, status_type()),
      if_exprt{
        equal_exprt{status(), from_integer(1, status_type())},
        from_integer(0, status_type()),
        status()}},
    l);
}

void shell_convertert::dynamic_operator(const source_locationt &loc)
{
  unsupported(
    loc,
    "test operand that is an expansion where its value could change how "
    "the arguments are parsed (an operator such as '!' or '-n')");
}

bool shell_convertert::is_unary_primary(const std::string &op) const
{
  static const std::set<std::string> posix{
    "-b",
    "-c",
    "-d",
    "-e",
    "-f",
    "-g",
    "-h",
    "-L",
    "-n",
    "-p",
    "-r",
    "-S",
    "-s",
    "-t",
    "-u",
    "-w",
    "-x",
    "-z"};
  return posix.count(op) != 0;
}

bool shell_convertert::is_binary_primary(const std::string &op) const
{
  static const std::set<std::string> posix{
    "=",
    "!=",
    "-eq",
    "-ne",
    "-gt",
    "-ge",
    "-lt",
    "-le",
    "-ef",
    "-nt",
    "-ot",
    "<",
    ">"};
  if(posix.count(op) != 0)
    return true;
  // "==" is an extension of bash, mksh, and zsh; dash rejects it.
  return op == "==" && program.dialect != shell_dialectt::POSIX;
}

void shell_convertert::test(
  std::vector<test_argumentt> args,
  code_blockt &dest,
  const source_locationt &loc)
{
  // POSIX specifies the results by the number of arguments; with more
  // than four, results are unspecified.
  switch(args.size())
  {
  case 0:
    set_status(dest, 1, loc);
    return;
  case 1:
    test1(args[0], dest, loc);
    return;
  case 2:
    test2(args[0], args[1], dest, loc);
    return;
  case 3:
    test3(args[0], args[1], args[2], dest, loc);
    return;
  case 4:
    test4(args, dest, loc);
    return;
  default:
    unsupported(loc, "test with more than four arguments");
  }
}

void shell_convertert::test1(
  const test_argumentt &a,
  code_blockt &dest,
  const source_locationt &loc)
{
  status_from(not_exprt{is_empty(a.value)}, dest, loc);
}

void shell_convertert::test2(
  const test_argumentt &a,
  const test_argumentt &b,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(!a.text.has_value())
    dynamic_operator(loc);
  if(*a.text == "!")
  {
    status_from(is_empty(b.value), dest, loc);
    return;
  }
  if(is_unary_primary(*a.text))
  {
    unary_test(*a.text, b.value, dest, loc);
    return;
  }
  // Unspecified: shells differ.
  nondet_status(dest, 2, loc);
}

void shell_convertert::test3(
  const test_argumentt &a,
  const test_argumentt &b,
  const test_argumentt &c,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(!b.text.has_value())
    dynamic_operator(loc);
  if(is_binary_primary(*b.text))
  {
    binary_test(a.value, *b.text, c.value, dest, loc);
    return;
  }
  if(!a.text.has_value())
    dynamic_operator(loc);
  if(*a.text == "!")
  {
    test2(b, c, dest, loc);
    negate_status(dest, loc);
    return;
  }
  if(*a.text == "(" && c.text.has_value() && *c.text == ")")
  {
    test1(b, dest, loc);
    return;
  }
  nondet_status(dest, 2, loc);
}

void shell_convertert::test4(
  const std::vector<test_argumentt> &args,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(!args[0].text.has_value())
    dynamic_operator(loc);
  if(*args[0].text == "!")
  {
    test3(args[1], args[2], args[3], dest, loc);
    negate_status(dest, loc);
    return;
  }
  if(*args[0].text == "(" && args[3].text.has_value() && *args[3].text == ")")
  {
    test2(args[1], args[2], dest, loc);
    return;
  }
  nondet_status(dest, 2, loc);
}

void shell_convertert::unary_test(
  const std::string &op,
  const exprt &operand,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(op == "-n")
    status_from(not_exprt{is_empty(operand)}, dest, loc);
  else if(op == "-z")
    status_from(is_empty(operand), dest, loc);
  else
  {
    // File tests and -t: the file system and terminals are not modelled.
    nondet_status(dest, 1, loc);
  }
}

void shell_convertert::binary_test(
  const exprt &lhs,
  const std::string &op,
  const exprt &rhs,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(op == "=" || op == "==" || op == "!=")
  {
    const exprt same = equal(lhs, rhs, dest, loc);
    status_from(op == "!=" ? exprt{not_exprt{same}} : same, dest, loc);
  }
  else if(
    op.size() == 3 && op[0] == '-' && op != "-ef" && op != "-nt" && op != "-ot")
    integer_test(lhs, op, rhs, dest, loc);
  else
  {
    // -ef, -nt, -ot (files) and < > (collation order) are not modelled.
    nondet_status(dest, 1, loc);
  }
}

void shell_convertert::integer_test(
  const exprt &lhs,
  const std::string &op,
  const exprt &rhs,
  code_blockt &dest,
  const source_locationt &loc)
{
  const typet &bool_type = c_bool_type();
  // zsh rejects trailing white space; dash, bash, and mksh accept it.
  const exprt trailing =
    from_integer(program.dialect == shell_dialectt::ZSH ? 0 : 1, bool_type);
  const symbol_exprt l = temporary(integer_type(), dest, loc);
  const symbol_exprt r = temporary(integer_type(), dest, loc);
  const symbol_exprt l_valid = call(
    SHELL_RT_TEST_INTEGER, {lhs, trailing, address_of_exprt{l}}, dest, loc);
  const symbol_exprt r_valid = call(
    SHELL_RT_TEST_INTEGER, {rhs, trailing, address_of_exprt{r}}, dest, loc);
  const exprt valid = and_exprt{
    notequal_exprt{l_valid, from_integer(0, l_valid.type())},
    notequal_exprt{r_valid, from_integer(0, r_valid.type())}};

  const irep_idt relation = op == "-eq"   ? ID_equal
                            : op == "-ne" ? ID_notequal
                            : op == "-gt" ? ID_gt
                            : op == "-ge" ? ID_ge
                            : op == "-lt" ? ID_lt
                                          : ID_le;
  code_blockt compare;
  status_from(binary_relation_exprt{l, relation, r}, compare, loc);
  code_blockt invalid;
  if(
    program.dialect == shell_dialectt::POSIX ||
    program.dialect == shell_dialectt::BASH)
  {
    set_status(invalid, 2, loc);
  }
  else
  {
    // mksh and zsh evaluate other operands as arithmetic expressions.
    nondet_status(invalid, 2, loc);
  }
  add(
    dest, code_ifthenelset{valid, std::move(compare), std::move(invalid)}, loc);
}

// ------------------------------------------------------------------
// Commands

command_kindt shell_convertert::classify(const std::string &name) const
{
  static const std::map<std::string, command_kindt> builtins{
    {":", command_kindt::COLON},
    {"true", command_kindt::TRUE},
    {"false", command_kindt::FALSE},
    {"test", command_kindt::TEST},
    {"[", command_kindt::BRACKET},
    {"exit", command_kindt::EXIT},
    {"export", command_kindt::EXPORT},
    {"unset", command_kindt::UNSET},
    {"break", command_kindt::BREAK},
    {"continue", command_kindt::CONTINUE},
    {CPROVER_PREFIX "assert", command_kindt::ASSERT},
    {CPROVER_PREFIX "assume", command_kindt::ASSUME},
    {"echo", command_kindt::OUTPUT},
    {"printf", command_kindt::OUTPUT}};
  // Builtins that change the shell environment (XCU 2.15 special
  // builtins, regular builtins of XCU 1.7, and common extensions).
  static const std::set<std::string> unsupported_builtins{
    ".",       "eval",      "exec",     "readonly", "return",  "set",
    "shift",   "times",     "trap",     "cd",       "read",    "getopts",
    "alias",   "unalias",   "umask",    "wait",     "jobs",    "fg",
    "bg",      "command",   "type",     "hash",     "kill",    "ulimit",
    "fc",      "newgrp",    "local",    "declare",  "typeset", "source",
    "let",     "shopt",     "builtin",  "enable",   "mapfile", "readarray",
    "pushd",   "popd",      "dirs",     "disown",   "suspend", "logout",
    "nameref", "autoload",  "setopt",   "unsetopt", "emulate", "print",
    "whence",  "functions", "zmodload", "caller",   "compgen", "complete"};
  const auto it = builtins.find(name);
  if(it != builtins.end())
    return it->second;
  if(unsupported_builtins.count(name) != 0)
    return command_kindt::UNSUPPORTED_BUILTIN;
  if(name.rfind(CPROVER_PREFIX, 0) == 0)
    return command_kindt::UNSUPPORTED_BUILTIN;
  return command_kindt::EXTERNAL;
}

/// Reject variables that shells maintain specially, whose values and
/// assignments would need modelling.
void shell_convertert::check_parameter_name(
  const std::string &name,
  const source_locationt &loc)
{
  static const std::set<std::string> special_everywhere{
    "LINENO", "RANDOM", "SECONDS", "OPTIND", "OPTARG", "IFS", "_"};
  static const std::set<std::string> special_bash{
    "BASH_ARGC",    "BASH_ARGV",     "BASH_COMMAND", "BASH_LINENO",
    "BASH_SOURCE",  "BASH_SUBSHELL", "BASHPID",      "EPOCHREALTIME",
    "EPOCHSECONDS", "FUNCNAME",      "GROUPS",       "HISTCMD",
    "PIPESTATUS",   "SRANDOM",       "UID",          "EUID",
    "PPID",         "SHELLOPTS",     "BASHOPTS",     "BASH_REMATCH",
    "DIRSTACK",     "REPLY"};
  static const std::set<std::string> special_zsh{
    "status",
    "argv",
    "pipestatus",
    "ERRNO",
    "TTYIDLE",
    "path",
    "PATH",
    "fpath",
    "cdpath",
    "manpath",
    "REPLY",
    "reply",
    "PPID",
    "EPOCHSECONDS",
    "EPOCHREALTIME"};
  static const std::set<std::string> special_mksh{
    "KSH_VERSION",
    "BASHPID",
    "EPOCHREALTIME",
    "PPID",
    "PIPESTATUS",
    "USER_ID",
    "REPLY"};
  bool special = special_everywhere.count(name) != 0;
  if(program.dialect == shell_dialectt::BASH)
    special |= special_bash.count(name) != 0;
  else if(program.dialect == shell_dialectt::ZSH)
    special |= special_zsh.count(name) != 0;
  else if(program.dialect == shell_dialectt::MKSH)
    special |= special_mksh.count(name) != 0;
  if(special)
    unsupported(loc, "variable " + name + ", which the shell maintains");
}

void shell_convertert::assign_variable(
  const std::string &name,
  const exprt &value,
  code_blockt &dest,
  const source_locationt &loc)
{
  check_parameter_name(name, loc);
  // Expansion results are always set.
  assign(dest, variable(name), value, loc);
}

code_ifthenelset shell_convertert::if_status_zero(bool zero, code_blockt block)
{
  const exprt is_zero = equal_exprt{status(), from_integer(0, status_type())};
  return code_ifthenelset{
    zero ? is_zero : exprt{not_exprt{is_zero}}, std::move(block)};
}

void shell_convertert::goto_label(code_blockt &dest, const irep_idt &label)
{
  dest.add(code_gotot{label});
}

void shell_convertert::label(code_blockt &dest, const irep_idt &label)
{
  dest.add(code_labelt{label, code_skipt{}});
}

void shell_convertert::convert_list(
  const shell_command_listt &list,
  code_blockt &dest)
{
  for(const auto &command : list)
    convert_command(command, dest);
}

void shell_convertert::convert_command(
  const shell_commandt &command,
  code_blockt &dest)
{
  using kindt = shell_commandt::kindt;
  if(command.background)
  {
    convert_background(command, dest);
    return;
  }
  if(!command.redirects.empty() && command.kind != kindt::SIMPLE)
    unsupported(command.location, "redirection of a compound command");

  switch(command.kind)
  {
  case kindt::SIMPLE:
    convert_simple(command, dest);
    break;
  case kindt::PIPELINE:
    convert_pipeline(command, dest);
    break;
  case kindt::AND:
  case kindt::OR:
    convert_and_or(command, dest);
    break;
  case kindt::SUBSHELL:
  case kindt::BRACE_GROUP:
  case kindt::IF:
  case kindt::WHILE:
  case kindt::UNTIL:
  case kindt::FOR:
  case kindt::CASE:
  case kindt::FUNCTION_DEFINITION:
    convert_compound(command, dest);
    break;
  }
  if(command.negated)
  {
    // ! pipeline: 0 becomes 1, any other status becomes 0 (XCU 2.9.2).
    assign(
      dest,
      status(),
      if_exprt{
        equal_exprt{status(), from_integer(0, status_type())},
        from_integer(1, status_type()),
        from_integer(0, status_type())},
      command.location);
  }
}

void shell_convertert::convert_compound(
  const shell_commandt &command,
  code_blockt &dest)
{
  using kindt = shell_commandt::kindt;
  switch(command.kind)
  {
  case kindt::BRACE_GROUP:
    convert_list(command.body, dest);
    return;
  case kindt::IF:
    convert_if(command, dest);
    return;
  case kindt::WHILE:
  case kindt::UNTIL:
    convert_while(command, dest);
    return;
  case kindt::FOR:
    convert_for(command, dest);
    return;
  case kindt::CASE:
    convert_case(command, dest);
    return;
  case kindt::SUBSHELL:
    unsupported(command.location, "subshell ( ... )");
  case kindt::FUNCTION_DEFINITION:
    unsupported(command.location, "function definition");
  case kindt::SIMPLE:
  case kindt::PIPELINE:
  case kindt::AND:
  case kindt::OR:
    UNREACHABLE;
  }
}

void shell_convertert::convert_and_or(
  const shell_commandt &command,
  code_blockt &dest)
{
  convert_command(command.body[0], dest);
  code_blockt rhs;
  convert_command(command.body[1], rhs);
  add(
    dest,
    if_status_zero(command.kind == shell_commandt::kindt::AND, std::move(rhs)),
    command.location);
}

void shell_convertert::convert_if(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  convert_list(command.condition, dest);
  code_blockt then_block;
  convert_list(command.body, then_block);
  code_blockt else_block;
  if(command.else_body.empty())
  {
    // With no branch executed, the status is zero (XCU 2.9.4.4).
    set_status(else_block, 0, loc);
  }
  else
    convert_list(command.else_body, else_block);
  add(
    dest,
    code_ifthenelset{
      equal_exprt{status(), from_integer(0, status_type())},
      std::move(then_block),
      std::move(else_block)},
    loc);
}

/// while and until loops (XCU 2.9.4.5, 2.9.4.6) as gotos, so that break
/// and continue with a count can leave several loops.
void shell_convertert::convert_while(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  const bool is_until = command.kind == shell_commandt::kindt::UNTIL;
  const irep_idt break_label = new_label("break");
  const irep_idt continue_label = new_label("continue");
  const symbol_exprt loop_status = temporary(status_type(), dest, loc);
  assign(dest, loop_status, from_integer(0, status_type()), loc);

  label(dest, continue_label);
  convert_list(command.condition, dest);
  code_blockt leave;
  // The status is that of the last body executed, or zero.
  assign(leave, status(), loop_status, loc);
  goto_label(leave, break_label);
  add(dest, if_status_zero(is_until, std::move(leave)), loc);

  loops.push_back({break_label, continue_label, loop_status});
  convert_list(command.body, dest);
  loops.pop_back();
  assign(dest, loop_status, status(), loc);
  goto_label(dest, continue_label);
  label(dest, break_label);
}

void shell_convertert::convert_for(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  if(!command.has_in)
    unsupported(loc, "for loop over the positional parameters");
  check_parameter_name(command.name, loc);

  // The words are expanded once, before the first iteration
  // (XCU 2.9.4.2); each yields one field, so the iterations are known.
  std::vector<exprt> items;
  for(const auto &word : command.for_items)
  {
    const symbol_exprt item = temporary(string_type(), dest, loc);
    assign(dest, item, expand_word(word, word_contextt::FIELD, dest, loc), loc);
    items.push_back(item);
  }

  const irep_idt break_label = new_label("break");
  const symbol_exprt loop_status = temporary(status_type(), dest, loc);
  assign(dest, loop_status, from_integer(0, status_type()), loc);
  for(const auto &item : items)
  {
    const irep_idt continue_label = new_label("continue");
    assign_variable(command.name, item, dest, loc);
    loops.push_back({break_label, continue_label, loop_status});
    convert_list(command.body, dest);
    loops.pop_back();
    assign(dest, loop_status, status(), loc);
    label(dest, continue_label);
  }
  assign(dest, status(), loop_status, loc);
  label(dest, break_label);
}

void shell_convertert::convert_break_continue(
  const shell_commandt &command,
  bool is_break,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  std::size_t n = 1;
  if(command.words.size() > 2)
    unsupported(loc, "break or continue with several operands");
  if(command.words.size() == 2)
  {
    auto text = static_text(command.words[1]);
    if(
      !text.has_value() || text->empty() ||
      text->find_first_not_of("0123456789") != std::string::npos ||
      std::stoul(*text) == 0)
    {
      unsupported(
        loc,
        "break or continue operand other than a positive "
        "decimal number");
    }
    n = std::stoul(*text);
  }
  if(loops.empty())
    unsupported(loc, "break or continue outside a loop");
  // A count larger than the number of loops leaves the outermost one.
  const loop_labelst &target = loops[loops.size() - std::min(n, loops.size())];
  set_status(dest, 0, loc);
  if(is_break)
    goto_label(dest, target.break_label);
  else
  {
    assign(dest, target.loop_status, status(), loc);
    goto_label(dest, target.continue_label);
  }
}

/// case (XCU 2.9.4.3): the patterns are tried in order, and ";&" falls
/// through to the next item's commands.
void shell_convertert::convert_case(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  const symbol_exprt subject = temporary(string_type(), dest, loc);
  assign(
    dest,
    subject,
    expand_word(command.case_word, word_contextt::CASE_SUBJECT, dest, loc),
    loc);

  const irep_idt end_label = new_label("esac");
  std::vector<irep_idt> body_labels;
  for(const auto &item : command.case_items)
  {
    body_labels.push_back(new_label("case_body"));
    for(const auto &pattern : item.patterns)
    {
      const exprt matches =
        case_pattern_matches(pattern, subject, dest, item.location);
      code_blockt jump;
      goto_label(jump, body_labels.back());
      add(dest, code_ifthenelset{matches, std::move(jump)}, item.location);
    }
  }
  // No pattern matched: the status is zero.
  set_status(dest, 0, loc);
  goto_label(dest, end_label);

  for(std::size_t i = 0; i < command.case_items.size(); ++i)
  {
    const auto &item = command.case_items[i];
    label(dest, body_labels[i]);
    set_status(dest, 0, item.location);
    convert_list(item.body, dest);
    if(item.terminator == ";;" || i + 1 == command.case_items.size())
      goto_label(dest, end_label);
  }
  label(dest, end_label);
}

/// Whether \p pattern matches; only patterns without unquoted pattern
/// characters, and the pattern "*", are supported.
exprt shell_convertert::case_pattern_matches(
  const shell_wordt &pattern,
  const exprt &subject,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(
    pattern.size() == 1 &&
    pattern[0].kind == shell_word_partt::kindt::LITERAL &&
    pattern[0].text == "*")
  {
    return true_exprt{};
  }
  const exprt text = expand_word(pattern, word_contextt::FIELD, dest, loc);
  return equal(subject, text, dest, loc);
}

/// Redirections that cannot fail or change the shell's state: duplicating
/// the standard descriptors and /dev/null, assuming that descriptors 0,
/// 1, and 2 are open and that /dev/null exists.
static bool is_harmless_redirection(const shell_redirectt &redirect)
{
  const auto target = [&]() -> std::optional<std::string>
  {
    std::string text;
    for(const auto &part : redirect.target)
    {
      if(
        part.kind != shell_word_partt::kindt::LITERAL &&
        part.kind != shell_word_partt::kindt::QUOTED_LITERAL)
      {
        return {};
      }
      text += part.text;
    }
    return text;
  }();
  if(!target.has_value())
    return false;
  if(redirect.op == ">&" || redirect.op == "<&")
    return *target == "0" || *target == "1" || *target == "2" || *target == "-";
  return (redirect.op == ">" || redirect.op == ">>" || redirect.op == "<" ||
          redirect.op == ">|") &&
         *target == "/dev/null";
}

void shell_convertert::check_redirections(
  const shell_commandt &command,
  command_kindt kind)
{
  for(const auto &redirect : command.redirects)
  {
    if(
      has_side_effects(redirect.target) ||
      has_side_effects(redirect.here_document))
    {
      // Shells differ in whether these expansions happen in the shell or
      // in the child process.
      unsupported(
        redirect.location, "redirection with an expansion with side effects");
    }
    if(
      kind != command_kindt::EXTERNAL && kind != command_kindt::OUTPUT &&
      !is_harmless_redirection(redirect))
    {
      // A failed redirection changes the status of builtins, and makes
      // the shell exit for special builtins (XCU 2.8.1).
      unsupported(
        redirect.location, "redirection of a builtin that could fail");
    }
  }
}

/// Commands in pipelines and asynchronous lists run in subshells: they
/// must not have effects that would wrongly be applied to this shell.
void shell_convertert::check_pure_command(const shell_commandt &command)
{
  if(command.kind != shell_commandt::kindt::SIMPLE || command.words.empty())
    unsupported(
      command.location,
      "pipeline or & with a command other than a "
      "simple command");
  auto name = static_text(command.words.front());
  if(!name.has_value())
    unsupported(command.location, "command name that is an expansion");
  const command_kindt kind = classify(*name);
  if(
    kind != command_kindt::EXTERNAL && kind != command_kindt::OUTPUT &&
    kind != command_kindt::TEST && kind != command_kindt::BRACKET &&
    kind != command_kindt::TRUE && kind != command_kindt::FALSE &&
    kind != command_kindt::COLON)
  {
    unsupported(command.location, "'" + *name + "' in a pipeline or with &");
  }
  for(const auto &word : command.words)
  {
    if(has_side_effects(word))
      unsupported(
        command.location,
        "expansion with side effects in a "
        "pipeline or with &");
  }
  for(const auto &assignment : command.assignments)
  {
    if(has_side_effects(assignment.value))
      unsupported(
        command.location,
        "expansion with side effects in a "
        "pipeline or with &");
  }
  check_redirections(command, kind);
}

/// The status of a pipeline is that of its last command (XCU 2.9.2); each
/// command runs in a subshell environment.
void shell_convertert::convert_pipeline(
  const shell_commandt &command,
  code_blockt &dest)
{
  for(const auto &element : command.body)
  {
    if(element.negated || element.background)
      unsupported(element.location, "! or & inside a pipeline");
    check_pure_command(element);
  }
  // The commands have no effects on this shell, so only the last one's
  // status is computed.
  shell_commandt last = command.body.back();
  last.redirects.clear();
  last.assignments.clear();
  convert_simple(last, dest);
}

void shell_convertert::convert_background(
  const shell_commandt &command,
  code_blockt &dest)
{
  if(command.kind == shell_commandt::kindt::PIPELINE)
  {
    for(const auto &element : command.body)
      check_pure_command(element);
  }
  else
    check_pure_command(command);
  // The status of an asynchronous list is zero (XCU 2.9.3.1).
  set_status(dest, 0, command.location);
}

void shell_convertert::convert_assignments_only(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  if(!command.redirects.empty())
    unsupported(loc, "redirection without a command name");
  expanded_command_substitution = false;
  for(const auto &assignment : command.assignments)
  {
    const exprt value = expand_word(
      assignment.value, word_contextt::ASSIGNMENT, dest, assignment.location);
    assign_variable(assignment.name, value, dest, assignment.location);
  }
  // The status is that of the last command substitution, or zero
  // (XCU 2.9.1.3).
  if(expanded_command_substitution)
    nondet_status(dest, 255, loc);
  else
    set_status(dest, 0, loc);
}

void shell_convertert::convert_simple(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  if(command.words.empty())
  {
    convert_assignments_only(command, dest);
    return;
  }
  auto name = static_text(command.words.front());
  if(!name.has_value())
    unsupported(loc, "command name that is an expansion");
  const command_kindt kind = classify(*name);
  if(kind == command_kindt::UNSUPPORTED_BUILTIN)
    unsupported(loc, "builtin '" + *name + "'");
  check_redirections(command, kind);
  convert_builtin(kind, command, dest);
}

void shell_convertert::convert_builtin(
  command_kindt kind,
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  // Special builtins keep the assignments before them (XCU 2.9.1.2).
  const bool special =
    kind == command_kindt::COLON || kind == command_kindt::EXIT ||
    kind == command_kindt::EXPORT || kind == command_kindt::UNSET ||
    kind == command_kindt::BREAK || kind == command_kindt::CONTINUE;
  if(
    !command.assignments.empty() && kind != command_kindt::EXTERNAL &&
    kind != command_kindt::OUTPUT && !special)
  {
    // Shells differ in whether these assignments persist.
    unsupported(loc, "variable assignment before a regular builtin");
  }

  switch(kind)
  {
  case command_kindt::TEST:
  case command_kindt::BRACKET:
    convert_test_command(command, kind == command_kindt::BRACKET, dest);
    return;
  case command_kindt::ASSERT:
  case command_kindt::ASSUME:
    convert_verification_command(command, kind == command_kindt::ASSERT, dest);
    return;
  case command_kindt::EXIT:
    convert_exit(command, dest);
    return;
  case command_kindt::EXPORT:
    convert_export(command, dest);
    return;
  case command_kindt::UNSET:
    convert_unset(command, dest);
    return;
  case command_kindt::BREAK:
  case command_kindt::CONTINUE:
    convert_break_continue(command, kind == command_kindt::BREAK, dest);
    return;
  case command_kindt::COLON:
  case command_kindt::TRUE:
  case command_kindt::FALSE:
  case command_kindt::OUTPUT:
  case command_kindt::EXTERNAL:
    break;
  case command_kindt::UNSUPPORTED_BUILTIN:
    UNREACHABLE;
  }

  // The remaining commands only depend on their arguments for output.
  for(std::size_t i = 1; i < command.words.size(); ++i)
    expand_word(command.words[i], word_contextt::IGNORED, dest, loc);
  for(const auto &assignment : command.assignments)
  {
    if(special)
    {
      const exprt value = expand_word(
        assignment.value, word_contextt::ASSIGNMENT, dest, assignment.location);
      assign_variable(assignment.name, value, dest, assignment.location);
    }
    else
    {
      // In the environment of the command only.
      expand_word(assignment.value, word_contextt::IGNORED, dest, loc);
    }
  }
  if(kind == command_kindt::COLON || kind == command_kindt::TRUE)
    set_status(dest, 0, loc);
  else if(kind == command_kindt::FALSE)
    set_status(dest, 1, loc);
  else
    nondet_status(dest, 255, loc);
}

void shell_convertert::convert_test_command(
  const shell_commandt &command,
  bool bracket,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  std::size_t end = command.words.size();
  if(bracket)
  {
    auto last = static_text(command.words.back());
    if(command.words.size() < 2 || !last.has_value() || *last != "]")
      unsupported(loc, "'[' without a closing ']'");
    --end;
  }
  std::vector<test_argumentt> args;
  for(std::size_t i = 1; i < end; ++i)
  {
    const auto &word = command.words[i];
    args.push_back(
      {expand_word(word, word_contextt::FIELD, dest, loc), static_text(word)});
  }
  test(std::move(args), dest, loc);
}

/// __CPROVER_assert and __CPROVER_assume take the arguments of test.
void shell_convertert::convert_verification_command(
  const shell_commandt &command,
  bool assertion,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  std::vector<test_argumentt> args;
  std::string description;
  for(std::size_t i = 1; i < command.words.size(); ++i)
  {
    const auto &word = command.words[i];
    args.push_back(
      {expand_word(word, word_contextt::FIELD, dest, loc), static_text(word)});
    description += (i > 1 ? " " : "") + display(word);
  }
  test(std::move(args), dest, loc);
  const exprt holds = equal_exprt{status(), from_integer(0, status_type())};
  if(assertion)
  {
    source_locationt assert_loc = loc;
    assert_loc.set_property_class("assertion");
    assert_loc.set_comment("assertion " + description);
    code_assertt check{holds};
    check.add_source_location() = assert_loc;
    dest.add(std::move(check));
  }
  else
    add(dest, code_assumet{holds}, loc);
  set_status(dest, 0, loc);
}

void shell_convertert::convert_exit(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  if(command.words.size() > 2)
    unsupported(loc, "exit with several operands");
  if(command.words.size() == 2)
  {
    // The value is only used for the status, which nothing observes yet.
    auto text = static_text(command.words[1]);
    if(
      text.has_value() && !text->empty() && text->size() < 10 &&
      text->find_first_not_of("0123456789") == std::string::npos)
    {
      set_status(dest, std::stoul(*text) % 256, loc);
    }
    else
    {
      expand_word(command.words[1], word_contextt::IGNORED, dest, loc);
      nondet_status(dest, 255, loc);
    }
  }
  add(dest, code_frontend_returnt{}, loc);
}

void shell_convertert::convert_export(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  for(const auto &assignment : command.assignments)
  {
    const exprt value = expand_word(
      assignment.value, word_contextt::ASSIGNMENT, dest, assignment.location);
    assign_variable(assignment.name, value, dest, assignment.location);
  }
  for(std::size_t i = 1; i < command.words.size(); ++i)
  {
    const shell_wordt &word = command.words[i];
    // NAME or NAME=value, where the name is literal text.
    if(word.empty() || word[0].kind != shell_word_partt::kindt::LITERAL)
      unsupported(loc, "export operand that is not NAME or NAME=value");
    const std::string &first = word[0].text;
    const std::size_t equals = first.find('=');
    const std::string name = first.substr(0, equals);
    if(
      name.empty() || name[0] == '-' ||
      name.find_first_not_of("abcdefghijklmnopqrstuvwxyz"
                             "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
        std::string::npos)
    {
      unsupported(loc, "export operand that is not NAME or NAME=value");
    }
    if(equals == std::string::npos)
    {
      if(word.size() != 1)
        unsupported(loc, "export operand that is not NAME or NAME=value");
      // Exporting only matters to child processes, which are not
      // modelled.
      check_parameter_name(name, loc);
      continue;
    }
    shell_wordt value_word = word;
    value_word[0].text = first.substr(equals + 1);
    const exprt value =
      expand_word(value_word, word_contextt::ASSIGNMENT, dest, loc);
    assign_variable(name, value, dest, loc);
  }
  set_status(dest, 0, loc);
}

void shell_convertert::convert_unset(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  std::size_t first = 1;
  if(command.words.size() > 1)
  {
    auto option = static_text(command.words[1]);
    if(option.has_value() && *option == "-v")
      first = 2;
    else if(option.has_value() && !option->empty() && (*option)[0] == '-')
      unsupported(loc, "unset option " + *option);
  }
  for(std::size_t i = first; i < command.words.size(); ++i)
  {
    auto name = static_text(command.words[i]);
    if(!name.has_value())
      unsupported(loc, "unset operand that is an expansion");
    check_parameter_name(*name, loc);
    const symbol_exprt var = variable(*name);
    assign(dest, var, string_literal("", loc), loc);
    assign(dest, set_of(var), from_integer(0, component_type("set")), loc);
  }
  set_status(dest, 0, loc);
}

// ------------------------------------------------------------------
// Initial state

void shell_convertert::add_main(code_blockt body)
{
  symbolt main{
    SHELL_MAIN_FUNCTION, code_typet{{}, empty_typet{}}, shell_mode()};
  main.base_name = "main";
  main.pretty_name = program.filename;
  main.is_lvalue = true;
  main.location.set_file(program.filename);
  main.value = std::move(body);
  symbol_table.add(main);
}

/// __CPROVER_initialize: the state in which the script starts.
void shell_convertert::add_initialize()
{
  source_locationt loc;
  loc.set_file(program.filename);
  code_blockt init;
  init.add(code_labelt{CPROVER_PREFIX "HIDE", code_skipt{}});

  set_status(init, 0, loc);
  // The number of arguments is bounded by ARG_MAX in practice.
  assign(
    init, argument_count(), nondet(unsigned_long_long_int_type(), loc), loc);
  add(
    init,
    code_assumet{binary_relation_exprt{
      argument_count(),
      ID_le,
      from_integer(2147483647, unsigned_long_long_int_type())}},
    loc);

  // Variables not yet assigned are inherited from the environment.
  for(const auto &name : variables)
  {
    const symbol_exprt var = variable(name);
    assign(init, var, call(SHELL_RT_ENVIRONMENT, {}, init, loc), loc);
  }
  // $1, $2, ... are set if there are that many arguments.
  for(const std::size_t n : positional_parameters)
  {
    const symbol_exprt arg = positional(n);
    assign(init, arg, call(SHELL_RT_NONDET_STRING, {}, init, loc), loc);
    code_blockt unset;
    assign(unset, set_of(arg), from_integer(0, component_type("set")), loc);
    assign(
      unset, length_of(arg), from_integer(0, component_type("length")), loc);
    add(
      init,
      code_ifthenelset{
        binary_relation_exprt{
          argument_count(),
          ID_lt,
          from_integer(n, unsigned_long_long_int_type())},
        std::move(unset)},
      loc);
  }
  // $0, $$, and $- are set; their values depend on how the shell was run.
  for(const auto &name : special_parameters)
  {
    const symbol_exprt param = special(name);
    assign(init, param, call(SHELL_RT_NONDET_STRING, {}, init, loc), loc);
  }

  const std::string init_name = CPROVER_PREFIX "initialize";
  if(symbol_table.has_symbol(init_name))
    symbol_table.remove(init_name);
  symbolt init_symbol{init_name, code_typet{{}, empty_typet{}}, shell_mode()};
  init_symbol.base_name = init_name;
  init_symbol.pretty_name = init_name;
  init_symbol.is_lvalue = true;
  init_symbol.value = std::move(init);
  symbol_table.add(init_symbol);
}

void shell_convertert::operator()()
{
  code_blockt body;
  convert_list(program.commands, body);
  add_main(std::move(body));
  add_initialize();
}
} // namespace

bool shell_convert(
  const shell_programt &program,
  std::size_t max_string_length,
  symbol_table_baset &symbol_table,
  message_handlert &message_handler)
{
  if(add_shell_runtime(max_string_length, symbol_table, message_handler))
    return true;
  try
  {
    shell_convertert{
      program, max_string_length, symbol_table, message_handler}();
  }
  catch(const conversion_errort &)
  {
    return true;
  }
  return false;
}
