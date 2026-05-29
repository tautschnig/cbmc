/*******************************************************************\

Module: JML Expression Parser

Author: Kiro (AI agent)

\*******************************************************************/

/// \file
/// Recursive-descent parser for JML expressions → CBMC exprt trees.

#include "jml_parser.h"

// Suppress -Wswitch-enum: the JML token enum has many values and
// our switches intentionally use default: for unhandled cases.
#pragma GCC diagnostic ignored "-Wswitch-enum"

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/cprover_prefix.h>
#include <util/ieee_float.h>
#include <util/mathematical_expr.h>
#include <util/std_expr.h>

#include <ansi-c/c_expr.h>

#include "jml_ids.h"
#include "jml_tokenizer.h"

#include <functional>
#include <sstream>

namespace
{

/// Internal parser state.
class jml_parser_statet
{
public:
  jml_parser_statet(
    std::vector<jml_tokent> tokens,
    const irep_idt &method_id,
    const irep_idt &class_id,
    const symbol_table_baset &symbol_table)
    : tokens_{std::move(tokens)},
      pos_{0},
      method_id_{method_id},
      class_id_{class_id},
      symbol_table_{symbol_table}
  {
  }

  jml_parse_resultt parse_expression();

private:
  std::vector<jml_tokent> tokens_;
  std::size_t pos_;
  const irep_idt &method_id_;
  const irep_idt &class_id_;
  const symbol_table_baset &symbol_table_;

  // Token access
  const jml_tokent &current() const { return tokens_[pos_]; }
  jml_token_kindt peek() const { return tokens_[pos_].kind; }
  const jml_tokent &advance()
  {
    const auto &tok = tokens_[pos_];
    if(pos_ + 1 < tokens_.size())
      ++pos_;
    return tok;
  }
  bool match(jml_token_kindt kind)
  {
    if(peek() == kind)
    {
      advance();
      return true;
    }
    return false;
  }
  bool expect(jml_token_kindt kind, const std::string &context)
  {
    if(!match(kind))
    {
      error("expected " + context + ", got '" + current().text + "'");
      return false;
    }
    return true;
  }

  // Error handling
  bool has_error_ = false;
  std::string error_message_;
  std::size_t error_position_ = 0;

  void error(const std::string &msg)
  {
    if(!has_error_)
    {
      has_error_ = true;
      error_message_ = msg;
      error_position_ = current().position;
    }
  }

  // Grammar productions (each returns exprt; nil_exprt on error)
  exprt expr();
  exprt equiv_expr();
  exprt implies_expr();
  exprt or_expr();
  exprt and_expr();
  exprt bitor_expr();
  exprt xor_expr();
  exprt bitand_expr();
  exprt equality_expr();
  exprt relational_expr();
  exprt additive_expr();
  exprt multiplicative_expr();
  exprt unary_expr();
  exprt postfix_expr();
  exprt primary();

  // Helpers
  exprt parse_quantifier(bool is_forall);
  exprt parse_old();
  exprt parse_fresh();
  typet parse_type();
};

// ─── Grammar productions ────────────────────────────────────────

exprt jml_parser_statet::expr()
{
  return equiv_expr();
}

exprt jml_parser_statet::equiv_expr()
{
  exprt left = implies_expr();
  if(has_error_)
    return nil_exprt();
  while(peek() == jml_token_kindt::EQUIV ||
        peek() == jml_token_kindt::NOT_EQUIV)
  {
    const bool is_equiv = (peek() == jml_token_kindt::EQUIV);
    advance();
    exprt right = implies_expr();
    if(has_error_)
      return nil_exprt();
    if(is_equiv)
      left = binary_relation_exprt(left, ID_equal, right);
    else
      left = binary_relation_exprt(left, ID_notequal, right);
  }
  return left;
}

exprt jml_parser_statet::implies_expr()
{
  exprt left = or_expr();
  if(has_error_)
    return nil_exprt();
  if(peek() == jml_token_kindt::IMPLIES)
  {
    advance();
    // Right-associative: a ==> b ==> c  is  a ==> (b ==> c)
    exprt right = implies_expr();
    if(has_error_)
      return nil_exprt();
    // a ==> b  is  !a || b
    return or_exprt(not_exprt(left), right);
  }
  return left;
}

exprt jml_parser_statet::or_expr()
{
  exprt left = and_expr();
  if(has_error_)
    return nil_exprt();
  while(peek() == jml_token_kindt::OR)
  {
    advance();
    exprt right = and_expr();
    if(has_error_)
      return nil_exprt();
    left = or_exprt(left, right);
  }
  return left;
}

exprt jml_parser_statet::and_expr()
{
  exprt left = bitor_expr();
  if(has_error_)
    return nil_exprt();
  while(peek() == jml_token_kindt::AND)
  {
    advance();
    exprt right = bitor_expr();
    if(has_error_)
      return nil_exprt();
    left = and_exprt(left, right);
  }
  return left;
}

exprt jml_parser_statet::bitor_expr()
{
  exprt left = xor_expr();
  if(has_error_)
    return nil_exprt();
  while(peek() == jml_token_kindt::PIPE)
  {
    advance();
    exprt right = xor_expr();
    if(has_error_)
      return nil_exprt();
    left = bitor_exprt(left, right);
  }
  return left;
}

exprt jml_parser_statet::xor_expr()
{
  exprt left = bitand_expr();
  if(has_error_)
    return nil_exprt();
  while(peek() == jml_token_kindt::CARET)
  {
    advance();
    exprt right = bitand_expr();
    if(has_error_)
      return nil_exprt();
    left = bitxor_exprt(left, right);
  }
  return left;
}

exprt jml_parser_statet::bitand_expr()
{
  exprt left = equality_expr();
  if(has_error_)
    return nil_exprt();
  while(peek() == jml_token_kindt::AMPERSAND)
  {
    advance();
    exprt right = equality_expr();
    if(has_error_)
      return nil_exprt();
    left = bitand_exprt(left, right);
  }
  return left;
}

exprt jml_parser_statet::equality_expr()
{
  exprt left = relational_expr();
  if(has_error_)
    return nil_exprt();
  while(peek() == jml_token_kindt::EQ || peek() == jml_token_kindt::NE)
  {
    const bool is_eq = (peek() == jml_token_kindt::EQ);
    advance();
    exprt right = relational_expr();
    if(has_error_)
      return nil_exprt();
    if(is_eq)
      left = binary_relation_exprt(left, ID_equal, right);
    else
      left = binary_relation_exprt(left, ID_notequal, right);
  }
  return left;
}

exprt jml_parser_statet::relational_expr()
{
  exprt left = additive_expr();
  if(has_error_)
    return nil_exprt();
  switch(peek())
  {
  case jml_token_kindt::LT:
    advance();
    {
      exprt right = additive_expr();
      if(has_error_)
        return nil_exprt();
      return binary_relation_exprt(left, ID_lt, right);
    }
  case jml_token_kindt::GT:
    advance();
    {
      exprt right = additive_expr();
      if(has_error_)
        return nil_exprt();
      return binary_relation_exprt(left, ID_gt, right);
    }
  case jml_token_kindt::LE:
    advance();
    {
      exprt right = additive_expr();
      if(has_error_)
        return nil_exprt();
      return binary_relation_exprt(left, ID_le, right);
    }
  case jml_token_kindt::GE:
    advance();
    {
      exprt right = additive_expr();
      if(has_error_)
        return nil_exprt();
      return binary_relation_exprt(left, ID_ge, right);
    }
  case jml_token_kindt::INSTANCEOF:
    advance();
    {
      // instanceof <type>
      typet type = parse_type();
      if(has_error_)
        return nil_exprt();
      // Produce a java_instanceof_exprt-like expression.
      // For now, use a binary expression with ID_java_instanceof.
      exprt type_expr(ID_type);
      type_expr.type() = type;
      return binary_exprt(left, ID_java_instanceof, type_expr, bool_typet());
    }
  default:
    return left;
  }
}

exprt jml_parser_statet::additive_expr()
{
  exprt left = multiplicative_expr();
  if(has_error_)
    return nil_exprt();
  while(peek() == jml_token_kindt::PLUS || peek() == jml_token_kindt::MINUS)
  {
    const bool is_plus = (peek() == jml_token_kindt::PLUS);
    advance();
    exprt right = multiplicative_expr();
    if(has_error_)
      return nil_exprt();
    if(is_plus)
      left = plus_exprt(left, right);
    else
      left = minus_exprt(left, right);
  }
  return left;
}

exprt jml_parser_statet::multiplicative_expr()
{
  exprt left = unary_expr();
  if(has_error_)
    return nil_exprt();
  while(
    peek() == jml_token_kindt::STAR || peek() == jml_token_kindt::SLASH ||
    peek() == jml_token_kindt::PERCENT)
  {
    const auto op = peek();
    advance();
    exprt right = unary_expr();
    if(has_error_)
      return nil_exprt();
    if(op == jml_token_kindt::STAR)
      left = mult_exprt(left, right);
    else if(op == jml_token_kindt::SLASH)
      left = div_exprt(left, right);
    else
      left = mod_exprt(left, right);
  }
  return left;
}

exprt jml_parser_statet::unary_expr()
{
  switch(peek())
  {
  case jml_token_kindt::BANG:
    advance();
    {
      exprt operand = unary_expr();
      if(has_error_)
        return nil_exprt();
      return not_exprt(operand);
    }
  case jml_token_kindt::TILDE:
    advance();
    {
      exprt operand = unary_expr();
      if(has_error_)
        return nil_exprt();
      return bitnot_exprt(operand);
    }
  case jml_token_kindt::MINUS:
    advance();
    {
      exprt operand = unary_expr();
      if(has_error_)
        return nil_exprt();
      return unary_minus_exprt(operand);
    }
  case jml_token_kindt::PLUS:
    advance();
    return unary_expr(); // unary + is a no-op
  default:
    return postfix_expr();
  }
}

exprt jml_parser_statet::postfix_expr()
{
  exprt left = primary();
  if(has_error_)
    return nil_exprt();

  while(true)
  {
    if(peek() == jml_token_kindt::DOT)
    {
      advance();
      if(peek() != jml_token_kindt::IDENTIFIER)
      {
        error("expected identifier after '.'");
        return nil_exprt();
      }
      const std::string field_name = current().text;
      advance();

      // Method call: obj.method(args)
      if(peek() == jml_token_kindt::LPAREN)
      {
        advance();
        exprt::operandst args;
        if(peek() != jml_token_kindt::RPAREN)
        {
          args.push_back(expr());
          if(has_error_)
            return nil_exprt();
          while(match(jml_token_kindt::COMMA))
          {
            args.push_back(expr());
            if(has_error_)
              return nil_exprt();
          }
        }
        if(!expect(jml_token_kindt::RPAREN, "')'"))
          return nil_exprt();
        // Represent as a function_application_exprt or a
        // generic "method call" expression. For now, use a
        // side_effect_expr_function_callt-like shape.
        // We'll refine this during type resolution in Phase 3.
        exprt call(jml_ids::jml_method_call);
        call.set("method_name", field_name);
        call.operands().push_back(left);
        for(auto &a : args)
          call.operands().push_back(a);
        left = call;
      }
      else
      {
        // Field access: obj.field
        // Use an unresolved representation (type resolution in Phase 3).
        exprt field_access(jml_ids::jml_field_access);
        field_access.set("field_name", field_name);
        field_access.operands().push_back(left);
        left = field_access;
      }
    }
    else if(peek() == jml_token_kindt::LBRACKET)
    {
      advance();
      exprt index = expr();
      if(has_error_)
        return nil_exprt();
      if(!expect(jml_token_kindt::RBRACKET, "']'"))
        return nil_exprt();
      left = index_exprt(left, index);
    }
    else
    {
      break;
    }
  }
  return left;
}

exprt jml_parser_statet::primary()
{
  switch(peek())
  {
  case jml_token_kindt::INTEGER_LITERAL:
  {
    const std::string &text = current().text;
    advance();
    // Parse integer value
    bool is_long = (!text.empty() && (text.back() == 'L' || text.back() == 'l'));
    std::string num_text = is_long ? text.substr(0, text.size() - 1) : text;
    mp_integer value;
    if(num_text.size() > 2 && num_text[0] == '0' &&
       (num_text[1] == 'x' || num_text[1] == 'X'))
    {
      value = string2integer(num_text.substr(2), 16);
    }
    else
    {
      value = string2integer(num_text, 10);
    }
    const typet type = is_long ? signedbv_typet(64) : signedbv_typet(32);
    return from_integer(value, type);
  }

  case jml_token_kindt::BOOLEAN_LITERAL:
  {
    const bool val = (current().text == "true");
    advance();
    return val ? exprt(true_exprt()) : exprt(false_exprt());
  }

  case jml_token_kindt::NULL_LITERAL:
    advance();
    // Unresolved null — type will be determined in Phase 3
    return constant_exprt("NULL", pointer_typet(empty_typet(), 64));

  case jml_token_kindt::THIS:
    advance();
    return symbol_exprt("this", typet());

  case jml_token_kindt::IDENTIFIER:
  {
    const std::string name = current().text;
    advance();
    // Resolve: could be a local variable, parameter, or field.
    // For now, emit as an unresolved symbol; Phase 3 resolves.
    return symbol_exprt(name, typet());
  }

  case jml_token_kindt::JML_RESULT:
    advance();
    // \result maps to __CPROVER_return_value
    return symbol_exprt(CPROVER_PREFIX "return_value", typet());

  case jml_token_kindt::JML_OLD:
    return parse_old();

  case jml_token_kindt::JML_FORALL:
    advance();
    return parse_quantifier(true);

  case jml_token_kindt::JML_EXISTS:
    advance();
    return parse_quantifier(false);

  case jml_token_kindt::JML_FRESH:
    return parse_fresh();

  case jml_token_kindt::JML_NONNULLELEMENTS:
  {
    advance();
    if(!expect(jml_token_kindt::LPAREN, "'('"))
      return nil_exprt();
    exprt arg = expr();
    if(has_error_)
      return nil_exprt();
    if(!expect(jml_token_kindt::RPAREN, "')'"))
      return nil_exprt();
    // Represent as a special expression; lowered in Phase 3
    exprt result(jml_ids::jml_nonnullelements);
    result.operands().push_back(arg);
    return result;
  }

  case jml_token_kindt::JML_SUM:
  case jml_token_kindt::JML_PRODUCT:
  case jml_token_kindt::JML_MIN:
  case jml_token_kindt::JML_MAX:
  case jml_token_kindt::JML_NUM_OF:
  {
    // \sum/\product/\min/\max/\num_of type var; range; body
    const auto kind = peek();
    advance();
    typet var_type = parse_type();
    if(has_error_)
      return nil_exprt();
    if(peek() != jml_token_kindt::IDENTIFIER)
    {
      error("expected variable name in aggregate expression");
      return nil_exprt();
    }
    const std::string var_name = current().text;
    advance();
    if(!expect(jml_token_kindt::SEMICOLON, "';'"))
      return nil_exprt();
    exprt range = expr();
    if(has_error_)
      return nil_exprt();
    if(!expect(jml_token_kindt::SEMICOLON, "';'"))
      return nil_exprt();
    exprt body = expr();
    if(has_error_)
      return nil_exprt();

    // Determine the aggregate operation ID
    irep_idt op_id;
    switch(kind)
    {
    case jml_token_kindt::JML_SUM:
      op_id = jml_ids::jml_sum;
      break;
    case jml_token_kindt::JML_PRODUCT:
      op_id = jml_ids::jml_product;
      break;
    case jml_token_kindt::JML_MIN:
      op_id = jml_ids::jml_min;
      break;
    case jml_token_kindt::JML_MAX:
      op_id = jml_ids::jml_max;
      break;
    case jml_token_kindt::JML_NUM_OF:
      op_id = jml_ids::jml_num_of;
      break;
    default:
      op_id = jml_ids::jml_aggregate;
    }

    exprt result(op_id);
    result.set("variable", var_name);
    result.type() = var_type;
    result.operands().push_back(symbol_exprt(var_name, var_type));
    result.operands().push_back(range);
    result.operands().push_back(body);
    return result;
  }

  case jml_token_kindt::JML_NOTHING:
    advance();
    return exprt(jml_ids::jml_nothing);

  case jml_token_kindt::JML_EVERYTHING:
    advance();
    return exprt(jml_ids::jml_everything);

  case jml_token_kindt::LPAREN:
  {
    advance();
    // Could be: (expr), (type)expr (cast), or ternary
    exprt inner = expr();
    if(has_error_)
      return nil_exprt();
    // Ternary: (cond ? then : else) — but ternary can also
    // appear without parens. Handle ? here.
    if(peek() == jml_token_kindt::QUESTION)
    {
      advance();
      exprt then_expr = expr();
      if(has_error_)
        return nil_exprt();
      if(!expect(jml_token_kindt::COLON, "':'"))
        return nil_exprt();
      exprt else_expr = expr();
      if(has_error_)
        return nil_exprt();
      if(!expect(jml_token_kindt::RPAREN, "')'"))
        return nil_exprt();
      return if_exprt(inner, then_expr, else_expr);
    }
    if(!expect(jml_token_kindt::RPAREN, "')'"))
      return nil_exprt();
    return inner;
  }

  default:
    error("unexpected token '" + current().text + "'");
    return nil_exprt();
  }
}

exprt jml_parser_statet::parse_old()
{
  advance(); // consume \old
  if(!expect(jml_token_kindt::LPAREN, "'(' after \\old"))
    return nil_exprt();
  exprt arg = expr();
  if(has_error_)
    return nil_exprt();
  if(!expect(jml_token_kindt::RPAREN, "')'"))
    return nil_exprt();
  // \old(e) → history_exprt(e, ID_old)
  return history_exprt(arg, ID_old);
}

exprt jml_parser_statet::parse_fresh()
{
  advance(); // consume \fresh
  if(!expect(jml_token_kindt::LPAREN, "'(' after \\fresh"))
    return nil_exprt();
  exprt arg = expr();
  if(has_error_)
    return nil_exprt();
  if(!expect(jml_token_kindt::RPAREN, "')'"))
    return nil_exprt();
  // Represent as a special expression; lowered in Phase 3
  exprt result(jml_ids::jml_fresh);
  result.operands().push_back(arg);
  return result;
}

exprt jml_parser_statet::parse_quantifier(bool is_forall)
{
  // \forall type var; range; body
  // \exists type var; range; body
  // For now, parse a simplified form:
  //   \forall int i; 0 <= i && i < n; a[i] > 0
  typet var_type = parse_type();
  if(has_error_)
    return nil_exprt();
  if(peek() != jml_token_kindt::IDENTIFIER)
  {
    error("expected variable name in quantifier");
    return nil_exprt();
  }
  const std::string var_name = current().text;
  advance();
  if(!expect(jml_token_kindt::SEMICOLON, "';' after quantifier variable"))
    return nil_exprt();
  exprt range = expr();
  if(has_error_)
    return nil_exprt();
  if(!expect(jml_token_kindt::SEMICOLON, "';' after quantifier range"))
    return nil_exprt();
  exprt body = expr();
  if(has_error_)
    return nil_exprt();

  // Build: forall var_name : var_type . (range ==> body)
  // or:    exists var_name : var_type . (range && body)
  symbol_exprt var_sym(var_name, var_type);
  exprt quantified_expr;
  if(is_forall)
    quantified_expr = or_exprt(not_exprt(range), body); // range ==> body
  else
    quantified_expr = and_exprt(range, body); // range && body

  if(is_forall)
    return forall_exprt(var_sym, quantified_expr);
  else
    return exists_exprt(var_sym, quantified_expr);
}

typet jml_parser_statet::parse_type()
{
  switch(peek())
  {
  case jml_token_kindt::INT:
    advance();
    return signedbv_typet(32);
  case jml_token_kindt::LONG:
    advance();
    return signedbv_typet(64);
  case jml_token_kindt::BOOLEAN:
    advance();
    return bool_typet();
  case jml_token_kindt::BYTE:
    advance();
    return signedbv_typet(8);
  case jml_token_kindt::SHORT:
    advance();
    return signedbv_typet(16);
  case jml_token_kindt::CHAR:
    advance();
    return unsignedbv_typet(16);
  case jml_token_kindt::FLOAT:
    advance();
    return ieee_float_spect::single_precision().to_type();
  case jml_token_kindt::DOUBLE:
    advance();
    return ieee_float_spect::double_precision().to_type();
  case jml_token_kindt::IDENTIFIER:
  {
    // Reference type: ClassName
    std::string class_name = current().text;
    advance();
    while(peek() == jml_token_kindt::DOT)
    {
      advance();
      if(peek() != jml_token_kindt::IDENTIFIER)
        break;
      class_name += "." + current().text;
      advance();
    }
    // Array dimensions
    while(peek() == jml_token_kindt::LBRACKET)
    {
      advance();
      if(!expect(jml_token_kindt::RBRACKET, "']'"))
        return typet();
      class_name += "[]";
    }
    return struct_tag_typet("java::" + class_name);
  }
  default:
    error("expected type");
    return typet();
  }
}

jml_parse_resultt jml_parser_statet::parse_expression()
{
  jml_parse_resultt result;
  result.expr = expr();
  if(has_error_)
  {
    result.success = false;
    result.error_message = error_message_;
    result.error_position = error_position_;
  }
  else if(peek() != jml_token_kindt::END_OF_INPUT)
  {
    result.success = false;
    result.error_message =
      "unexpected token '" + current().text + "' after expression";
    result.error_position = current().position;
  }
  else
  {
    result.success = true;
  }
  return result;
}

} // namespace

// ─── Public API ─────────────────────────────────────────────────

jml_parse_resultt jml_parse_expression(
  const std::string &jml_text,
  const irep_idt &method_id,
  const irep_idt &class_id,
  const symbol_table_baset &symbol_table)
{
  auto tokens = jml_tokenize(jml_text);
  jml_parser_statet parser(std::move(tokens), method_id, class_id, symbol_table);
  return parser.parse_expression();
}

jml_clauset jml_parse_clause(
  const std::string &clause_text,
  const irep_idt &method_id,
  const irep_idt &class_id,
  const symbol_table_baset &symbol_table)
{
  jml_clauset result;
  result.raw_text = clause_text;

  // Strip leading/trailing whitespace
  std::string text = clause_text;
  while(!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
    text.erase(text.begin());
  while(!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    text.pop_back();
  // Strip trailing semicolon
  if(!text.empty() && text.back() == ';')
    text.pop_back();

  // Determine clause kind from keyword prefix
  auto strip_prefix = [&](const std::string &prefix) -> bool
  {
    if(text.size() > prefix.size() &&
       text.substr(0, prefix.size()) == prefix &&
       std::isspace(static_cast<unsigned char>(text[prefix.size()])))
    {
      text = text.substr(prefix.size());
      while(!text.empty() &&
            std::isspace(static_cast<unsigned char>(text.front())))
        text.erase(text.begin());
      return true;
    }
    return false;
  };

  if(strip_prefix("requires"))
    result.kind = jml_clauset::kindt::REQUIRES;
  else if(strip_prefix("ensures"))
    result.kind = jml_clauset::kindt::ENSURES;
  else if(strip_prefix("assignable") || strip_prefix("modifiable") ||
          strip_prefix("modifies"))
    result.kind = jml_clauset::kindt::ASSIGNABLE;
  else if(strip_prefix("signals_only"))
    result.kind = jml_clauset::kindt::SIGNALS_ONLY;
  else if(strip_prefix("signals"))
    result.kind = jml_clauset::kindt::SIGNALS;
  else if(strip_prefix("invariant") || strip_prefix("maintaining") ||
          strip_prefix("loop_invariant"))
    result.kind = jml_clauset::kindt::INVARIANT;
  else if(strip_prefix("decreases") || strip_prefix("decreasing") ||
          strip_prefix("loop_variant"))
    result.kind = jml_clauset::kindt::DECREASES;
  else if(text == "pure")
  {
    result.kind = jml_clauset::kindt::PURE;
    result.expr = true_exprt();
    return result;
  }
  else if(text == "also")
  {
    result.kind = jml_clauset::kindt::ALSO;
    result.expr = true_exprt();
    return result;
  }
  else
  {
    result.kind = jml_clauset::kindt::UNKNOWN;
    return result;
  }

  // Special case: assignable \nothing / \everything
  if(result.kind == jml_clauset::kindt::ASSIGNABLE)
  {
    if(text == "\\nothing")
    {
      result.expr = exprt("jml_nothing");
      return result;
    }
    if(text == "\\everything")
    {
      result.expr = exprt("jml_everything");
      return result;
    }
  }

  // Special case: signals_only T1, T2, ...;
  // The body is a comma-separated list of Java exception types,
  // not an expression. Parse the list into signal_types and
  // skip the expression parser.
  if(result.kind == jml_clauset::kindt::SIGNALS_ONLY)
  {
    auto trim_str = [](std::string s)
    {
      auto start = s.find_first_not_of(" \t");
      if(start == std::string::npos)
        return std::string{};
      auto end = s.find_last_not_of(" \t;");
      return s.substr(start, end - start + 1);
    };
    std::string body = trim_str(text);
    // Split by comma.
    std::string::size_type pos = 0;
    while(pos <= body.size())
    {
      const auto comma = body.find(',', pos);
      const auto stop = (comma == std::string::npos) ? body.size() : comma;
      const std::string ty = trim_str(body.substr(pos, stop - pos));
      if(!ty.empty())
        result.signal_types.push_back(ty);
      if(comma == std::string::npos)
        break;
      pos = comma + 1;
    }
    result.expr = true_exprt();
    return result;
  }

  // Special case: signals (T e) predicate;
  // Recognise the optional `(T e)` after `signals` and capture
  // the type plus bound variable name; the rest of `text` is
  // the predicate expression.
  if(result.kind == jml_clauset::kindt::SIGNALS)
  {
    auto skip_ws = [](const std::string &s, std::size_t &i)
    {
      while(i < s.size() &&
            std::isspace(static_cast<unsigned char>(s[i])))
        ++i;
    };
    std::size_t i = 0;
    skip_ws(text, i);
    if(i < text.size() && text[i] == '(')
    {
      ++i;
      skip_ws(text, i);
      // Parse type: dotted Java identifier (e.g.
      // `IllegalArgumentException`,
      // `com.example.Foo$Bar`).
      const std::size_t type_start = i;
      while(i < text.size() &&
            (std::isalnum(static_cast<unsigned char>(text[i])) ||
             text[i] == '.' || text[i] == '_' || text[i] == '$'))
        ++i;
      std::string ty = text.substr(type_start, i - type_start);
      skip_ws(text, i);
      // Parse variable name (identifier).
      const std::size_t var_start = i;
      while(i < text.size() &&
            (std::isalnum(static_cast<unsigned char>(text[i])) ||
             text[i] == '_'))
        ++i;
      std::string var = text.substr(var_start, i - var_start);
      skip_ws(text, i);
      if(i < text.size() && text[i] == ')' && !ty.empty() && !var.empty())
      {
        ++i;
        result.signal_type = ty;
        result.signal_var = var;
        // Trim leading whitespace and trailing ';' from the
        // predicate slice.
        std::string pred = text.substr(i);
        while(!pred.empty() &&
              std::isspace(static_cast<unsigned char>(pred.front())))
          pred.erase(pred.begin());
        while(!pred.empty() && pred.back() == ';')
          pred.pop_back();
        text = pred;
      }
    }
  }

  // Parse the expression
  auto parse_result =
    jml_parse_expression(text, method_id, class_id, symbol_table);
  result.expr = parse_result.expr;
  return result;
}
