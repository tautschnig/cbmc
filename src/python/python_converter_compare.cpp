/// Python to GOTO converter — Compare (PLR §6.10) implementation.
///
/// Extracted from python_converter.cpp to reduce the main file size.
/// All logic and class-member state remains unchanged — this file is
/// a pure source-split.

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/floatbv_expr.h>
#include <util/json.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

exprt python_convertert::convert_compare(const jsont &expr)
{
  exprt left = convert_expression(json_member(expr, "left"));
  const jsont &ops = json_member(expr, "ops");
  const jsont &comparators = json_member(expr, "comparators");

  if(
    !ops.is_array() || !comparators.is_array() || as_array(ops).empty() ||
    as_array(comparators).empty())
  {
    log.error() << "Malformed Compare node" << messaget::eom;
    return nil_exprt{};
  }

  // Handle chained comparisons: a < b < c → (a < b) and (b < c)
  exprt result = nil_exprt{};
  exprt current_left = left;

  auto ops_it = as_array(ops).begin();
  auto comp_it = as_array(comparators).begin();
  for(; ops_it != as_array(ops).end(); ++ops_it, ++comp_it)
  {
    std::string op = json_string(json_member(*ops_it, "_type"));
    exprt right = convert_expression(*comp_it);

    if(current_left.is_nil() || right.is_nil())
      return nil_exprt{};

    // PLR §6.10.1: chained-comparison single-evaluation.
    // If there is another comparator after this one, the
    // current right operand becomes the next left operand,
    // so materialise its side effects into a tmp before
    // using it for both this comparison and the next.
    auto peek_next = ops_it;
    ++peek_next;
    if(peek_next != as_array(ops).end())
    {
      static unsigned chain_snap_ctr = 0;
      std::string tmpn = "__chained_" + std::to_string(chain_snap_ctr++);
      std::string tmpq = qualify_name(tmpn);
      irep_idt tmpid{tmpq};
      if(symbol_table.lookup(tmpid) == nullptr)
      {
        symbolt ts{tmpid, right.type(), "python"};
        ts.base_name = tmpn;
        ts.is_lvalue = true;
        ts.is_state_var = true;
        ts.is_static_lifetime = current_function.empty();
        symbol_table.add(ts);
      }
      symbol_exprt snap = symbol_table.lookup_ref(tmpid).symbol_expr();
      pending_checks.push_back(code_frontend_assignt{snap, right});
      right = snap;
    }

    // Unwrap tagged-union values (skip for In/NotIn — container stays wrapped)
    if(op != "In" && op != "NotIn")
    {
      if(is_python_value_type(current_left.type()))
        current_left = unwrap_value(current_left, right.type());
      if(is_python_value_type(right.type()))
        right = unwrap_value(right, current_left.type());
    }
    else if(
      is_python_value_type(current_left.type()) &&
      !is_python_value_type(right.type()))
    {
      // For "x in lst": unwrap x but keep lst
      current_left = unwrap_value(current_left, right.type());
    }

    // Type promotion for comparisons (skip for In/NotIn/Is/IsNot)
    if(
      current_left.type() != right.type() && op != "In" && op != "NotIn" &&
      op != "Is" && op != "IsNot")
    {
      // Complex promotion: promote numeric to complex(val, 0.0)
      auto is_complex = [](const typet &t)
      {
        return t.id() == ID_struct &&
               to_struct_type(t).get_tag() == "python_complex";
      };
      if(is_complex(current_left.type()) && !is_complex(right.type()))
      {
        exprt r_float = right;
        if(right.type().id() != ID_floatbv)
          r_float = safe_typecast(right, double_type());
        struct_typet ct{
          {struct_typet::componentt{"real", double_type()},
           struct_typet::componentt{"imag", double_type()}}};
        ct.set_tag("python_complex");
        right = struct_exprt{{r_float, safe_zero(double_type())}, ct};
      }
      else if(is_complex(right.type()) && !is_complex(current_left.type()))
      {
        exprt l_float = current_left;
        if(current_left.type().id() != ID_floatbv)
          l_float = safe_typecast(current_left, double_type());
        struct_typet ct{
          {struct_typet::componentt{"real", double_type()},
           struct_typet::componentt{"imag", double_type()}}};
        ct.set_tag("python_complex");
        current_left = struct_exprt{{l_float, safe_zero(double_type())}, ct};
      }
      // PLR §6.10.1: string vs numeric → never equal
      else if(
        (is_python_string_type(current_left.type()) !=
         is_python_string_type(right.type())) &&
        !is_python_value_type(current_left.type()) &&
        !is_python_value_type(right.type()))
      {
        if(is_python_string_type(current_left.type()))
          right = python_string_literal("__NEVER_EQUAL__");
        else
          current_left = python_string_literal("__NEVER_EQUAL__");
      }
      // PLR §6.10.1: lists with different element types are never equal.
      // Two lists of different shapes (e.g. list[int] vs list[list[int]])
      // pass through here as struct expressions whose underlying widths
      // differ. Falling through to a plain equal_exprt would let
      // boolbv::convert_bv_typecast try to widen/narrow one side to fit
      // the other, which crashes on incompatible element types
      // (regression test: assert [[1]] == [1]). Force the comparison to
      // statically false by re-using the existing "__NEVER_EQUAL__"
      // string-literal trick.
      else if(
        is_python_list_type(current_left.type()) &&
        is_python_list_type(right.type()) &&
        current_left.type() != right.type())
      {
        current_left = python_string_literal("__NEVER_EQUAL__");
        right = python_string_literal("__NOT_EQUAL_TO_THIS__");
      }
      // PLR §6.10.1: list vs non-list (excluding python_value tagged
      // unions, where the comparison stays dynamic) → never equal.
      else if(
        (is_python_list_type(current_left.type()) !=
         is_python_list_type(right.type())) &&
        !is_python_value_type(current_left.type()) &&
        !is_python_value_type(right.type()))
      {
        current_left = python_string_literal("__NEVER_EQUAL__");
        right = python_string_literal("__NOT_EQUAL_TO_THIS__");
      }
      else if(current_left.type().id() == ID_floatbv)
      {
        // If right is an int constant, convert it exactly to float
        if(right.is_constant() && right.type().id() == ID_signedbv)
          right = safe_typecast(right, current_left.type());
        else
          right = safe_typecast(right, current_left.type());
      }
      else if(right.type().id() == ID_floatbv)
      {
        // If left is an int constant, convert it exactly to float
        if(
          current_left.is_constant() && current_left.type().id() == ID_signedbv)
          current_left = safe_typecast(current_left, right.type());
        // If left is numeric and right is a float constant representable
        // as int, cast to int for exact comparison
        else if(
          right.is_constant() && (current_left.type().id() == ID_signedbv ||
                                  current_left.type().id() == ID_integer))
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(right));
          mp_integer iv = fv.to_integer();
          ieee_floatt check{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          check.from_integer(iv);
          if(fv == check)
            right = from_integer(iv, current_left.type());
          else
            current_left = safe_typecast(current_left, right.type());
        }
        else
          current_left = safe_typecast(current_left, right.type());
      }
      else if(
        is_python_value_type(current_left.type()) &&
        !is_python_value_type(right.type()))
        // Unwrap tagged union to match concrete type
        current_left = unwrap_value(current_left, right.type());
      else if(
        is_python_value_type(right.type()) &&
        !is_python_value_type(current_left.type()))
        right = unwrap_value(right, current_left.type());
      else
        // General case: cast right to left's type
        right = safe_typecast(right, current_left.type());
    }

    exprt cmp;

    // String ordering: compare first characters of data arrays
    if(
      is_python_string_type(current_left.type()) &&
      is_python_string_type(right.type()) &&
      (op == "Lt" || op == "LtE" || op == "Gt" || op == "GtE"))
    {
      // Constant-string optimization for ordering
      {
        auto lv = extract_string_value(current_left);
        if(!lv.has_value() && current_left.id() == ID_symbol)
        {
          auto it = string_constants.find(
            to_symbol_expr(current_left).get_identifier());
          if(it != string_constants.end())
            lv = it->second;
        }
        auto rv = extract_string_value(right);
        if(!rv.has_value() && right.id() == ID_symbol)
        {
          auto it =
            string_constants.find(to_symbol_expr(right).get_identifier());
          if(it != string_constants.end())
            rv = it->second;
        }
        if(lv.has_value() && rv.has_value())
        {
          bool result = false;
          if(op == "Lt")
            result = lv.value() < rv.value();
          else if(op == "LtE")
            result = lv.value() <= rv.value();
          else if(op == "Gt")
            result = lv.value() > rv.value();
          else if(op == "GtE")
            result = lv.value() >= rv.value();
          return result ? exprt(true_exprt()) : exprt(false_exprt());
        }
      }
      return side_effect_expr_nondett{bool_typet(), source_locationt{}};
      typet str_type = python_string_type();
      const auto &data_type = array_typet(
        unsignedbv_typet{8},
        from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
      exprt left_char = index_exprt{
        member_exprt{current_left, "data", data_type},
        from_integer(0, python_int_type())};
      exprt right_char = index_exprt{
        member_exprt{right, "data", data_type},
        from_integer(0, python_int_type())};

      if(op == "Lt")
        cmp = binary_relation_exprt{left_char, ID_lt, right_char};
      else if(op == "LtE")
        cmp = binary_relation_exprt{left_char, ID_le, right_char};
      else if(op == "Gt")
        cmp = binary_relation_exprt{left_char, ID_gt, right_char};
      else
        cmp = binary_relation_exprt{left_char, ID_ge, right_char};
    }
    else if(op == "Eq")
    {
      // PLR §3.3.1 __eq__: if the class defines __eq__, use it
      // in preference to structural equality.
      if(
        current_left.type().id() == ID_struct && right.type().id() == ID_struct)
      {
        const auto &eq_st = to_struct_type(current_left.type());
        std::string eq_tag = id2string(eq_st.get_tag());
        if(eq_tag.substr(0, 13) == "python_class_")
        {
          std::string cls = eq_tag.substr(13);
          irep_idt mid{"python::" + cls + "::__eq__"};
          const symbolt *msym = symbol_table.lookup(mid);
          if(msym != nullptr && msym->type.id() == ID_code)
          {
            const code_typet &mty = to_code_type(msym->type);
            exprt self_ptr = address_of_exprt{current_left};
            exprt other_arg = right;
            if(
              mty.parameters().size() >= 2 &&
              other_arg.type() != mty.parameters()[1].type())
              other_arg = safe_typecast(other_arg, mty.parameters()[1].type());
            side_effect_expr_function_callt call{
              msym->symbol_expr(),
              {self_ptr, std::move(other_arg)},
              mty.return_type(),
              get_location(expr)};
            cmp = std::move(call);
            goto done_cmp;
          }
        }
      }
      if(
        is_python_set_type(current_left.type()) &&
        is_python_set_type(right.type()))
      {
        cmp = and_exprt{
          equal_exprt{
            member_exprt{current_left, "bitmap", unsignedbv_typet{64}},
            member_exprt{right, "bitmap", unsignedbv_typet{64}}},
          equal_exprt{
            member_exprt{current_left, "offset", signedbv_typet{64}},
            member_exprt{right, "offset", signedbv_typet{64}}}};
      }
      else
      {
        if(current_left.type() != right.type())
          right = safe_typecast(right, current_left.type());
        // Complex equality: compare components with ieee_float_equal
        if(
          current_left.type().id() == ID_struct &&
          to_struct_type(current_left.type()).get_tag() == "python_complex" &&
          right.type().id() == ID_struct &&
          to_struct_type(right.type()).get_tag() == "python_complex")
        {
          cmp = and_exprt{
            ieee_float_equal_exprt{
              member_exprt{current_left, "real", double_type()},
              member_exprt{right, "real", double_type()}},
            ieee_float_equal_exprt{
              member_exprt{current_left, "imag", double_type()},
              member_exprt{right, "imag", double_type()}}};
        }
        // Constant-string equality: resolve at conversion time
        else if(
          is_python_string_type(current_left.type()) &&
          is_python_string_type(right.type()))
        {
          auto lv = extract_string_value(current_left);
          if(!lv.has_value() && current_left.id() == ID_symbol)
          {
            auto it = string_constants.find(
              to_symbol_expr(current_left).get_identifier());
            if(it != string_constants.end())
              lv = it->second;
          }
          auto rv = extract_string_value(right);
          if(!rv.has_value() && right.id() == ID_symbol)
          {
            auto it =
              string_constants.find(to_symbol_expr(right).get_identifier());
            if(it != string_constants.end())
              rv = it->second;
          }
          if(lv.has_value() && rv.has_value())
          {
            cmp = lv.value() == rv.value() ? exprt{true_exprt{}}
                                           : exprt{false_exprt{}};
            goto done_cmp;
          }
          // Use string solver for content equality
          {
            auto to_str = [](const exprt &s) -> exprt
            {
              if(s.id() == ID_struct && s.operands().size() == 2)
                return s;
              return struct_exprt(
                {member_exprt(s, "length", signedbv_typet{64}),
                 member_exprt(
                   s, "data", pointer_typet(unsignedbv_typet{8}, 64))},
                s.type());
            };
            cmp = emit_string_bool_function(
              ID_cprover_string_equal_func,
              to_str(current_left),
              to_str(right),
              symbol_table,
              pending_checks);
            goto done_cmp;
          }
        }
        else if(current_left.type().id() == ID_floatbv)
          cmp = ieee_float_equal_exprt{current_left, right};
        else
          cmp = equal_exprt{current_left, right};
      }
    }
    else if(op == "NotEq")
    {
      if(
        is_python_set_type(current_left.type()) &&
        is_python_set_type(right.type()))
      {
        cmp = or_exprt{
          notequal_exprt{
            member_exprt{current_left, "bitmap", unsignedbv_typet{64}},
            member_exprt{right, "bitmap", unsignedbv_typet{64}}},
          notequal_exprt{
            member_exprt{current_left, "offset", signedbv_typet{64}},
            member_exprt{right, "offset", signedbv_typet{64}}}};
      }
      else
      {
        if(current_left.type() != right.type())
          right = safe_typecast(right, current_left.type());
        if(
          is_python_string_type(current_left.type()) &&
          is_python_string_type(right.type()))
        {
          auto lv = extract_string_value(current_left);
          if(!lv.has_value() && current_left.id() == ID_symbol)
          {
            auto it = string_constants.find(
              to_symbol_expr(current_left).get_identifier());
            if(it != string_constants.end())
              lv = it->second;
          }
          auto rv = extract_string_value(right);
          if(!rv.has_value() && right.id() == ID_symbol)
          {
            auto it =
              string_constants.find(to_symbol_expr(right).get_identifier());
            if(it != string_constants.end())
              rv = it->second;
          }
          if(lv.has_value() && rv.has_value())
          {
            cmp = lv.value() != rv.value() ? exprt{true_exprt{}}
                                           : exprt{false_exprt{}};
            goto done_cmp;
          }
          // Use string solver for content inequality
          {
            auto to_str = [](const exprt &s) -> exprt
            {
              if(s.id() == ID_struct && s.operands().size() == 2)
                return s;
              return struct_exprt(
                {member_exprt(s, "length", signedbv_typet{64}),
                 member_exprt(
                   s, "data", pointer_typet(unsignedbv_typet{8}, 64))},
                s.type());
            };
            cmp = not_exprt{emit_string_bool_function(
              ID_cprover_string_equal_func,
              to_str(current_left),
              to_str(right),
              symbol_table,
              pending_checks)};
            goto done_cmp;
          }
        }
        else if(
          current_left.type().id() == ID_struct &&
          to_struct_type(current_left.type()).get_tag() == "python_complex")
        {
          cmp = or_exprt{
            ieee_float_notequal_exprt{
              member_exprt{current_left, "real", double_type()},
              member_exprt{right, "real", double_type()}},
            ieee_float_notequal_exprt{
              member_exprt{current_left, "imag", double_type()},
              member_exprt{right, "imag", double_type()}}};
        }
        else if(current_left.type().id() == ID_floatbv)
          cmp = ieee_float_notequal_exprt{current_left, right};
        else
          cmp = notequal_exprt{current_left, right};
      }
    }
    else if(op == "Lt" || op == "LtE" || op == "Gt" || op == "GtE")
    {
      // PLR §3.3.8 Emulating numeric types / §3.3.1 ordering:
      // if the struct class defines __lt__ / __le__ / __gt__
      // / __ge__, route through it. PLR also requires a
      // 'reflected' fallback: when left's method is absent
      // but right's reflected method exists (e.g. left __lt__
      // missing, right __gt__ present), dispatch to the
      // right operand with arguments reversed.
      if(
        current_left.type().id() == ID_struct && right.type().id() == ID_struct)
      {
        const auto &lt_st = to_struct_type(current_left.type());
        std::string lt_tag = id2string(lt_st.get_tag());
        const auto &rt_st = to_struct_type(right.type());
        std::string rt_tag = id2string(rt_st.get_tag());
        if(lt_tag.substr(0, 13) == "python_class_")
        {
          std::string cls = lt_tag.substr(13);
          std::string mname = op == "Lt"    ? "__lt__"
                              : op == "LtE" ? "__le__"
                              : op == "Gt"  ? "__gt__"
                                            : "__ge__";
          irep_idt mid{"python::" + cls + "::" + mname};
          const symbolt *msym = symbol_table.lookup(mid);
          if(msym != nullptr && msym->type.id() == ID_code)
          {
            const code_typet &mty = to_code_type(msym->type);
            // Build the call: method(&left, right)
            exprt self_ptr = address_of_exprt{current_left};
            exprt other_arg = right;
            if(
              mty.parameters().size() >= 2 &&
              other_arg.type() != mty.parameters()[1].type())
              other_arg = safe_typecast(other_arg, mty.parameters()[1].type());
            side_effect_expr_function_callt call{
              msym->symbol_expr(),
              {self_ptr, std::move(other_arg)},
              mty.return_type(),
              get_location(expr)};
            cmp = std::move(call);
            goto done_cmp;
          }
        }
        // Left has no matching dunder — try right's reflected.
        //   left <  right  →  right >  left   (__gt__ on right)
        //   left <= right  →  right >= left   (__ge__ on right)
        //   left >  right  →  right <  left   (__lt__ on right)
        //   left >= right  →  right <= left   (__le__ on right)
        if(rt_tag.substr(0, 13) == "python_class_")
        {
          std::string rcls = rt_tag.substr(13);
          std::string rname = op == "Lt"    ? "__gt__"
                              : op == "LtE" ? "__ge__"
                              : op == "Gt"  ? "__lt__"
                                            : "__le__";
          irep_idt rmid{"python::" + rcls + "::" + rname};
          const symbolt *rmsym = symbol_table.lookup(rmid);
          if(rmsym != nullptr && rmsym->type.id() == ID_code)
          {
            const code_typet &rmty = to_code_type(rmsym->type);
            exprt self_ptr = address_of_exprt{right};
            exprt other_arg = current_left;
            if(
              rmty.parameters().size() >= 2 &&
              other_arg.type() != rmty.parameters()[1].type())
              other_arg = safe_typecast(other_arg, rmty.parameters()[1].type());
            side_effect_expr_function_callt call{
              rmsym->symbol_expr(),
              {self_ptr, std::move(other_arg)},
              rmty.return_type(),
              get_location(expr)};
            cmp = std::move(call);
            goto done_cmp;
          }
        }
      }
      if(current_left.type() != right.type())
        right = safe_typecast(right, current_left.type());
      irep_idt rel_id = op == "Lt"    ? ID_lt
                        : op == "LtE" ? ID_le
                        : op == "Gt"  ? ID_gt
                                      : ID_ge;
      cmp = binary_relation_exprt{current_left, rel_id, right};
    }
    else if(op == "In" || op == "NotIn")
    {
      // Set membership: x in s → (s.bitmap >> x) & 1
      exprt container = right;
      exprt item = current_left;
      // PLR §3.3.1: custom __contains__ dunder — if the
      // container is a user-defined class instance with a
      // __contains__ method, dispatch to it.
      {
        std::string tag;
        if(container.type().id() == ID_struct)
          tag = id2string(to_struct_type(container.type()).get_tag());
        else if(container.type().id() == ID_struct_tag)
          tag =
            id2string(to_struct_tag_type(container.type()).get_identifier());
        if(!tag.empty())
        {
          // Class tag is python_class_X for user classes; look up
          // under both python::python_class_X::__contains__ and
          // python::X::__contains__ for robustness.
          std::string bare =
            tag.substr(0, 13) == "python_class_" ? tag.substr(13) : tag;
          for(const std::string &prefix :
              {std::string{"python::"} + tag + "::__contains__",
               std::string{"python::"} + bare + "::__contains__"})
          {
            const symbolt *cs = symbol_table.lookup(irep_idt{prefix});
            if(cs != nullptr)
            {
              side_effect_expr_function_callt call{
                cs->symbol_expr(),
                {address_of_exprt{container}, item},
                bool_typet{},
                source_locationt{}};
              cmp = (op == "In") ? exprt{call} : exprt{not_exprt{call}};
              goto done_cmp;
            }
          }
        }
      }
      if(is_python_value_type(container.type()))
      {
        // Try to detect if it's a set by checking the tag
        // For now, only handle concrete set types
      }
      if(is_python_set_type(container.type()))
      {
        member_exprt bm{container, "bitmap", unsignedbv_typet{64}};
        member_exprt off{container, "offset", signedbv_typet{64}};
        exprt shifted_item = item;
        if(shifted_item.type() != signedbv_typet{64})
          shifted_item = safe_typecast(shifted_item, signedbv_typet{64});
        // Cast to unsigned for shift
        exprt shift_amount = typecast_exprt{shifted_item, unsignedbv_typet{64}};
        exprt shifted = lshr_exprt{bm, shift_amount};
        exprt bit =
          bitand_exprt{shifted, from_integer(1, unsignedbv_typet{64})};
        exprt in_set =
          notequal_exprt{bit, from_integer(0, unsignedbv_typet{64})};
        cmp = (op == "In") ? in_set : exprt{not_exprt{in_set}};
      }
      // x in lst → disjunction: lst.data[0]==x or lst.data[1]==x or ...
      else if(is_python_list_type(container.type()))
      {
        // Constant-string optimization: resolve at conversion time
        auto item_str = extract_string_value(item);
        if(
          item_str.has_value() &&
          is_python_string_type(
            to_array_type(
              to_struct_type(container.type()).components()[1].type())
              .element_type()))
        {
          const exprt *list_val = &container;
          if(container.id() == ID_symbol)
          {
            auto it =
              list_literals.find(to_symbol_expr(container).get_identifier());
            if(it != list_literals.end())
              list_val = &it->second;
          }
          if(
            list_val->id() == ID_struct && list_val->operands().size() >= 2 &&
            list_val->operands()[0].is_constant())
          {
            mp_integer len_val;
            if(!to_integer(to_constant_expr(list_val->operands()[0]), len_val))
            {
              const exprt &data_arr = list_val->operands()[1];
              bool found = false;
              for(mp_integer i = 0; i < len_val; ++i)
              {
                auto idx = i.to_ulong();
                if(idx < data_arr.operands().size())
                {
                  auto ev = extract_string_value(data_arr.operands()[idx]);
                  if(ev.has_value() && ev.value() == item_str.value())
                  {
                    found = true;
                    break;
                  }
                }
              }
              cmp = (op == "In")
                      ? (found ? exprt{true_exprt{}} : exprt{false_exprt{}})
                      : (found ? exprt{false_exprt{}} : exprt{true_exprt{}});
              goto done_cmp;
            }
          }
        }

        const auto &list_st = to_struct_type(container.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        member_exprt data{container, "data", data_type};
        member_exprt length{container, "length", signedbv_typet{64}};

        // Build disjunction for up to PYTHON_MAX_LIST_LENGTH elements
        // guarded by index < length
        exprt in_expr = false_exprt{};
        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          exprt elem = index_exprt{data, idx};
          // Ensure types match for equality comparison
          if(current_left.type() != elem.type())
            elem = safe_typecast(elem, current_left.type());
          exprt match = equal_exprt{current_left, elem};
          exprt in_range = binary_relation_exprt{idx, ID_lt, length};
          in_expr = or_exprt{in_expr, and_exprt{in_range, match}};
        }
        cmp = (op == "In") ? in_expr : not_exprt{in_expr};
      }
      else if(is_python_string_type(container.type()))
      {
        // PLR §6.10.2: "x in s" for strings — check character membership
        // Constant-string optimization
        {
          auto container_sv = extract_string_value(container);
          if(!container_sv.has_value() && container.id() == ID_symbol)
          {
            auto it =
              string_constants.find(to_symbol_expr(container).get_identifier());
            if(it != string_constants.end())
              container_sv = it->second;
          }
          auto item_sv = extract_string_value(item);
          if(!item_sv.has_value() && item.id() == ID_symbol)
          {
            auto it =
              string_constants.find(to_symbol_expr(item).get_identifier());
            if(it != string_constants.end())
              item_sv = it->second;
          }
          if(container_sv.has_value() && item_sv.has_value())
          {
            bool found =
              container_sv.value().find(item_sv.value()) != std::string::npos;
            cmp = (op == "In")
                    ? (found ? exprt{true_exprt{}} : exprt{false_exprt{}})
                    : (found ? exprt{false_exprt{}} : exprt{true_exprt{}});
            goto done_cmp;
          }
        }
        // Use string solver for non-constant string 'in' operator
        {
          exprt contains = emit_string_bool_function(
            ID_cprover_string_contains_func,
            container,
            item,
            symbol_table,
            pending_checks);
          cmp = (op == "In") ? contains : exprt(not_exprt{contains});
        }
        goto done_cmp;
        const auto &data_type = array_typet(
          unsignedbv_typet{8},
          from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
        member_exprt data{container, "data", data_type};
        member_exprt length{container, "length", signedbv_typet{64}};

        exprt search_byte;
        if(is_python_string_type(item.type()))
          search_byte = index_exprt{
            member_exprt{item, "data", data_type},
            from_integer(0, signedbv_typet{64})};
        else
          search_byte = safe_typecast(item, unsignedbv_typet{8});

        exprt in_expr = false_exprt{};
        for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          exprt elem = index_exprt{data, idx};
          exprt match = equal_exprt{search_byte, elem};
          exprt in_range = binary_relation_exprt{idx, ID_lt, length};
          in_expr = or_exprt{in_expr, and_exprt{in_range, match}};
        }
        cmp = (op == "In") ? in_expr : not_exprt{in_expr};
      }
      else if(is_python_value_type(container.type()))
      {
        exprt list_val = python_value_list(container);
        if(is_python_list_type(list_val.type()))
        {
          const auto &list_st = to_struct_type(list_val.type());
          const auto &data_type = to_array_type(list_st.components()[1].type());
          member_exprt list_len{list_val, "length", signedbv_typet{64}};
          member_exprt list_data{list_val, "data", data_type};
          exprt in_expr = false_exprt{};
          for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, list_len};
            exprt elem = index_exprt{list_data, idx};
            exprt unwrapped = unwrap_value(elem, current_left.type());
            exprt cmp_left = current_left;
            if(is_python_value_type(cmp_left.type()))
              cmp_left = unwrap_value(cmp_left, python_int_type());
            if(is_python_value_type(unwrapped.type()))
              unwrapped = unwrap_value(unwrapped, cmp_left.type());
            if(cmp_left.type() != unwrapped.type())
              unwrapped = safe_typecast(unwrapped, cmp_left.type());
            exprt match = equal_exprt{cmp_left, unwrapped};
            in_expr = or_exprt{in_expr, and_exprt{in_range, match}};
          }
          cmp = (op == "In") ? in_expr : not_exprt{in_expr};
        }
        else
          cmp = (op == "In") ? exprt{false_exprt{}} : exprt{true_exprt{}};
      }
      else if(is_python_dict_type(container.type()))
      {
        // key in dict: constant-key optimization
        auto key_str = extract_string_value(item);
        const exprt *dict_val = &container;
        if(container.id() == ID_symbol)
        {
          auto it =
            dict_literals.find(to_symbol_expr(container).get_identifier());
          if(it != dict_literals.end())
            dict_val = &it->second;
        }
        if(
          key_str.has_value() && dict_val->id() == ID_struct &&
          dict_val->operands().size() >= 2 &&
          dict_val->operands()[0].is_constant())
        {
          mp_integer len_val;
          if(!to_integer(to_constant_expr(dict_val->operands()[0]), len_val))
          {
            const exprt &keys_arr = dict_val->operands()[1];
            bool found = false;
            for(mp_integer i = 0; i < len_val; ++i)
            {
              auto idx = i.to_ulong();
              if(idx < keys_arr.operands().size())
              {
                auto kv = extract_string_value(keys_arr.operands()[idx]);
                if(kv.has_value() && kv.value() == key_str.value())
                {
                  found = true;
                  break;
                }
              }
            }
            cmp = (op == "In")
                    ? (found ? exprt{true_exprt{}} : exprt{false_exprt{}})
                    : (found ? exprt{false_exprt{}} : exprt{true_exprt{}});
          }
          else
            goto dict_in_symbolic;
        }
        else
        {
        dict_in_symbolic:
          // Symbolic dict 'in': iterate keys and compare
          const auto &dict_st = to_struct_type(container.type());
          const auto &keys_type = to_array_type(dict_st.components()[1].type());
          member_exprt length{container, "length", signedbv_typet{64}};
          member_exprt keys{container, "keys", keys_type};
          exprt in_expr = false_exprt{};
          for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, length};
            exprt key_i = index_exprt{keys, idx};
            exprt match;
            if(
              is_python_string_type(item.type()) &&
              is_python_string_type(key_i.type()))
            {
              // Use string solver for key comparison
              auto to_str = [](const exprt &s) -> exprt
              {
                if(s.id() == ID_struct && s.operands().size() == 2)
                  return s;
                return struct_exprt(
                  {member_exprt(s, "length", signedbv_typet{64}),
                   member_exprt(
                     s, "data", pointer_typet(unsignedbv_typet{8}, 64))},
                  s.type());
              };
              match = emit_string_bool_function(
                ID_cprover_string_equal_func,
                to_str(item),
                to_str(key_i),
                symbol_table,
                pending_checks);
            }
            else
            {
              if(item.type() == key_i.type())
                match = equal_exprt{item, key_i};
              else
                match = false_exprt{}; // type mismatch → not equal
            }
            in_expr = or_exprt{in_expr, and_exprt{in_range, match}};
          }
          cmp = (op == "In") ? in_expr : not_exprt{in_expr};
        }
      }
      else if(is_python_tuple_type(container.type()))
      {
        // x in (a, b, c) → x==a or x==b or x==c
        const auto &st = to_struct_type(container.type());
        exprt in_expr = false_exprt{};
        for(const auto &comp : st.components())
        {
          exprt elem = member_exprt{container, comp.get_name(), comp.type()};
          if(item.type() != elem.type())
            elem = safe_typecast(elem, item.type());
          in_expr = or_exprt{in_expr, equal_exprt{item, elem}};
        }
        cmp = (op == "In") ? in_expr : exprt{not_exprt{in_expr}};
      }
      else if(container.type().id() == ID_struct)
      {
        std::string tag = id2string(to_struct_type(container.type()).get_tag());
        std::string cls =
          tag.substr(0, 13) == "python_class_" ? tag.substr(13) : tag;
        irep_idt cid{"python::" + cls + "::__contains__"};
        const symbolt *csym = symbol_table.lookup(cid);
        if(csym != nullptr)
        {
          exprt call = side_effect_expr_function_callt{
            csym->symbol_expr(),
            {address_of_exprt{container}, item},
            bool_typet{},
            get_location(expr)};
          cmp = (op == "In") ? call : exprt{not_exprt{call}};
        }
        else
          cmp = (op == "In") ? exprt{false_exprt{}} : exprt{true_exprt{}};
      }
      else
      {
        log_overapprox(
          "'in' operator on unsupported container: returning constant");
        cmp = (op == "In") ? exprt{false_exprt{}} : exprt{true_exprt{}};
      }
    }
    else if(op == "Is")
    {
      // PLR §6.10.3: Identity comparison
      // For tagged unions, "x is None" checks tag == NONE
      if(
        is_python_value_type(current_left.type()) && right.is_constant() &&
        right.type().id() == ID_signedbv)
      {
        mp_integer rv;
        if(
          !to_integer(to_constant_expr(right), rv) &&
          rv == mp_integer{-4611686018427387904LL})
        {
          cmp = python_value_is(current_left, python_type_tagt::NONE);
          goto done_cmp;
        }
      }
      if(
        is_python_value_type(right.type()) && current_left.is_constant() &&
        current_left.type().id() == ID_signedbv)
      {
        mp_integer lv;
        if(
          !to_integer(to_constant_expr(current_left), lv) &&
          lv == mp_integer{-4611686018427387904LL})
        {
          cmp = python_value_is(right, python_type_tagt::NONE);
          goto done_cmp;
        }
      }
      // Concrete struct instance compared with None sentinel is
      // always false — the struct is never the None value.
      if(
        current_left.type().id() == ID_struct && right.is_constant() &&
        right.type().id() == ID_signedbv)
      {
        mp_integer rv;
        if(
          !to_integer(to_constant_expr(right), rv) &&
          rv == mp_integer{-4611686018427387904LL})
        {
          cmp = false_exprt{};
          goto done_cmp;
        }
      }
      // PLR §6.10.3: lists, dicts, sets, and class instances are
      // distinct heap objects per construction. Two list/dict
      // values referenced through different *names* are different
      // objects unless one was assigned from the other. We don't
      // track aliasing, so this approximation breaks Python's
      // `z = y` aliasing idiom — but on net resolves more
      // soundness gaps (e.g. `[1,2,3] is [1,2,3]` correctly
      // False) than it introduces (`y = x; assert y is x`).
      // Same-symbol comparisons stay True, literal-vs-anything is
      // False, and different-symbol cases are False.
      if(
        (is_python_list_type(current_left.type()) ||
         is_python_dict_type(current_left.type())) &&
        (is_python_list_type(right.type()) ||
         is_python_dict_type(right.type())))
      {
        bool same_id = current_left.id() == ID_symbol &&
                       right.id() == ID_symbol &&
                       to_symbol_expr(current_left).get_identifier() ==
                         to_symbol_expr(right).get_identifier();
        cmp = same_id ? exprt{true_exprt{}} : exprt{false_exprt{}};
        goto done_cmp;
      }
      if(current_left.type() != right.type())
        right = safe_typecast(right, current_left.type());
      cmp = equal_exprt{current_left, right};
    }
    else if(op == "IsNot")
    {
      // PLR §6.10.3: "x is not None" checks tag != NONE
      if(
        is_python_value_type(current_left.type()) && right.is_constant() &&
        right.type().id() == ID_signedbv)
      {
        mp_integer rv;
        if(
          !to_integer(to_constant_expr(right), rv) &&
          rv == mp_integer{-4611686018427387904LL})
        {
          cmp =
            not_exprt{python_value_is(current_left, python_type_tagt::NONE)};
          goto done_cmp;
        }
      }
      // A concrete class-instance (struct) compared against None is
      // always non-None — typecasting the None-sentinel int to a
      // struct type produces nondet and would allow the solver to
      // pick a value that looks like None. Simplify to true.
      if(
        current_left.type().id() == ID_struct && right.is_constant() &&
        right.type().id() == ID_signedbv)
      {
        mp_integer rv;
        if(
          !to_integer(to_constant_expr(right), rv) &&
          rv == mp_integer{-4611686018427387904LL})
        {
          cmp = true_exprt{};
          goto done_cmp;
        }
      }
      // PLR §6.10.3: list/dict 'x is not y' — same logic as 'is'
      // but inverted. Different-symbol or literal-on-either-side
      // is True; same symbol is False. Aliasing is not tracked.
      if(
        (is_python_list_type(current_left.type()) ||
         is_python_dict_type(current_left.type())) &&
        (is_python_list_type(right.type()) ||
         is_python_dict_type(right.type())))
      {
        bool same_id = current_left.id() == ID_symbol &&
                       right.id() == ID_symbol &&
                       to_symbol_expr(current_left).get_identifier() ==
                         to_symbol_expr(right).get_identifier();
        cmp = same_id ? exprt{false_exprt{}} : exprt{true_exprt{}};
        goto done_cmp;
      }
      if(current_left.type() != right.type())
        right = safe_typecast(right, current_left.type());
      cmp = notequal_exprt{current_left, right};
    }
    else
    {
      log.warning() << "Unsupported comparison operator: " << op
                    << messaget::eom;
      return side_effect_expr_nondett{bool_typet{}, source_locationt{}};
    }

  done_cmp:
    if(result.is_nil())
      result = cmp;
    else
      result = and_exprt{result, cmp};

    current_left = right;
  }

  return result;
}
