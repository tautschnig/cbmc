/// Python to GOTO converter — BinOp (PLR §6.7 / §6.8 / §6.9),
/// UnaryOp (PLR §6.6), and BoolOp (PLR §6.11) implementation.
///
/// Extracted from python_converter.cpp to reduce the main file size.
/// All logic and class-member state remains unchanged — this file is
/// a pure source-split.

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/floatbv_expr.h>
#include <util/ieee_float.h>
#include <util/json.h>
#include <util/mathematical_expr.h>
#include <util/mathematical_types.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/symbol.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>

// PLR §6.7: Binary arithmetic operations
// PLR §6.8: Shifting operations
// PLR §6.9: Binary bitwise operations
exprt python_convertert::convert_bin_op(const jsont &expr)
{
  exprt left = convert_expression(json_member(expr, "left"));
  exprt right = convert_expression(json_member(expr, "right"));
  std::string op = json_string(json_member(json_member(expr, "op"), "_type"));

  if(left.is_nil() || right.is_nil())
    return nil_exprt{};

  // PLR §3.3.1: binary-operator dunder dispatch — if the
  // left operand is a user-defined class instance with a
  // matching __<op>__ method, dispatch to it.
  //
  // PLR §3.3.1 reflected variant: if left lacks the
  // method but the right operand has the __r<op>__
  // reflected method, Python tries right.__r<op>__(left).
  // We cover both.
  {
    static const std::map<std::string, std::pair<std::string, std::string>>
      op_to_dunder = {
        {"Add", {"__add__", "__radd__"}},
        {"Sub", {"__sub__", "__rsub__"}},
        {"Mult", {"__mul__", "__rmul__"}},
        {"MatMult", {"__matmul__", "__rmatmul__"}},
        {"Div", {"__truediv__", "__rtruediv__"}},
        {"FloorDiv", {"__floordiv__", "__rfloordiv__"}},
        {"Mod", {"__mod__", "__rmod__"}},
        {"Pow", {"__pow__", "__rpow__"}},
        {"LShift", {"__lshift__", "__rlshift__"}},
        {"RShift", {"__rshift__", "__rrshift__"}},
        {"BitOr", {"__or__", "__ror__"}},
        {"BitXor", {"__xor__", "__rxor__"}},
        {"BitAnd", {"__and__", "__rand__"}}};
    auto du = op_to_dunder.find(op);
    if(du != op_to_dunder.end())
    {
      auto try_dispatch = [&](
                            const exprt &self,
                            const exprt &other,
                            const std::string &meth) -> exprt
      {
        std::string tag;
        if(self.type().id() == ID_struct)
          tag = id2string(to_struct_type(self.type()).get_tag());
        else if(self.type().id() == ID_struct_tag)
          tag = id2string(to_struct_tag_type(self.type()).get_identifier());
        if(tag.substr(0, 13) != "python_class_")
          return nil_exprt{};
        std::string bare = tag.substr(13);
        for(const std::string &prefix :
            {std::string{"python::"} + tag + "::" + meth,
             std::string{"python::"} + bare + "::" + meth})
        {
          const symbolt *ds = symbol_table.lookup(irep_idt{prefix});
          if(ds != nullptr)
          {
            typet ret_type = self.type();
            exprt other_arg = other;
            if(ds->type.id() == ID_code)
            {
              const auto &ct = to_code_type(ds->type);
              ret_type = ct.return_type();
              if(ct.parameters().size() >= 2)
              {
                const typet &param1_t = ct.parameters()[1].type();
                if(other_arg.type() != param1_t)
                  other_arg = safe_typecast(other_arg, param1_t);
              }
            }
            return exprt{side_effect_expr_function_callt{
              ds->symbol_expr(),
              {address_of_exprt{self}, other_arg},
              ret_type,
              source_locationt{}}};
          }
        }
        return nil_exprt{};
      };

      exprt direct = try_dispatch(left, right, du->second.first);
      if(direct.id() != ID_nil)
        return direct;
      exprt reflected = try_dispatch(right, left, du->second.second);
      if(reflected.id() != ID_nil)
        return reflected;
    }
  }

  // PLR §6.7: type compatibility for binary operators. Python
  // raises TypeError at runtime for mismatched operand types
  // (e.g. int + list). CBMC's solver layers do not tolerate
  // mixed-type arithmetic in GOTO and abort with an invariant.
  // Return a nondet int for obviously incompatible operand
  // combinations so the symex graph stays well-typed; the
  // caller's reasoning continues with an over-approximation.
  {
    bool l_is_list = is_python_list_type(left.type());
    bool r_is_list = is_python_list_type(right.type());
    bool l_is_dict = is_python_dict_type(left.type());
    bool r_is_dict = is_python_dict_type(right.type());
    bool l_is_set = is_python_set_type(left.type());
    bool r_is_set = is_python_set_type(right.type());
    bool l_is_str = is_python_string_type(left.type());
    bool r_is_str = is_python_string_type(right.type());
    bool l_is_num =
      left.type().id() == ID_signedbv || left.type().id() == ID_integer ||
      left.type().id() == ID_floatbv || left.type().id() == ID_bool;
    bool r_is_num =
      right.type().id() == ID_signedbv || right.type().id() == ID_integer ||
      right.type().id() == ID_floatbv || right.type().id() == ID_bool;
    bool incompatible = false;
    // list OP non-list: only list * int (repeat) is valid.
    if(l_is_list && !r_is_list)
    {
      if(!(op == "Mult" && r_is_num))
        incompatible = true;
    }
    if(r_is_list && !l_is_list)
    {
      if(!(op == "Mult" && l_is_num))
        incompatible = true;
    }
    // dict / set with anything else is invalid.
    if(l_is_dict != r_is_dict)
      incompatible = true;
    if(l_is_set != r_is_set && (l_is_num || r_is_num))
      incompatible = true;
    // str OP non-str/num: invalid unless str * int (repeat).
    if(l_is_str && !r_is_str)
    {
      if(!(op == "Mult" && r_is_num))
        incompatible = true;
    }
    if(r_is_str && !l_is_str)
    {
      if(!(op == "Mult" && l_is_num))
        incompatible = true;
    }
    if(incompatible)
    {
      log_overapprox(
        "BinOp " + op + " on incompatible types — returning nondet");
      return side_effect_expr_nondett{python_int_type(), get_location(expr)};
    }
  }

  // PLib stdtypes: "str" type, §6.7: binary arithmetic
  // String concatenation: s1 + s2 produces a new string containing the
  // PLR §3.2: Complex number arithmetic
  auto is_complex = [](const typet &t)
  {
    return t.id() == ID_struct &&
           to_struct_type(t).get_tag() == "python_complex";
  };
  // PLR §3.2: Promote int/float to complex for mixed arithmetic
  if(is_complex(left.type()) && !is_complex(right.type()))
  {
    struct_typet ct = to_struct_type(left.type());
    exprt real_part = safe_typecast(right, double_type());
    right = struct_exprt{{real_part, safe_zero(double_type())}, ct};
  }
  if(!is_complex(left.type()) && is_complex(right.type()))
  {
    struct_typet ct = to_struct_type(right.type());
    exprt real_part = safe_typecast(left, double_type());
    left = struct_exprt{{real_part, safe_zero(double_type())}, ct};
  }
  if(is_complex(left.type()) && is_complex(right.type()))
  {
    struct_typet ct = to_struct_type(left.type());
    member_exprt lr{left, "real", double_type()};
    member_exprt li{left, "imag", double_type()};
    member_exprt rr{right, "real", double_type()};
    member_exprt ri{right, "imag", double_type()};
    if(op == "Add")
      return struct_exprt{{plus_exprt{lr, rr}, plus_exprt{li, ri}}, ct};
    if(op == "Sub")
      return struct_exprt{{minus_exprt{lr, rr}, minus_exprt{li, ri}}, ct};
    if(op == "Mult")
      return struct_exprt{
        {minus_exprt{mult_exprt{lr, rr}, mult_exprt{li, ri}},
         plus_exprt{mult_exprt{lr, ri}, mult_exprt{li, rr}}},
        ct};
    // PLR §6.7: FloorDiv and Mod on complex raise TypeError
    if(op == "FloorDiv" || op == "Mod")
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
        pending_checks.push_back(
          code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
      if(exc_type_sym != nullptr)
      {
        long type_hash = exception_type_hash("TypeError");
        pending_checks.push_back(code_frontend_assignt{
          exc_type_sym->symbol_expr(),
          from_integer(type_hash, python_int_type())});
      }
      return from_integer(0, python_int_type());
    }
    if(op == "Div")
    {
      // (a+bi)/(c+di) = ((ac+bd) + (bc-ad)i) / (c²+d²)
      exprt denom = plus_exprt{mult_exprt{rr, rr}, mult_exprt{ri, ri}};
      exprt real_num = plus_exprt{mult_exprt{lr, rr}, mult_exprt{li, ri}};
      exprt imag_num = minus_exprt{mult_exprt{li, rr}, mult_exprt{lr, ri}};
      return struct_exprt{
        {div_exprt{real_num, denom}, div_exprt{imag_num, denom}}, ct};
    }
    // Other ops: return nondet complex
    return side_effect_expr_nondett{ct, source_locationt{}};
  }

  // PLib stdtypes: "str" type, §6.7: binary arithmetic
  // String concatenation: s1 + s2 produces a new string containing the
  // characters of s1 followed by s2. We track content by copying data
  // arrays element-by-element via pending_checks.
  if(
    is_python_string_type(left.type()) && is_python_string_type(right.type()) &&
    op == "Add")
  {
    // Constant-string optimization for concat
    {
      auto lv = extract_string_value(left);
      if(!lv.has_value() && left.id() == ID_symbol)
      {
        auto it = string_constants.find(to_symbol_expr(left).get_identifier());
        if(it != string_constants.end())
          lv = it->second;
      }
      auto rv = extract_string_value(right);
      if(!rv.has_value() && right.id() == ID_symbol)
      {
        auto it = string_constants.find(to_symbol_expr(right).get_identifier());
        if(it != string_constants.end())
          rv = it->second;
      }
      if(lv.has_value() && rv.has_value())
        return python_string_literal(lv.value() + rv.value());
    }
    // Fallback: use string solver for non-constant concat
    {
      // Decompose symbols into struct_exprt so the solver can process them.
      // The solver requires struct_exprt (not symbol_exprt) for source strings.
      auto to_string_struct = [](const exprt &s) -> exprt
      {
        if(s.id() == ID_struct && s.operands().size() == 2)
          return s; // already a struct
        return struct_exprt(
          {member_exprt(s, "length", signedbv_typet{64}),
           member_exprt(s, "data", pointer_typet(unsignedbv_typet{8}, 64))},
          s.type());
      };
      return emit_string_function(
        ID_cprover_string_concat_func,
        {to_string_struct(left), to_string_struct(right)},
        symbol_table,
        pending_checks);
    }
    struct_typet str_type = python_string_struct_def();
    const auto &data_type = array_typet(
      unsignedbv_typet{8},
      from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
    member_exprt left_len{left, "length", signedbv_typet{64}};
    member_exprt right_len{right, "length", signedbv_typet{64}};
    member_exprt left_data{left, "data", data_type};
    member_exprt right_data{right, "data", data_type};

    // Create temporary for result
    static unsigned str_concat_counter = 0;
    std::string tmp_name =
      "__str_concat_" + std::to_string(str_concat_counter++);
    std::string tmp_qname = qualify_name(tmp_name);
    irep_idt tmp_id{tmp_qname};

    if(symbol_table.lookup(tmp_id) == nullptr)
    {
      symbolt tmp_sym{tmp_id, str_type, "python"};
      tmp_sym.base_name = tmp_name;
      tmp_sym.is_lvalue = true;
      tmp_sym.is_state_var = true;
      symbol_table.add(tmp_sym);
    }

    const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);
    symbol_exprt tmp = tmp_sym.symbol_expr();

    // Set length
    pending_checks.push_back(code_frontend_assignt{
      member_exprt{tmp, "length", signedbv_typet{64}},
      plus_exprt{left_len, right_len}});

    // Copy data: for each index i, tmp.data[i] =
    //   i < left.length ? left.data[i] : right.data[i - left.length]
    member_exprt tmp_data{tmp, "data", data_type};
    for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      exprt from_left = index_exprt{left_data, idx};
      exprt from_right = index_exprt{right_data, minus_exprt{idx, left_len}};
      exprt val = if_exprt{
        binary_relation_exprt{idx, ID_lt, left_len}, from_left, from_right};
      pending_checks.push_back(
        code_frontend_assignt{index_exprt{tmp_data, idx}, val});
    }

    return std::move(tmp);
  }

  // PLR §6.7: Type-dispatched arithmetic on tagged unions
  // When both operands are tagged unions, dispatch on types:
  // if either is FLOAT, use float arithmetic; else use int
  if(
    is_python_value_type(left.type()) && is_python_value_type(right.type()) &&
    (op == "Add" || op == "Sub" || op == "Mult"))
  {
    exprt either_float = or_exprt{
      python_value_is(left, python_type_tagt::FLOAT),
      python_value_is(right, python_type_tagt::FLOAT)};
    exprt left_int = python_value_int(left);
    exprt right_int = python_value_int(right);
    exprt left_float = python_value_float(left);
    exprt right_float = python_value_float(right);

    exprt int_result, float_result;
    if(op == "Add")
    {
      int_result = plus_exprt{left_int, right_int};
      float_result = plus_exprt{left_float, right_float};
    }
    else if(op == "Sub")
    {
      int_result = minus_exprt{left_int, right_int};
      float_result = minus_exprt{left_float, right_float};
    }
    else
    {
      int_result = mult_exprt{left_int, right_int};
      float_result = mult_exprt{left_float, right_float};
    }

    // Return tagged union with appropriate type
    exprt int_wrapped = make_python_value(python_type_tagt::INT, int_result);
    exprt float_wrapped =
      make_python_value(python_type_tagt::FLOAT, float_result);
    return if_exprt{either_float, float_wrapped, int_wrapped};
  }

  // Unwrap tagged-union values to concrete types for operations
  if(is_python_value_type(left.type()))
    left = unwrap_value(
      left, right.type().id() != ID_struct ? right.type() : python_int_type());
  if(is_python_value_type(right.type()))
    right = unwrap_value(right, left.type());

  // Guard: if types are incompatible after unwrapping, cast to match
  if(
    left.type() != right.type() && op == "Add" &&
    (is_python_string_type(left.type()) ||
     is_python_string_type(right.type())) &&
    !(is_python_string_type(left.type()) &&
      is_python_string_type(right.type())))
  {
    // String + non-string: TypeError in Python
    const symbolt *exc_sym = symbol_table.lookup("python::__exception_active");
    if(exc_sym != nullptr)
      pending_checks.push_back(
        code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
    const symbolt *exc_type_sym =
      symbol_table.lookup("python::__exception_type");
    if(exc_type_sym != nullptr)
      pending_checks.push_back(code_frontend_assignt{
        exc_type_sym->symbol_expr(),
        from_integer(exception_type_hash("TypeError"), python_int_type())});
    return side_effect_expr_nondett{python_string_type(), source_locationt{}};
  }

  // PLR §6.7: String repetition: "ab" * 3 → "ababab"
  if(
    op == "Mult" &&
    (is_python_string_type(left.type()) || is_python_string_type(right.type())))
  {
    exprt str_op = is_python_string_type(left.type()) ? left : right;
    exprt num_op = is_python_string_type(left.type()) ? right : left;
    num_op = safe_typecast(num_op, signedbv_typet{64});

    // Constant-string optimization for repetition
    {
      auto sv = extract_string_value(str_op);
      if(!sv.has_value() && str_op.id() == ID_symbol)
      {
        auto it =
          string_constants.find(to_symbol_expr(str_op).get_identifier());
        if(it != string_constants.end())
          sv = it->second;
      }
      auto nv = try_eval_double(num_op);
      if(sv.has_value() && nv.has_value() && nv.value() >= 0)
      {
        std::string result;
        for(int i = 0; i < static_cast<int>(nv.value()); i++)
          result += sv.value();
        return python_string_literal(result);
      }
    }
    return side_effect_expr_nondett{python_string_type(), source_locationt{}};
    struct_typet str_type = python_string_struct_def();
    const auto &data_type = array_typet(
      unsignedbv_typet{8},
      from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
    member_exprt old_len{str_op, "length", signedbv_typet{64}};
    member_exprt old_data{str_op, "data", data_type};

    static unsigned str_rep_counter = 0;
    std::string tmp_name = "__str_rep_" + std::to_string(str_rep_counter++);
    std::string tmp_qname = qualify_name(tmp_name);
    irep_idt tmp_id{tmp_qname};
    if(symbol_table.lookup(tmp_id) == nullptr)
    {
      symbolt tmp_sym{tmp_id, str_type, "python"};
      tmp_sym.base_name = tmp_name;
      tmp_sym.is_lvalue = true;
      tmp_sym.is_state_var = true;
      symbol_table.add(tmp_sym);
    }
    symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
    exprt new_len = mult_exprt{old_len, num_op};
    pending_checks.push_back(code_frontend_assignt{
      member_exprt{tmp, "length", signedbv_typet{64}}, new_len});
    member_exprt tmp_data{tmp, "data", data_type};
    for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      exprt src_idx = mod_exprt{idx, old_len};
      exprt val = if_exprt{
        binary_relation_exprt{idx, ID_lt, new_len},
        index_exprt{old_data, src_idx},
        from_integer(0, unsignedbv_typet{8})};
      pending_checks.push_back(
        code_frontend_assignt{index_exprt{tmp_data, idx}, val});
    }
    return std::move(tmp);
  }

  // Set operations: bitmap-based
  if(is_python_set_type(left.type()) && is_python_set_type(right.type()))
  {
    member_exprt lb{left, "bitmap", unsignedbv_typet{64}};
    member_exprt rb{right, "bitmap", unsignedbv_typet{64}};
    member_exprt lo{left, "offset", signedbv_typet{64}};
    exprt result_bitmap;
    if(op == "Sub")
      result_bitmap = bitand_exprt{lb, bitnot_exprt{rb}};
    else if(op == "BitOr") // a | b (union)
      result_bitmap = bitor_exprt{lb, rb};
    else if(op == "BitAnd") // a & b (intersection)
      result_bitmap = bitand_exprt{lb, rb};
    else if(op == "BitXor") // a ^ b (symmetric difference)
      result_bitmap = bitxor_exprt{lb, rb};
    else
      return minus_exprt{left, right}; // fallback for non-set ops
    return struct_exprt{{result_bitmap, lo}, python_set_type()};
  }

  // List concatenation: [1,2] + [3,4] → [1,2,3,4]
  if(
    is_python_list_type(left.type()) && is_python_list_type(right.type()) &&
    op == "Add")
  {
    const auto &list_st = to_struct_type(left.type());
    const auto &data_type = to_array_type(list_st.components()[1].type());
    typet elem_type = data_type.element_type();

    member_exprt left_len{left, "length", signedbv_typet{64}};
    member_exprt right_len{right, "length", signedbv_typet{64}};
    member_exprt left_data{left, "data", data_type};
    member_exprt right_data{right, "data", data_type};

    exprt new_len = plus_exprt{left_len, right_len};
    exprt::operandst elems;
    for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      // If i < left_len, take from left; else take from right at i-left_len
      elems.push_back(if_exprt{
        binary_relation_exprt{idx, ID_lt, left_len},
        index_exprt{left_data, idx},
        index_exprt{right_data, minus_exprt{idx, left_len}}});
    }
    array_exprt new_data{std::move(elems), data_type};
    return struct_exprt{{new_len, new_data}, left.type()};
  }

  // List repetition with content tracking: lst * n or n * lst
  if(
    op == "Mult" && is_python_list_type(right.type()) &&
    !is_python_list_type(left.type()))
  {
    std::swap(left, right); // normalize to lst * n
  }
  if(is_python_list_type(left.type()) && op == "Mult")
  {
    struct_typet list_type = to_struct_type(left.type());
    const auto &data_type = to_array_type(list_type.components()[1].type());
    member_exprt old_len{left, "length", signedbv_typet{64}};
    member_exprt old_data{left, "data", data_type};
    exprt n = safe_typecast(right, signedbv_typet{64});

    static unsigned list_repeat_counter = 0;
    std::string tmp_name =
      "__list_repeat_" + std::to_string(list_repeat_counter++);
    std::string tmp_qname = qualify_name(tmp_name);
    irep_idt tmp_id{tmp_qname};

    if(symbol_table.lookup(tmp_id) == nullptr)
    {
      symbolt tmp_sym{tmp_id, list_type, "python"};
      tmp_sym.base_name = tmp_name;
      tmp_sym.is_lvalue = true;
      tmp_sym.is_state_var = true;
      symbol_table.add(tmp_sym);
    }

    const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);
    symbol_exprt tmp = tmp_sym.symbol_expr();

    pending_checks.push_back(code_frontend_assignt{
      member_exprt{tmp, "length", signedbv_typet{64}}, mult_exprt{old_len, n}});

    // Copy data: tmp.data[i] = i < new_len ? old.data[i % old.length] : 0
    member_exprt tmp_data{tmp, "data", data_type};
    exprt new_len = mult_exprt{old_len, n};
    for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      exprt src_idx = mod_exprt{idx, old_len};
      exprt val = if_exprt{
        binary_relation_exprt{idx, ID_lt, new_len},
        index_exprt{old_data, src_idx},
        safe_zero(data_type.element_type())};
      pending_checks.push_back(
        code_frontend_assignt{index_exprt{tmp_data, idx}, val});
    }

    return std::move(tmp);
  }

  // Type promotion: Python promotes bool → int → float. For
  // BinOp we widen to whichever side is more numerically
  // general.
  if(left.type() != right.type())
  {
    if(left.type().id() == ID_floatbv)
      right = safe_typecast(right, left.type());
    else if(right.type().id() == ID_floatbv)
      left = safe_typecast(left, right.type());
    else if(left.type().id() == ID_bool && right.type().id() == ID_signedbv)
      left = safe_typecast(left, right.type());
    else if(right.type().id() == ID_bool && left.type().id() == ID_signedbv)
      right = safe_typecast(right, left.type());
    else if(
      left.type().id() == ID_signedbv && right.type().id() == ID_signedbv &&
      to_signedbv_type(left.type()).get_width() !=
        to_signedbv_type(right.type()).get_width())
    {
      // Mixed-width int: widen to the larger.
      if(
        to_signedbv_type(left.type()).get_width() >
        to_signedbv_type(right.type()).get_width())
        right = safe_typecast(right, left.type());
      else
        left = safe_typecast(left, right.type());
    }
  }

  if(op == "Add")
  {
    return plus_exprt{left, right};
  }
  else if(op == "Sub")
  {
    return minus_exprt{left, right};
  }
  else if(op == "Mult")
  {
    return mult_exprt{left, right};
  }
  else if(op == "FloorDiv")
  {
    // PLR §6.7: Complex // anything raises TypeError
    if(is_complex(left.type()) || is_complex(right.type()))
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
        pending_checks.push_back(
          code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
      if(exc_type_sym != nullptr)
      {
        long type_hash = exception_type_hash("TypeError");
        pending_checks.push_back(code_frontend_assignt{
          exc_type_sym->symbol_expr(),
          from_integer(type_hash, python_int_type())});
      }
      return from_integer(0, python_int_type());
    }
    // Python handles division by zero as ZeroDivisionError exception,
    // not as a property check. Set the exception flag instead.
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
      {
        exprt is_zero = equal_exprt{right, safe_zero(right.type())};
        pending_checks.push_back(code_ifthenelset{
          is_zero,
          code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}}});
        if(exc_type_sym != nullptr)
          pending_checks.push_back(code_ifthenelset{
            is_zero,
            code_frontend_assignt{
              exc_type_sym->symbol_expr(),
              from_integer(
                exception_type_hash("ZeroDivisionError"), python_int_type())}});
      }
    }
    // PLR §6.7: Floor division rounds toward negative infinity
    // Constant evaluation
    if(
      left.is_constant() && right.is_constant() &&
      left.type().id() == ID_signedbv && right.type().id() == ID_signedbv)
    {
      mp_integer a, b;
      if(
        !to_integer(to_constant_expr(left), a) &&
        !to_integer(to_constant_expr(right), b) && b != 0)
      {
        mp_integer q = a / b;
        mp_integer r = a % b;
        if(r != 0 && ((a < 0) != (b < 0)))
          q -= 1;
        return from_integer(q, left.type());
      }
    }
    // C division truncates toward zero. Adjust for negative results:
    // floor_div(a, b) = a/b - (1 if (a%b != 0 and sign(a) != sign(b)) else 0)
    if(left.type().id() == ID_signedbv || left.type().id() == ID_unsignedbv)
    {
      exprt quotient = div_exprt{left, right};
      exprt remainder = mod_exprt{left, right};
      exprt has_remainder =
        notequal_exprt{remainder, from_integer(0, left.type())};
      exprt diff_sign = binary_relation_exprt{
        bitxor_exprt{left, right}, ID_lt, from_integer(0, left.type())};
      return minus_exprt{
        quotient,
        if_exprt{
          and_exprt{has_remainder, diff_sign},
          from_integer(1, left.type()),
          from_integer(0, left.type())}};
    }
    return div_exprt{left, right};
  }
  else if(op == "Mod")
  {
    // Constant evaluation: compute at conversion time
    if(
      left.is_constant() && right.is_constant() &&
      left.type().id() == ID_signedbv && right.type().id() == ID_signedbv)
    {
      mp_integer a, b;
      if(
        !to_integer(to_constant_expr(left), a) &&
        !to_integer(to_constant_expr(right), b) && b != 0)
      {
        // Python modulo: result has same sign as divisor
        mp_integer r = a % b;
        if(r != 0 && ((a < 0) != (b < 0)))
          r += b;
        return from_integer(r, left.type());
      }
    }
    // PLR §6.7: Complex % anything raises TypeError
    if(is_complex(left.type()) || is_complex(right.type()))
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
        pending_checks.push_back(
          code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
      if(exc_type_sym != nullptr)
      {
        long type_hash = exception_type_hash("TypeError");
        pending_checks.push_back(code_frontend_assignt{
          exc_type_sym->symbol_expr(),
          from_integer(type_hash, python_int_type())});
      }
      return from_integer(0, python_int_type());
    }
    // Python handles modulo by zero as ZeroDivisionError exception
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
      {
        exprt is_zero = equal_exprt{right, safe_zero(right.type())};
        pending_checks.push_back(code_ifthenelset{
          is_zero,
          code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}}});
        if(exc_type_sym != nullptr)
          pending_checks.push_back(code_ifthenelset{
            is_zero,
            code_frontend_assignt{
              exc_type_sym->symbol_expr(),
              from_integer(
                exception_type_hash("ZeroDivisionError"), python_int_type())}});
      }
    }
    // PLR §6.7: Python modulo: result has same sign as divisor
    if(left.type().id() == ID_signedbv || left.type().id() == ID_unsignedbv)
    {
      exprt c_mod = mod_exprt{left, right};
      exprt has_rem = notequal_exprt{c_mod, from_integer(0, left.type())};
      exprt diff_sign = binary_relation_exprt{
        bitxor_exprt{left, right}, ID_lt, from_integer(0, left.type())};
      return if_exprt{
        and_exprt{has_rem, diff_sign}, plus_exprt{c_mod, right}, c_mod};
    }
    // Float modulo: x % y = x - floor(x/y) * y
    if(left.type().id() == ID_floatbv || right.type().id() == ID_floatbv)
    {
      exprt fl = left, fr = right;
      if(fl.type().id() != ID_floatbv)
        fl = typecast_exprt{fl, double_type()};
      if(fr.type().id() != ID_floatbv)
        fr = typecast_exprt{fr, double_type()};
      // floor(x/y) * y
      exprt quotient = div_exprt{fl, fr};
      // Use if_exprt to implement floor for positive/negative
      exprt truncated = typecast_exprt{
        typecast_exprt{quotient, signedbv_typet{64}}, double_type()};
      exprt floored = if_exprt{
        binary_relation_exprt{quotient, ID_lt, truncated},
        minus_exprt{truncated, double_to_floatbv(1.0)},
        truncated};
      return minus_exprt{fl, mult_exprt{floored, fr}};
    }
    // Non-integer types: return nondet
    return side_effect_expr_nondett{python_int_type(), source_locationt{}};
  }
  else if(op == "Pow")
  {
    // PLR §6.5: The power operator
    // For constant integer exponents, unroll the multiplication.
    // For negative exponents, return 1.0 / (base ** abs(exp)).
    mp_integer exp_val;
    bool exp_known = false;
    if(right.is_constant() && right.type().id() == ID_signedbv)
      exp_known = !to_integer(to_constant_expr(right), exp_val);
    // Handle -N (UnaryOp USub on constant)
    if(
      !exp_known && right.id() == ID_unary_minus &&
      right.operands().size() == 1 && right.operands()[0].is_constant())
    {
      mp_integer pos_val;
      if(!to_integer(to_constant_expr(right.operands()[0]), pos_val))
      {
        exp_val = -pos_val;
        exp_known = true;
      }
    }
    // PLR §6.5: Complex power — not supported as exact expression
    if(is_complex(left.type()))
      return side_effect_expr_nondett{left.type(), source_locationt{}};

    // Constant base and exponent: compute pow() at conversion time
    // Handle typecast(constant) as constant (from int→float promotion)
    auto ev_base = try_eval_double(left);
    auto ev_exp = try_eval_double(right);
    if(ev_base.has_value() && ev_exp.has_value())
    {
      double base_d = ev_base.value(), exp_d = ev_exp.value();
      double result_d = std::pow(base_d, exp_d);
      if(
        right.type().id() == ID_floatbv || exp_d < 0 ||
        exp_d != std::floor(exp_d))
      {
        return double_to_floatbv(result_d);
      }
      return from_integer(
        mp_integer{static_cast<long long>(result_d)}, left.type());
    }

    if(exp_known)
    {
      bool negative = exp_val < 0;
      if(negative)
        exp_val = -exp_val;

      if(exp_val == 0)
        return from_integer(1, left.type());

      // Constant base and exponent: compute at conversion time
      if(left.is_constant() && left.type().id() == ID_signedbv)
      {
        mp_integer base_val;
        if(!to_integer(to_constant_expr(left), base_val))
        {
          mp_integer result_val{1};
          for(mp_integer i = 0; i < exp_val; ++i)
            result_val *= base_val;
          if(negative)
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_integer(result_val);
            ieee_floatt one{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            one.from_integer(1);
            return div_exprt{one.to_expr(), fv.to_expr()};
          }
          return from_integer(result_val, left.type());
        }
      }

      // Unroll: base * base * ... (up to reasonable limit)
      if(exp_val <= 16)
      {
        exprt result = left;
        for(mp_integer i = 1; i < exp_val; ++i)
          result = mult_exprt{result, left};
        if(negative)
        {
          typet ft = double_type();
          ieee_floatt one{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          one.from_integer(1);
          // Use ieee_floatt for exact base conversion
          exprt float_result = result;
          if(result.is_constant() && result.type().id() == ID_signedbv)
          {
            mp_integer rv;
            if(!to_integer(to_constant_expr(result), rv))
            {
              ieee_floatt fv{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              fv.from_integer(rv);
              float_result = fv.to_expr();
            }
            else
              float_result = typecast_exprt{result, ft};
          }
          else
            float_result = typecast_exprt{result, ft};
          return div_exprt{one.to_expr(), float_result};
        }
        return result;
      }
    }
    // Variable exponent: build if-then-else chain for b=0..16
    // (only for scalar types — complex handled above)
    {
      exprt result = from_integer(1, left.type()); // x**0 == 1
      for(int i = 16; i >= 1; i--)
      {
        exprt power = left;
        for(int j = 1; j < i; j++)
          power = mult_exprt{power, left};
        result = if_exprt{
          equal_exprt{right, from_integer(i, right.type())}, power, result};
      }
      return result;
    }
  }
  else if(op == "Div")
  {
    // PLR §6.7: True division always returns float.
    // For constant integer operands, compute exactly with ieee_floatt
    if(
      left.is_constant() && right.is_constant() &&
      left.type().id() == ID_signedbv && right.type().id() == ID_signedbv)
    {
      mp_integer lv, rv;
      if(
        !to_integer(to_constant_expr(left), lv) &&
        !to_integer(to_constant_expr(right), rv) && rv != 0)
      {
        ieee_floatt fl{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        fl.from_integer(lv);
        ieee_floatt fr{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        fr.from_integer(rv);
        fl /= fr;
        return fl.to_expr();
      }
    }
    typet float_type = double_type();
    // Set ZeroDivisionError for division by zero
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
      {
        exprt is_zero = equal_exprt{right, safe_zero(right.type())};
        pending_checks.push_back(code_ifthenelset{
          is_zero,
          code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}}});
        if(exc_type_sym != nullptr)
          pending_checks.push_back(code_ifthenelset{
            is_zero,
            code_frontend_assignt{
              exc_type_sym->symbol_expr(),
              from_integer(
                exception_type_hash("ZeroDivisionError"), python_int_type())}});
      }
    }
    exprt fl = safe_typecast(left, float_type);
    exprt fr = safe_typecast(right, float_type);
    return if_exprt{
      equal_exprt{right, safe_zero(right.type())},
      safe_zero(float_type),
      div_exprt{fl, fr}};
  }
  else if(op == "BitOr" || op == "BitAnd" || op == "BitXor")
  {
    // PLR §6.9: bitwise ops require integer-like operands. For
    // set/list-of-string operands (and mixed-type operands that the
    // set-handling block above did not pick up), a bitvector bit-op
    // is ill-typed and would trip solver invariants; return a typed
    // nondet instead. Visible with --python-strict-warnings.
    const bool left_int =
      left.type().id() == ID_signedbv || left.type().id() == ID_unsignedbv ||
      left.type().id() == ID_integer || left.type().id() == ID_bool;
    const bool right_int =
      right.type().id() == ID_signedbv || right.type().id() == ID_unsignedbv ||
      right.type().id() == ID_integer || right.type().id() == ID_bool;
    if(!left_int || !right_int)
    {
      log_overapprox(
        std::string{"bitwise "} + op +
        " on non-integer operand: using nondet over-approximation");
      // Pick a result type: prefer left's type if it's a struct,
      // else python_int_type().
      const typet &rt =
        (left.type().id() == ID_struct || left.type().id() == ID_struct_tag)
          ? left.type()
          : python_int_type();
      return side_effect_expr_nondett{rt, source_locationt{}};
    }
    // Bitwise ops require bitvectors — cast if using unbounded ints
    if(left.type().id() == ID_integer)
    {
      left = typecast_exprt{left, signedbv_typet{64}};
      right = typecast_exprt{right, signedbv_typet{64}};
    }
    if(op == "BitOr")
      return bitor_exprt{left, right};
    if(op == "BitAnd")
      return bitand_exprt{left, right};
    return bitxor_exprt{left, right};
  }
  else if(op == "LShift")
  {
    if(left.type().id() == ID_integer)
    {
      left = typecast_exprt{left, signedbv_typet{64}};
      right = typecast_exprt{right, signedbv_typet{64}};
    }
    // Negative shift raises ValueError
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      if(exc_sym != nullptr)
      {
        exprt neg =
          binary_relation_exprt{right, ID_lt, from_integer(0, right.type())};
        pending_checks.push_back(code_ifthenelset{
          neg, code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}}});
      }
    }
    return shl_exprt{left, right};
  }
  else if(op == "RShift")
  {
    if(left.type().id() == ID_integer)
    {
      left = typecast_exprt{left, signedbv_typet{64}};
      right = typecast_exprt{right, signedbv_typet{64}};
    }
    // Negative shift raises ValueError
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      if(exc_sym != nullptr)
      {
        exprt neg =
          binary_relation_exprt{right, ID_lt, from_integer(0, right.type())};
        pending_checks.push_back(code_ifthenelset{
          neg, code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}}});
      }
    }
    return ashr_exprt{left, right};
  }
  else if(op == "MatMult")
  {
    // PLR §6.7: matrix multiplication '@'. The Python semantics are
    // defined only in terms of the operands' __matmul__ methods; for
    // primitive types 'x @ y' is not defined. We do not model matrix
    // objects, so we route MatMult through Mult (scalar product),
    // which gives the correct result for scalar operands and a sound
    // over-approximation for anything else.
    if(left.type() == right.type() && left.type().id() != ID_struct)
      return mult_exprt{left, right};
    return side_effect_expr_nondett{left.type(), source_locationt{}};
  }
  else
  {
    log.warning() << "Unsupported binary operator: " << op << messaget::eom;
    return side_effect_expr_nondett{python_int_type(), source_locationt{}};
  }
}

// PLR §6.6: Unary arithmetic and bitwise operations
// "All unary arithmetic and bitwise operations have the same priority."
exprt python_convertert::convert_unary_op(const jsont &expr)
{
  exprt operand = convert_expression(json_member(expr, "operand"));
  std::string op = json_string(json_member(json_member(expr, "op"), "_type"));

  if(operand.is_nil())
    return nil_exprt{};

  // PLR §3.3.8: unary-operator dunder dispatch — if the
  // operand is a user-defined class instance with a
  // matching __<op>__ method, dispatch to it.
  {
    static const std::map<std::string, std::string> op_to_dunder = {
      {"USub", "__neg__"}, {"UAdd", "__pos__"}, {"Invert", "__invert__"}};
    auto du = op_to_dunder.find(op);
    if(du != op_to_dunder.end())
    {
      std::string tag;
      if(operand.type().id() == ID_struct)
        tag = id2string(to_struct_type(operand.type()).get_tag());
      else if(operand.type().id() == ID_struct_tag)
        tag = id2string(to_struct_tag_type(operand.type()).get_identifier());
      if(tag.substr(0, 13) == "python_class_")
      {
        std::string bare = tag.substr(13);
        for(const std::string &prefix :
            {std::string{"python::"} + tag + "::" + du->second,
             std::string{"python::"} + bare + "::" + du->second})
        {
          const symbolt *ds = symbol_table.lookup(irep_idt{prefix});
          if(ds != nullptr)
          {
            typet ret_type = operand.type();
            if(ds->type.id() == ID_code)
              ret_type = to_code_type(ds->type).return_type();
            return side_effect_expr_function_callt{
              ds->symbol_expr(),
              {address_of_exprt{operand}},
              ret_type,
              source_locationt{}};
          }
        }
      }
    }
  }

  // Unwrap tagged-union values
  if(is_python_value_type(operand.type()))
    operand = unwrap_value(operand, python_int_type());

  if(op == "USub")
  {
    if(
      operand.type().id() == ID_struct &&
      to_struct_type(operand.type()).get_tag() == "python_complex")
    {
      return struct_exprt{
        {unary_minus_exprt{member_exprt{operand, "real", double_type()}},
         unary_minus_exprt{member_exprt{operand, "imag", double_type()}}},
        operand.type()};
    }
    return unary_minus_exprt{operand};
  }
  else if(op == "UAdd")
    return operand;
  else if(op == "Not")
    return not_exprt{safe_typecast(operand, bool_typet{})};
  else if(op == "Invert")
  {
    // PLR §6.7: bitwise ~x. Promote bool to int, then bitnot.
    exprt cast_operand = operand.type().id() == ID_bool
                           ? safe_typecast(operand, python_int_type())
                           : operand;
    return bitnot_exprt{cast_operand};
  }
  else
  {
    log.warning() << "Unsupported unary operator: " << op << messaget::eom;
    return side_effect_expr_nondett{python_int_type(), source_locationt{}};
  }
}

// PLR §6.11: Boolean operations
// "x or y: if x is true, then x, else y"
// "x and y: if x is false, then x, else y"
exprt python_convertert::convert_bool_op(const jsont &expr)
{
  std::string op = json_string(json_member(json_member(expr, "op"), "_type"));
  const jsont &values = json_member(expr, "values");

  if(!values.is_array() || as_array(values).size() < 2)
  {
    log.error() << "BoolOp requires at least 2 operands" << messaget::eom;
    return nil_exprt{};
  }

  exprt result = convert_expression(*as_array(values).begin());
  auto it = std::next(as_array(values).begin());
  for(; it != as_array(values).end(); ++it)
  {
    exprt next = convert_expression(*it);
    if(result.is_nil() || next.is_nil())
      return nil_exprt{};

    // PLR §6.11: "x or y" returns x if x is truthy, else y
    // "x and y" returns x if x is falsy, else y
    if(op == "And")
    {
      exprt cond = safe_typecast(result, bool_typet{});
      // If result is falsy, return result; else return next
      if(result.type() == next.type())
        result = if_exprt{cond, next, result};
      else
        result = and_exprt{cond, safe_typecast(next, bool_typet{})};
    }
    else if(op == "Or")
    {
      exprt cond = safe_typecast(result, bool_typet{});
      // If result is truthy, return result; else return next
      if(result.type() == next.type())
        result = if_exprt{cond, result, next};
      else
        result = or_exprt{cond, safe_typecast(next, bool_typet{})};
    }
    else
    {
      log.warning() << "Unsupported bool operator: " << op << messaget::eom;
      return side_effect_expr_nondett{bool_typet{}, source_locationt{}};
    }
  }

  return result;
}
