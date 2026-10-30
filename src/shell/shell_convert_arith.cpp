/// \file
/// Shell converter: arithmetic expansion (POSIX.1-2024 XCU 2.6.4).

#include "shell_convert_class.h"

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
    throw shell_conversion_errort{};
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
  const typet type = native()              ? integer_type()
                     : arith_width() == 32 ? typet{signed_int_type()}
                                           : typet{signed_long_long_int_type()};
  return typecast_exprt::conditional_cast(value, type);
}

exprt shell_convertert::arith_in_range(const exprt &value) const
{
  return and_exprt{
    binary_relation_exprt{
      value, ID_ge, from_integer(arith_min(), value.type())},
    binary_relation_exprt{
      value, ID_le, from_integer(arith_max(), value.type())}};
}

exprt shell_convertert::arith_bitwise(
  const std::function<exprt(const exprt &, const exprt &)> &operation,
  const exprt &lhs,
  const exprt &rhs) const
{
  if(!native())
    return operation(lhs, rhs);
  // Mathematical integers in range: compute on bit-vectors of the width
  // of arithmetic.
  const signedbv_typet bv_type{arith_width()};
  return typecast_exprt{
    operation(typecast_exprt{lhs, bv_type}, typecast_exprt{rhs, bv_type}),
    lhs.type()};
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
    if(expr.value > arith_max())
    {
      log.error().source_location = loc;
      log.error() << "arithmetic constant " << integer2string(expr.value)
                  << " is out of range" << messaget::eom;
      throw shell_conversion_errort{};
    }
    return from_integer(expr.value, type);
  }
  case kindt::VARIABLE:
    check_parameter_name(expr.name, loc);
    // bash and zsh apply -u to names in arithmetic; dash and mksh do not.
    if(
      program.dialect == shell_dialectt::BASH ||
      program.dialect == shell_dialectt::ZSH)
    {
      check_nounset(variable(expr.name), expr.name, dest, loc);
    }
    return arith_read(expr.name, value_of(variable(expr.name), loc), dest, loc);
  case kindt::PARAMETER:
  {
    // $x is expanded before the expression is evaluated (XCU 2.6.4); an
    // empty expansion would leave a syntax error.
    shell_word_partt part;
    part.kind = shell_word_partt::kindt::PARAMETER;
    part.text = expr.name;
    part.location = loc;
    if(!expr.op.empty())
    {
      // The expression is expanded as if in double quotes (XCU 2.6.4),
      // and the parser only accepts literal words.
      part.parameter_operator = expr.op;
      shell_word_partt word;
      word.kind = shell_word_partt::kindt::QUOTED_LITERAL;
      word.text = expr.word;
      word.location = loc;
      part.parts.push_back(std::move(word));
    }
    const exprt string =
      expand_parameter(part, shell_word_contextt::FIELD, true, dest);
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
      arith_overflow_check(
        native() ? exprt{not_exprt{arith_in_range(unary_minus_exprt{operand})}}
                 : exprt{unary_minus_overflow_exprt{operand}},
        "-",
        dest,
        loc);
      return unary_minus_exprt{operand};
    }
    if(expr.op == "~")
    {
      return arith_bitwise(
        [](const exprt &a, const exprt &) { return bitnot_exprt{a}; },
        operand,
        operand);
    }
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
  const exprt valid = arith_integer(string, octal, any_space, value, dest, loc);
  check(
    dest,
    valid,
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
    const exprt result = op == "+"   ? exprt{plus_exprt{lhs, rhs}}
                         : op == "-" ? exprt{minus_exprt{lhs, rhs}}
                                     : exprt{mult_exprt{lhs, rhs}};
    arith_overflow_check(
      native() ? exprt{not_exprt{arith_in_range(result)}}
               : exprt{binary_overflow_exprt{
                   lhs,
                   op == "+"   ? ID_plus
                   : op == "-" ? ID_minus
                               : ID_mult,
                   rhs}},
      op,
      dest,
      loc);
    return result;
  }
  if(op == "/" || op == "%")
  {
    check(
      dest,
      notequal_exprt{rhs, zero},
      "division-by-zero",
      "division by zero in arithmetic expansion",
      loc);
    arith_overflow_check(
      and_exprt{
        equal_exprt{lhs, from_integer(arith_min(), type)},
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
    const std::size_t width = arith_width();
    check(
      dest,
      and_exprt{
        binary_relation_exprt{rhs, ID_ge, zero},
        binary_relation_exprt{rhs, ID_lt, from_integer(width, type)}},
      "undefined-shift",
      "shift distance is in range in arithmetic expansion",
      loc);
    if(op == ">>")
    {
      return arith_bitwise(
        [](const exprt &a, const exprt &b) {
          return ashr_exprt{a, b};
        },
        lhs,
        rhs);
    }
    if(native())
    {
      const signedbv_typet bv_type{width};
      arith_overflow_check(
        shl_overflow_exprt{
          typecast_exprt{lhs, bv_type}, typecast_exprt{rhs, bv_type}},
        op,
        dest,
        loc);
    }
    else
      arith_overflow_check(shl_overflow_exprt{lhs, rhs}, op, dest, loc);
    return arith_bitwise(
      [](const exprt &a, const exprt &b) {
        return shl_exprt{a, b};
      },
      lhs,
      rhs);
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
  {
    return arith_bitwise(
      [](const exprt &a, const exprt &b) {
        return bitand_exprt{a, b};
      },
      lhs,
      rhs);
  }
  if(op == "^")
  {
    return arith_bitwise(
      [](const exprt &a, const exprt &b) {
        return bitxor_exprt{a, b};
      },
      lhs,
      rhs);
  }
  if(op == "|")
  {
    return arith_bitwise(
      [](const exprt &a, const exprt &b) {
        return bitor_exprt{a, b};
      },
      lhs,
      rhs);
  }
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
