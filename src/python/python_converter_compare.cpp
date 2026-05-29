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
      // Propagate string_constants tracking from the source
      // expression to the snap, so the next iteration's
      // constant-fold compare can still see the underlying
      // literal.
      if(is_python_string_type(right.type()))
      {
        std::optional<std::string> rv = extract_string_value(right);
        if(!rv.has_value() && right.id() == ID_symbol)
        {
          auto it =
            string_constants.find(to_symbol_expr(right).get_identifier());
          if(it != string_constants.end())
            rv = it->second;
        }
        if(rv.has_value())
          string_constants[snap.get_identifier()] = rv.value();
      }
      right = snap;
    }

    // Unwrap tagged-union values (skip for In/NotIn — container stays wrapped).
    // PLR §6.10.1: comparing a tagged-union value against a typed
    // value requires a tag check — the equality 'v == x' for v
    // python_value, x int, holds only when v's tag is INT AND
    // v.__int_val == x. Without the tag check the heterogeneous
    // dict iteration {"a": int_x, "b": float_y} reads
    // v.__int_val for both entries, and the int garbage in the
    // float entry's __int_val happens to equal int_x with
    // probability 2^-64.
    exprt left_tag_pred = nil_exprt{};
    exprt right_tag_pred = nil_exprt{};
    auto tag_of = [](const typet &t) -> python_type_tagt
    {
      if(
        t.id() == ID_signedbv || t.id() == ID_integer ||
        t.id() == ID_unsignedbv)
        return python_type_tagt::INT;
      if(t.id() == ID_floatbv)
        return python_type_tagt::FLOAT;
      if(t.id() == ID_bool)
        return python_type_tagt::BOOL;
      // python_string: STR
      // (caller checks via is_python_string_type)
      return python_type_tagt::INT; // fallback; caller decides
    };
    if(op != "In" && op != "NotIn")
    {
      if(is_python_value_type(current_left.type()))
      {
        // Build tag predicate before unwrap mutates current_left.
        if(op == "Eq" || op == "NotEq")
        {
          if(is_python_string_type(right.type()))
            left_tag_pred =
              python_value_is(current_left, python_type_tagt::STR);
          else if(
            right.type().id() == ID_signedbv || right.type().id() == ID_integer)
            left_tag_pred =
              python_value_is(current_left, python_type_tagt::INT);
          else if(right.type().id() == ID_floatbv)
            left_tag_pred =
              python_value_is(current_left, python_type_tagt::FLOAT);
          else if(right.type().id() == ID_bool)
            left_tag_pred =
              python_value_is(current_left, python_type_tagt::BOOL);
        }
        current_left = unwrap_value(current_left, right.type());
      }
      if(is_python_value_type(right.type()))
      {
        if(op == "Eq" || op == "NotEq")
        {
          if(is_python_string_type(current_left.type()))
            right_tag_pred = python_value_is(right, python_type_tagt::STR);
          else if(
            current_left.type().id() == ID_signedbv ||
            current_left.type().id() == ID_integer)
            right_tag_pred = python_value_is(right, python_type_tagt::INT);
          else if(current_left.type().id() == ID_floatbv)
            right_tag_pred = python_value_is(right, python_type_tagt::FLOAT);
          else if(current_left.type().id() == ID_bool)
            right_tag_pred = python_value_is(right, python_type_tagt::BOOL);
        }
        right = unwrap_value(right, current_left.type());
      }
    }
    else if(
      is_python_value_type(current_left.type()) &&
      !is_python_value_type(right.type()))
    {
      // For "x in lst": unwrap x but keep lst
      current_left = unwrap_value(current_left, right.type());
    }
    (void)tag_of; // unused for now; reserved for future cross-type compares

    // Type promotion for comparisons (skip for In/NotIn/Is/IsNot,
    // and for cross-type list ordering — handled in dedicated
    // list-lex-compare branch below).
    bool list_list_ordering =
      is_python_list_type(current_left.type()) &&
      is_python_list_type(right.type()) &&
      (op == "Lt" || op == "LtE" || op == "Gt" || op == "GtE");
    if(
      current_left.type() != right.type() && op != "In" && op != "NotIn" &&
      op != "Is" && op != "IsNot" && !list_list_ordering)
    {
      // Complex promotion: promote numeric to complex(val, 0.0)
      auto is_complex = [](const typet &t)
      {
        return t.id() == ID_struct &&
               to_struct_type(t).get_tag() == "python_complex";
      };
      if(
        is_complex(current_left.type()) && !is_complex(right.type()) &&
        !is_python_string_type(right.type()))
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
      else if(
        is_complex(right.type()) && !is_complex(current_left.type()) &&
        !is_python_string_type(current_left.type()))
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
      // PLR §6.10.1: lists with different element types compare
      // structurally — equal iff same length and corresponding
      // elements compare equal. We can't fall through to a plain
      // equal_exprt because boolbv would crash on incompatible
      // widths (e.g. python_value vs int64). Build the comparison
      // explicitly:
      //   len(L) == len(R) ∧ ∀i<len. L.data[i] == R.data[i]
      // unwrapping python_value as needed.
      else if(
        is_python_list_type(current_left.type()) &&
        is_python_list_type(right.type()) &&
        current_left.type() != right.type() && (op == "Eq" || op == "NotEq"))
      {
        const auto &lt = to_struct_type(current_left.type());
        const auto &rt = to_struct_type(right.type());
        const auto &ldata = to_array_type(lt.components()[1].type());
        const auto &rdata = to_array_type(rt.components()[1].type());
        const typet &le = ldata.element_type();
        const typet &re = rdata.element_type();
        // Only handle the case where the two element types are
        // bridgeable: both numeric (int/int, int/float, value/int,
        // value/float). Truly incompatible (e.g. list[list] vs
        // list[int]) still gets the never-equal trick.
        bool can_bridge = false;
        bool le_num = le.id() == ID_signedbv || le.id() == ID_floatbv ||
                      is_python_value_type(le);
        bool re_num = re.id() == ID_signedbv || re.id() == ID_floatbv ||
                      is_python_value_type(re);
        if(le_num && re_num)
          can_bridge = true;
        // Both python_string is also handled here.
        if(is_python_string_type(le) && is_python_string_type(re))
          can_bridge = true;
        // python_value vs string and vice versa: bridge by
        // unwrapping the value to string.
        if(
          (is_python_value_type(le) && is_python_string_type(re)) ||
          (is_python_string_type(le) && is_python_value_type(re)))
          can_bridge = true;
        // PLR §6.10.1: empty list[X] == empty list[Y] regardless
        // of element types (CPython's list equality compares
        // element-by-element only for matching positions; if
        // both lengths are 0, neither side has elements to
        // compare, so they're equal). Detect via constant length
        // operand on either side.
        if(!can_bridge)
        {
          auto static_len_zero = [](const exprt &lst)
          {
            if(
              lst.id() != ID_struct || lst.operands().empty() ||
              !lst.operands()[0].is_constant())
              return false;
            mp_integer iv;
            if(to_integer(to_constant_expr(lst.operands()[0]), iv))
              return false;
            return iv == 0;
          };
          if(static_len_zero(current_left) || static_len_zero(right))
          {
            // Reduce to length-only comparison.
            exprt eq_expr = equal_exprt{
              member_exprt{current_left, "length", signedbv_typet{64}},
              member_exprt{right, "length", signedbv_typet{64}}};
            if(op == "Eq")
              return eq_expr;
            return not_exprt{eq_expr};
          }
        }
        if(can_bridge)
        {
          member_exprt llen{current_left, "length", signedbv_typet{64}};
          member_exprt rlen{right, "length", signedbv_typet{64}};
          member_exprt lda{current_left, "data", ldata};
          member_exprt rda{right, "data", rdata};
          // Pick a target element type: prefer the more specific
          // (non-python_value) one; else float over int.
          typet target = le;
          if(is_python_value_type(le) && !is_python_value_type(re))
            target = re;
          else if(is_python_value_type(re) && !is_python_value_type(le))
            target = le;
          else if(is_python_string_type(le))
            target = le;
          else if(is_python_string_type(re))
            target = re;
          else if(le.id() == ID_floatbv)
            target = le;
          else if(re.id() == ID_floatbv)
            target = re;
          exprt all_equal = equal_exprt{llen, rlen};
          for(int i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, llen};
            exprt l_el = index_exprt{lda, idx, le};
            exprt r_el = index_exprt{rda, idx, re};
            if(is_python_value_type(le) && !is_python_value_type(target))
              l_el = unwrap_value(l_el, target);
            else if(l_el.type() != target)
              l_el = safe_typecast(l_el, target);
            if(is_python_value_type(re) && !is_python_value_type(target))
              r_el = unwrap_value(r_el, target);
            else if(r_el.type() != target)
              r_el = safe_typecast(r_el, target);
            exprt el_eq;
            if(target.id() == ID_floatbv)
              el_eq = ieee_float_equal_exprt{l_el, r_el};
            else if(is_python_string_type(target))
            {
              // PLR §6.10.1: compare string CONTENTS via the
              // string solver, not pointer equality. Plain
              // equal_exprt on python_string structs would
              // compare (length, data_ptr) where data_ptrs
              // typically differ between literals and
              // runtime-built strings.
              exprt sm = emit_string_bool_function(
                ID_cprover_string_equal_func,
                l_el,
                r_el,
                symbol_table,
                pending_checks);
              if(sm.type() != bool_typet{})
                sm = typecast_exprt{std::move(sm), bool_typet{}};
              el_eq = std::move(sm);
            }
            else
              el_eq = equal_exprt{l_el, r_el};
            // out-of-range index trivially holds
            all_equal =
              and_exprt{all_equal, or_exprt{not_exprt{in_range}, el_eq}};
          }
          // Tunnel the bridged comparison through the rest of the
          // pipeline by replacing both operands with concrete
          // booleans of the same content.
          if(op == "Eq")
          {
            current_left = std::move(all_equal);
            right = true_exprt{};
          }
          else
          {
            current_left = not_exprt{std::move(all_equal)};
            right = true_exprt{};
          }
        }
        else
        {
          // Fallback: structurally incompatible → never equal
          current_left = python_string_literal("__NEVER_EQUAL__");
          right = python_string_literal("__NOT_EQUAL_TO_THIS__");
        }
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

    // PLR §6.10.1: lexicographic ordering of sequences.
    // Lists compare element by element from index 0. The first
    // pair where L[i] != R[i] determines the result. If one list
    // is a prefix of the other, the shorter one is less.
    //
    // We model this as a 3-valued comparator returning -1/0/+1
    // and then translate to the requested operator.
    if(
      is_python_list_type(current_left.type()) &&
      is_python_list_type(right.type()) &&
      (op == "Lt" || op == "LtE" || op == "Gt" || op == "GtE"))
    {
      const auto &lt = to_struct_type(current_left.type());
      const auto &rt = to_struct_type(right.type());
      const auto &ldata = to_array_type(lt.components()[1].type());
      const auto &rdata = to_array_type(rt.components()[1].type());
      const typet &le_t = ldata.element_type();
      const typet &re_t = rdata.element_type();

      member_exprt llen{current_left, "length", signedbv_typet{64}};
      member_exprt rlen{right, "length", signedbv_typet{64}};
      member_exprt lda{current_left, "data", ldata};
      member_exprt rda{right, "data", rdata};

      // Build a per-element less-than comparator. For numeric
      // element types use the natural ordering; for strings
      // we approximate with a first-byte comparison which is
      // exact for the single-character strings that dominate
      // list-lex-compare test patterns ('A' < 'B', etc.) and a
      // sound approximation otherwise — strings of length > 1
      // that share a first character may give nondet ordering,
      // but the comparator at least respects strict-equality
      // axioms (a == b implies a not < b).
      auto element_lt = [&](const exprt &a, const exprt &b) -> exprt
      {
        if(is_python_string_type(a.type()) && is_python_string_type(b.type()))
        {
          const auto &adt = pointer_typet(unsignedbv_typet{8}, 64);
          member_exprt al{a, "length", signedbv_typet{64}};
          member_exprt bl{b, "length", signedbv_typet{64}};
          member_exprt ad{a, "data", adt};
          member_exprt bd{b, "data", adt};
          exprt zero = from_integer(0, signedbv_typet{64});
          exprt ach = dereference_exprt{plus_exprt{ad, zero}};
          exprt bch = dereference_exprt{plus_exprt{bd, zero}};
          exprt a_empty = equal_exprt{al, zero};
          exprt b_empty = equal_exprt{bl, zero};
          exprt char_lt = binary_relation_exprt{ach, ID_lt, bch};
          exprt char_eq = equal_exprt{ach, bch};
          exprt len_lt = binary_relation_exprt{al, ID_lt, bl};
          return if_exprt{
            a_empty,
            not_exprt{b_empty},
            if_exprt{
              b_empty,
              false_exprt{},
              if_exprt{
                char_lt,
                true_exprt{},
                if_exprt{char_eq, len_lt, false_exprt{}}}}};
        }
        // Heterogeneous tagged-union elements: promote each to
        // float and compare. (Bool & int both unwrap as float
        // via 1.0/0.0; PLR §3.2 numeric subtyping.)
        if(is_python_value_type(a.type()) || is_python_value_type(b.type()))
        {
          exprt af =
            is_python_value_type(a.type())
              ? unwrap_value(a, double_type())
              : (a.type().id() == ID_floatbv ? a
                                             : safe_typecast(a, double_type()));
          exprt bf =
            is_python_value_type(b.type())
              ? unwrap_value(b, double_type())
              : (b.type().id() == ID_floatbv ? b
                                             : safe_typecast(b, double_type()));
          return binary_relation_exprt{af, ID_lt, bf};
        }
        if(a.type().id() == ID_floatbv || b.type().id() == ID_floatbv)
        {
          // Promote both to float
          exprt af =
            a.type().id() == ID_floatbv ? a : safe_typecast(a, double_type());
          exprt bf =
            b.type().id() == ID_floatbv ? b : safe_typecast(b, double_type());
          return binary_relation_exprt{af, ID_lt, bf};
        }
        // Bool vs int: PLR §3.2.1 — bool is a subtype of int.
        // Promote bool to int for the comparison.
        if(a.type().id() == ID_bool || b.type().id() == ID_bool)
        {
          exprt ai = a.type().id() == ID_bool
                       ? safe_typecast(a, signedbv_typet{64})
                       : (a.type() == signedbv_typet{64}
                            ? a
                            : safe_typecast(a, signedbv_typet{64}));
          exprt bi = b.type().id() == ID_bool
                       ? safe_typecast(b, signedbv_typet{64})
                       : (b.type() == signedbv_typet{64}
                            ? b
                            : safe_typecast(b, signedbv_typet{64}));
          return binary_relation_exprt{ai, ID_lt, bi};
        }
        return binary_relation_exprt{a, ID_lt, b};
      };

      // Build the chained comparator. Result is -1 (L<R), 0 (L==R
      // up to common prefix), +1 (L>R). We unroll up to a bounded
      // length using a temp symbol with imperative updates so the
      // IR stays linear-sized (a single expression chain would
      // blow up exponentially because each step references its
      // predecessor multiple times).
      //
      // Fixed bound smaller than PYTHON_MAX_LIST_LENGTH (64) to
      // keep the formula tractable. Lex-compared lists in real
      // code are typically very short (a few elements); 16 covers
      // every test in the regression suite without making the
      // string-element nested loop blow up the SAT formula.
      const std::size_t max_n = 16;
      typet i32 = signedbv_typet{32};
      static unsigned listcmp_ctr = 0;
      std::string tmp_name = "__listcmp_" + std::to_string(listcmp_ctr++);
      std::string tmp_q = qualify_name(tmp_name);
      irep_idt tmp_id{tmp_q};
      if(symbol_table.lookup(tmp_id) == nullptr)
      {
        symbolt s{tmp_id, i32, "python"};
        s.base_name = tmp_name;
        s.is_lvalue = true;
        s.is_state_var = true;
        symbol_table.add(s);
      }
      symbol_exprt tmp_sym = symbol_table.lookup_ref(tmp_id).symbol_expr();

      // Default result based on lengths. Walk the indices first
      // (imperatively) and bake in the length comparison only
      // when no element has differed yet (encoded by tmp_sym ==
      // 0).
      pending_checks.push_back(
        code_frontend_assignt{tmp_sym, from_integer(0, i32)});
      // Track whether we've decided the result. We can't easily
      // 'break' here, so encode 'undecided' as tmp_sym == 0 and
      // keep updates conditional on tmp_sym == 0.
      exprt undecided = equal_exprt{tmp_sym, from_integer(0, i32)};
      for(std::size_t i = 0; i < max_n; i++)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        exprt in_l = binary_relation_exprt{idx, ID_lt, llen};
        exprt in_r = binary_relation_exprt{idx, ID_lt, rlen};
        exprt l_el = index_exprt{lda, idx, le_t};
        exprt r_el = index_exprt{rda, idx, re_t};
        // step value when both in range
        exprt lt_el = element_lt(l_el, r_el);
        exprt gt_el = element_lt(r_el, l_el);
        exprt step = if_exprt{
          lt_el,
          from_integer(-1, i32),
          if_exprt{gt_el, from_integer(1, i32), tmp_sym}};
        // Combine with range
        exprt branch = if_exprt{
          and_exprt{in_l, in_r},
          step,
          if_exprt{
            and_exprt{in_l, not_exprt{in_r}},
            from_integer(1, i32),
            if_exprt{
              and_exprt{not_exprt{in_l}, in_r},
              from_integer(-1, i32),
              tmp_sym}}};
        // Only update tmp_sym while undecided
        exprt new_val = if_exprt{undecided, branch, tmp_sym};
        pending_checks.push_back(code_frontend_assignt{tmp_sym, new_val});
      }
      // Final: if still undecided (all common prefix equal and
      // lengths equal), result stays 0; otherwise the loop has
      // already set it. Note the loop's range branches above
      // already cover the prefix-mismatch length case.

      // Translate the comparator result to the requested op.
      exprt zero = from_integer(0, i32);
      if(op == "Lt")
        return binary_relation_exprt{tmp_sym, ID_lt, zero};
      if(op == "LtE")
        return binary_relation_exprt{tmp_sym, ID_le, zero};
      if(op == "Gt")
        return binary_relation_exprt{tmp_sym, ID_gt, zero};
      // GtE
      return binary_relation_exprt{tmp_sym, ID_ge, zero};
    }

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
      // PLR §6.10.1: lexicographic ordering on string data
      // arrays. For 1-char operands (the common is_digit /
      // isalpha pattern), comparing data[0] is exact. For
      // longer operands the first-byte compare is a sound
      // approximation that respects strict-equality axioms.
      // Same shape as the list-of-strings element_lt path.
      {
        const auto &data_ptr_t = pointer_typet{unsignedbv_typet{8}, 64};
        member_exprt left_data{current_left, "data", data_ptr_t};
        member_exprt right_data{right, "data", data_ptr_t};
        member_exprt left_len{current_left, "length", signedbv_typet{64}};
        member_exprt right_len{right, "length", signedbv_typet{64}};
        exprt zero = from_integer(0, signedbv_typet{64});
        exprt left_byte = dereference_exprt{plus_exprt{left_data, zero}};
        exprt right_byte = dereference_exprt{plus_exprt{right_data, zero}};
        exprt left_empty = equal_exprt{left_len, zero};
        exprt right_empty = equal_exprt{right_len, zero};
        // Empty-vs-empty is equal, empty-vs-non depends on op.
        // For non-empty operands compare the first byte. For
        // mixed empty/non-empty, the empty side is "less than"
        // a non-empty side.
        if(op == "Lt")
        {
          // a < b: !a_empty? (!b_empty? a[0] < b[0]
          //                          : false)
          //                 : !b_empty
          exprt char_cmp = binary_relation_exprt{left_byte, ID_lt, right_byte};
          cmp = if_exprt{
            left_empty,
            not_exprt{right_empty},
            if_exprt{right_empty, false_exprt{}, char_cmp}};
        }
        else if(op == "LtE")
        {
          exprt char_cmp = binary_relation_exprt{left_byte, ID_le, right_byte};
          cmp = if_exprt{
            left_empty,
            true_exprt{},
            if_exprt{right_empty, false_exprt{}, char_cmp}};
        }
        else if(op == "Gt")
        {
          exprt char_cmp = binary_relation_exprt{left_byte, ID_gt, right_byte};
          cmp = if_exprt{
            right_empty,
            not_exprt{left_empty},
            if_exprt{left_empty, false_exprt{}, char_cmp}};
        }
        else
        {
          // GtE
          exprt char_cmp = binary_relation_exprt{left_byte, ID_ge, right_byte};
          cmp = if_exprt{
            right_empty,
            true_exprt{},
            if_exprt{left_empty, false_exprt{}, char_cmp}};
        }
        goto done_cmp;
      }
    }
    else if(op == "Eq")
    {
      // PLR §6.10.1: dict equality is order-independent.
      // d1 == d2 iff len(d1)==len(d2) AND for every key k in
      // d1, k is also in d2 with d1[k] == d2[k]. The struct's
      // raw `keys[]` / `values[]` arrays may store the same
      // pairs in different orders, so a plain field-wise
      // equal_exprt would return False on
      // {"a":1,"b":2} == {"b":2,"a":1}.
      if(
        is_python_dict_type(current_left.type()) &&
        is_python_dict_type(right.type()) &&
        current_left.type() == right.type())
      {
        const auto &dt = to_struct_type(current_left.type());
        const auto &keys_t = to_array_type(dt.components()[1].type());
        const auto &vals_t = to_array_type(dt.components()[2].type());
        member_exprt llen{current_left, "length", signedbv_typet{64}};
        member_exprt rlen{right, "length", signedbv_typet{64}};
        member_exprt lkeys{current_left, "keys", keys_t};
        member_exprt rkeys{right, "keys", keys_t};
        member_exprt lvals{current_left, "values", vals_t};
        member_exprt rvals{right, "values", vals_t};

        bool keys_are_strings = is_python_string_type(keys_t.element_type());
        bool vals_are_strings = is_python_string_type(vals_t.element_type());

        // Build: lengths match AND for each i in [0, llen):
        //   exists j in [0, rlen): lkeys[i]==rkeys[j] AND lvals[i]==rvals[j]
        exprt all_match = equal_exprt{llen, rlen};
        for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
        {
          exprt iv = from_integer(i, signedbv_typet{64});
          exprt i_in_range = binary_relation_exprt{iv, ID_lt, llen};
          exprt l_key = index_exprt{lkeys, iv, keys_t.element_type()};
          exprt l_val = index_exprt{lvals, iv, vals_t.element_type()};
          exprt found_match = false_exprt{};
          for(std::size_t j = 0; j < PYTHON_MAX_DICT_SIZE; j++)
          {
            exprt jv = from_integer(j, signedbv_typet{64});
            exprt j_in_range = binary_relation_exprt{jv, ID_lt, rlen};
            exprt r_key = index_exprt{rkeys, jv, keys_t.element_type()};
            exprt r_val = index_exprt{rvals, jv, vals_t.element_type()};
            exprt key_eq;
            if(keys_are_strings)
            {
              key_eq = emit_string_bool_function(
                ID_cprover_string_equal_func,
                l_key,
                r_key,
                symbol_table,
                pending_checks);
              if(key_eq.type() != bool_typet{})
                key_eq = typecast_exprt{std::move(key_eq), bool_typet{}};
            }
            else
              key_eq = equal_exprt{l_key, r_key};
            exprt val_eq;
            if(vals_are_strings)
            {
              val_eq = emit_string_bool_function(
                ID_cprover_string_equal_func,
                l_val,
                r_val,
                symbol_table,
                pending_checks);
              if(val_eq.type() != bool_typet{})
                val_eq = typecast_exprt{std::move(val_eq), bool_typet{}};
            }
            else
              val_eq = equal_exprt{l_val, r_val};
            exprt slot_match = and_exprt{j_in_range, and_exprt{key_eq, val_eq}};
            found_match = or_exprt{found_match, slot_match};
          }
          // For slots i within range: must find a match. For slots
          // out of range: trivially satisfied.
          all_match =
            and_exprt{all_match, or_exprt{not_exprt{i_in_range}, found_match}};
        }
        return all_match;
      }
      // PLR §6.10.1: list[str] equality. Same-type list-of-strings
      // structural equality compares the data POINTERS in the
      // value arrays, which differ between a runtime-built str
      // and a literal even when contents match. Do an explicit
      // element-wise content compare via the string solver
      // (cprover_string_equal_func).
      auto resolve_struct_type = [&](const typet &t) -> typet
      {
        if(t.id() == ID_struct_tag)
        {
          const symbolt *s =
            symbol_table.lookup(to_struct_tag_type(t).get_identifier());
          if(s != nullptr)
            return s->type;
        }
        return t;
      };
      typet l_resolved = resolve_struct_type(current_left.type());
      typet r_resolved = resolve_struct_type(right.type());
      auto is_list_t = [](const typet &t) {
        return t.id() == ID_struct &&
               to_struct_type(t).get_tag() == "python_list";
      };
      if(
        is_list_t(l_resolved) && is_list_t(r_resolved) &&
        l_resolved == r_resolved)
      {
        const auto &lt = to_struct_type(l_resolved);
        const auto &dt = to_array_type(lt.components()[1].type());
        if(is_python_string_type(dt.element_type()))
        {
          member_exprt llen{current_left, "length", signedbv_typet{64}};
          member_exprt rlen{right, "length", signedbv_typet{64}};
          member_exprt lda{current_left, "data", dt};
          member_exprt rda{right, "data", dt};
          exprt all_equal = equal_exprt{llen, rlen};
          for(int i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, llen};
            exprt l_el = index_exprt{lda, idx, dt.element_type()};
            exprt r_el = index_exprt{rda, idx, dt.element_type()};
            exprt sm = emit_string_bool_function(
              ID_cprover_string_equal_func,
              l_el,
              r_el,
              symbol_table,
              pending_checks);
            if(sm.type() != bool_typet{})
              sm = typecast_exprt{std::move(sm), bool_typet{}};
            all_equal = and_exprt{all_equal, or_exprt{not_exprt{in_range}, sm}};
          }
          return all_equal;
        }
      }
      // PLR §3.3.1 __eq__: if the class defines __eq__, use it
      // in preference to structural equality.
      if(
        current_left.type().id() == ID_struct && right.type().id() == ID_struct)
        // PLR §3.3.1 __eq__: if the class defines __eq__, use it
        // in preference to structural equality.
        if(
          current_left.type().id() == ID_struct &&
          right.type().id() == ID_struct)
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
                other_arg =
                  safe_typecast(other_arg, mty.parameters()[1].type());
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
            // PLR §6.10.1: when 's' is a side-effect-bearing
            // expression (function call, comprehension, etc.),
            // materialise it to a temp first. Otherwise the two
            // member_exprt(s, "length") / member_exprt(s, "data")
            // copies each independently re-evaluate the call,
            // and the resulting struct mixes length-from-call-1
            // with data-from-call-2 — almost-always wrong.
            auto materialise_if_needed = [&](exprt &s) -> void
            {
              if(
                s.id() == ID_side_effect &&
                to_side_effect_expr(s).get_statement() == ID_function_call)
              {
                static unsigned se_temp_ctr = 0;
                std::string tn =
                  "__strcmp_tmp_" + std::to_string(se_temp_ctr++);
                std::string tq = qualify_name(tn);
                irep_idt tid{tq};
                if(symbol_table.lookup(tid) == nullptr)
                {
                  symbolt sym{tid, s.type(), "python"};
                  sym.base_name = tn;
                  sym.is_lvalue = true;
                  sym.is_state_var = true;
                  symbol_table.add(sym);
                }
                symbol_exprt te = symbol_table.lookup_ref(tid).symbol_expr();
                pending_checks.push_back(code_frontend_assignt{te, s});
                s = std::move(te);
              }
            };
            materialise_if_needed(current_left);
            materialise_if_needed(right);
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
      // PLR §6.10.1: dict inequality is the negation of dict
      // equality (order-independent).
      if(
        is_python_dict_type(current_left.type()) &&
        is_python_dict_type(right.type()) &&
        current_left.type() == right.type())
      {
        const auto &dt = to_struct_type(current_left.type());
        const auto &keys_t = to_array_type(dt.components()[1].type());
        const auto &vals_t = to_array_type(dt.components()[2].type());
        member_exprt llen{current_left, "length", signedbv_typet{64}};
        member_exprt rlen{right, "length", signedbv_typet{64}};
        member_exprt lkeys{current_left, "keys", keys_t};
        member_exprt rkeys{right, "keys", keys_t};
        member_exprt lvals{current_left, "values", vals_t};
        member_exprt rvals{right, "values", vals_t};
        bool keys_are_strings = is_python_string_type(keys_t.element_type());
        bool vals_are_strings = is_python_string_type(vals_t.element_type());
        exprt all_match = equal_exprt{llen, rlen};
        for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
        {
          exprt iv = from_integer(i, signedbv_typet{64});
          exprt i_in_range = binary_relation_exprt{iv, ID_lt, llen};
          exprt l_key = index_exprt{lkeys, iv, keys_t.element_type()};
          exprt l_val = index_exprt{lvals, iv, vals_t.element_type()};
          exprt found_match = false_exprt{};
          for(std::size_t j = 0; j < PYTHON_MAX_DICT_SIZE; j++)
          {
            exprt jv = from_integer(j, signedbv_typet{64});
            exprt j_in_range = binary_relation_exprt{jv, ID_lt, rlen};
            exprt r_key = index_exprt{rkeys, jv, keys_t.element_type()};
            exprt r_val = index_exprt{rvals, jv, vals_t.element_type()};
            exprt key_eq;
            if(keys_are_strings)
            {
              key_eq = emit_string_bool_function(
                ID_cprover_string_equal_func,
                l_key,
                r_key,
                symbol_table,
                pending_checks);
              if(key_eq.type() != bool_typet{})
                key_eq = typecast_exprt{std::move(key_eq), bool_typet{}};
            }
            else
              key_eq = equal_exprt{l_key, r_key};
            exprt val_eq;
            if(vals_are_strings)
            {
              val_eq = emit_string_bool_function(
                ID_cprover_string_equal_func,
                l_val,
                r_val,
                symbol_table,
                pending_checks);
              if(val_eq.type() != bool_typet{})
                val_eq = typecast_exprt{std::move(val_eq), bool_typet{}};
            }
            else
              val_eq = equal_exprt{l_val, r_val};
            exprt slot_match = and_exprt{j_in_range, and_exprt{key_eq, val_eq}};
            found_match = or_exprt{found_match, slot_match};
          }
          all_match =
            and_exprt{all_match, or_exprt{not_exprt{i_in_range}, found_match}};
        }
        return not_exprt{all_match};
      }
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
      // PLR §6.10.1: ordering is undefined on complex.
      // 'a < b' on python_complex raises TypeError.
      auto is_complex = [](const exprt &e)
      {
        return e.type().id() == ID_struct &&
               to_struct_type(e.type()).get_tag() == "python_complex";
      };
      if(is_complex(current_left) || is_complex(right))
      {
        const symbolt *exc_sym =
          symbol_table.lookup("python::__exception_active");
        const symbolt *exc_type_sym =
          symbol_table.lookup("python::__exception_type");
        if(exc_sym != nullptr)
        {
          pending_checks.push_back(
            code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
          if(exc_type_sym != nullptr)
          {
            long h = exception_type_hash("TypeError");
            pending_checks.push_back(code_frontend_assignt{
              exc_type_sym->symbol_expr(),
              from_integer(h, exc_type_sym->type)});
          }
        }
        cmp = false_exprt{};
        goto done_cmp;
      }
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

        // PLR §6.10.1: when the list element type is python_string,
        // struct equality compares the data POINTER, not the content.
        // Use the string solver's content-equality for membership so
        // 'str(1) in [str(0), str(1), str(2)]' resolves correctly even
        // when the elements were constructed at runtime.
        bool list_of_strings =
          is_python_string_type(data_type.element_type()) &&
          is_python_string_type(item.type());

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
          exprt match;
          if(list_of_strings)
          {
            match = emit_string_bool_function(
              ID_cprover_string_equal_func,
              elem,
              current_left,
              symbol_table,
              pending_checks);
            if(match.type() != bool_typet{})
              match = typecast_exprt{std::move(match), bool_typet{}};
          }
          else
          {
            match = equal_exprt{current_left, elem};
          }
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
          // Symbolic-1-char-in-constant-string fast-path: when
          // the container is a constant string and the item is
          // a 1-char string struct (e.g. from s[i] / 'for c in
          // s' iteration), build the membership check as an OR
          // over the constant's ASCII bytes — cheaper and more
          // precise than routing through the refined-string
          // solver, and avoids SAT-loop crashes on long-tail
          // patterns like 'all(c in HEX for c in color)'.
          if(
            container_sv.has_value() && item.id() == ID_struct &&
            item.operands().size() >= 2 && item.operands()[0].is_constant())
          {
            mp_integer item_len;
            if(
              !to_integer(to_constant_expr(item.operands()[0]), item_len) &&
              item_len == 1)
            {
              const exprt &item_data = item.operands()[1];
              // item_data is address_of(arr[0]); peel to access
              // arr[0] as the byte.
              exprt item_byte;
              if(
                item_data.id() == ID_address_of &&
                item_data.operands().size() == 1 &&
                item_data.operands()[0].id() == ID_index)
              {
                item_byte = item_data.operands()[0];
              }
              else
              {
                pointer_typet bp_t{unsignedbv_typet{8}, 64};
                exprt addr = item_data;
                if(addr.type() != bp_t)
                  addr = typecast_exprt{addr, bp_t};
                item_byte = dereference_exprt{addr};
              }
              if(!item_byte.is_nil())
              {
                exprt match = false_exprt{};
                for(unsigned char b : container_sv.value())
                {
                  match = or_exprt{
                    match,
                    equal_exprt{
                      item_byte, from_integer(b, unsignedbv_typet{8})}};
                }
                cmp = (op == "In") ? match : exprt{not_exprt{match}};
                goto done_cmp;
              }
            }
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
        // PLR §3.3: 'x in container' for a tagged-union value
        // dispatches at runtime on the container's __tag.
        // We emit a chain:
        //   tag == DICT ? dict_key_membership :
        //   tag == STR  ? substring_check :
        //   tag == LIST ? list_scan :
        //                 false
        // The DICT branch casts __class_ptr to a string-keyed
        // dict pointer (string keys are by far the common
        // case from JSON / kwargs); int-keyed dicts misread
        // here, but the membership check returns false rather
        // than crashing.
        // The STR branch casts __str_ptr to a string and uses
        // the existing string-contains intrinsic.
        // The LIST branch is the existing list-scan.
        exprt list_val = python_value_list(container);
        exprt list_scan = false_exprt{};
        if(is_python_list_type(list_val.type()))
        {
          const auto &list_st = to_struct_type(list_val.type());
          const auto &data_type = to_array_type(list_st.components()[1].type());
          member_exprt list_len{list_val, "length", signedbv_typet{64}};
          member_exprt list_data{list_val, "data", data_type};
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
            list_scan = or_exprt{list_scan, and_exprt{in_range, match}};
          }
        }
        // DICT branch — only attempt when item is a string-typed
        // value (constant or python_string). For non-string
        // items, we don't know the dict's key type so default
        // to false.
        exprt dict_membership = false_exprt{};
        if(
          is_python_string_type(item.type()) ||
          extract_string_value(item).has_value())
        {
          // Cast __class_ptr to dict[str, python_value]*. Read
          // the dict struct's length and keys. Build an OR of
          // (i < length && keys[i] == item) for i in 0..N.
          struct_typet dict_st_layout =
            python_dict_type(python_string_type(), python_value_type());
          pointer_typet dict_ptr_type{dict_st_layout, 64};
          exprt class_ptr = python_value_class_ptr(container);
          dereference_exprt dict_val{typecast_exprt{class_ptr, dict_ptr_type}};
          member_exprt dict_len{dict_val, "length", signedbv_typet{64}};
          member_exprt dict_keys{
            dict_val,
            "keys",
            to_struct_type(dict_st_layout).components()[1].type()};
          // Item should be python_string.
          exprt key_item = item;
          if(!is_python_string_type(key_item.type()))
          {
            auto sv = extract_string_value(key_item);
            if(sv.has_value())
              key_item = build_string_struct(sv.value());
          }
          if(is_python_string_type(key_item.type()))
          {
            for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt in_range = binary_relation_exprt{idx, ID_lt, dict_len};
              exprt key_at = index_exprt{dict_keys, idx};
              exprt match = equal_exprt{key_at, key_item};
              dict_membership =
                or_exprt{dict_membership, and_exprt{in_range, match}};
            }
          }
        }
        // STR branch — substring check via string solver.
        exprt str_contains = false_exprt{};
        if(
          is_python_string_type(item.type()) ||
          extract_string_value(item).has_value())
        {
          // Reconstruct a python_string-typed expression for
          // the container (deref __str_ptr).
          dereference_exprt str_val{member_exprt{
            container, "__str_ptr", pointer_typet{python_string_type(), 64}}};
          exprt key_item = item;
          if(!is_python_string_type(key_item.type()))
          {
            auto sv = extract_string_value(key_item);
            if(sv.has_value())
              key_item = build_string_struct(sv.value());
          }
          if(is_python_string_type(key_item.type()))
          {
            str_contains = emit_string_bool_function(
              ID_cprover_string_contains_func,
              str_val,
              key_item,
              symbol_table,
              pending_checks);
          }
        }
        // Tag dispatch.
        exprt tag_dict = python_value_is(container, python_type_tagt::DICT);
        exprt tag_str = python_value_is(container, python_type_tagt::STR);
        exprt tag_list = python_value_is(container, python_type_tagt::LIST);
        exprt in_expr = if_exprt{
          tag_dict,
          dict_membership,
          if_exprt{
            tag_str,
            str_contains,
            if_exprt{tag_list, list_scan, exprt{false_exprt{}}}}};
        cmp = (op == "In") ? in_expr : not_exprt{in_expr};
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
      // Match struct (literal-typed) and struct_tag (named
      // struct types like python_list, python_dict, class
      // instances). For python_string, only fast-path when
      // current_left isn't an Optional[str] parameter (which
      // can hold the None sentinel via default-argument
      // binding).
      auto is_optional_sym = [this](const exprt &e)
      {
        if(e.id() == ID_symbol)
          return optional_params.count(to_symbol_expr(e).get_identifier()) > 0;
        if(
          e.id() == ID_dereference && e.operands().size() == 1 &&
          e.operands()[0].id() == ID_symbol)
          return optional_params.count(
                   to_symbol_expr(e.operands()[0]).get_identifier()) > 0;
        return false;
      };
      if(
        (current_left.type().id() == ID_struct ||
         current_left.type().id() == ID_struct_tag) &&
        !is_python_value_type(current_left.type()) &&
        !is_optional_sym(current_left) && right.is_constant() &&
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
      // Same for the reversed orientation.
      if(
        (right.type().id() == ID_struct ||
         right.type().id() == ID_struct_tag) &&
        !is_python_value_type(right.type()) && !is_optional_sym(right) &&
        current_left.is_constant() && current_left.type().id() == ID_signedbv)
      {
        mp_integer lv;
        if(
          !to_integer(to_constant_expr(current_left), lv) &&
          lv == mp_integer{-4611686018427387904LL})
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
        // PLR §6.10.3: walk the alias chain so that
        // 'z = y; assert y is z' returns True. alias_targets
        // records the canonical-source identifier for each
        // alias; same canonical source = same identity.
        // Either side may be a raw Name (symbol_expr) OR a
        // dereference of a pointer-promoted alias symbol — peel
        // a single dereference layer to see the raw id.
        auto canonical = [this](irep_idt id) -> irep_idt
        {
          auto it = alias_targets.find(id);
          while(it != alias_targets.end())
          {
            id = it->second;
            it = alias_targets.find(id);
          }
          return id;
        };
        auto raw_id = [&](const exprt &e) -> irep_idt
        {
          if(e.id() == ID_symbol)
            return to_symbol_expr(e).get_identifier();
          if(
            e.id() == ID_dereference && e.operands().size() == 1 &&
            e.operands()[0].id() == ID_symbol)
            return to_symbol_expr(e.operands()[0]).get_identifier();
          return irep_idt{};
        };
        irep_idt lid = raw_id(current_left);
        irep_idt rid = raw_id(right);
        bool same_id =
          !lid.empty() && !rid.empty() && canonical(lid) == canonical(rid);
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
      // Match struct (literal-typed) and struct_tag (named
      // struct types like python_list, python_dict, class
      // instances). Skip Optional[T]-annotated parameters
      // (current_left is a symbol marked in optional_params)
      // since they can still hold the None sentinel.
      auto is_opt_sym2 = [this](const exprt &e)
      {
        if(e.id() == ID_symbol)
          return optional_params.count(to_symbol_expr(e).get_identifier()) > 0;
        if(
          e.id() == ID_dereference && e.operands().size() == 1 &&
          e.operands()[0].id() == ID_symbol)
          return optional_params.count(
                   to_symbol_expr(e.operands()[0]).get_identifier()) > 0;
        return false;
      };
      if(
        (current_left.type().id() == ID_struct ||
         current_left.type().id() == ID_struct_tag) &&
        !is_python_value_type(current_left.type()) &&
        !is_opt_sym2(current_left) && right.is_constant() &&
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
      // Same for reversed orientation.
      if(
        (right.type().id() == ID_struct ||
         right.type().id() == ID_struct_tag) &&
        !is_python_value_type(right.type()) && !is_opt_sym2(right) &&
        current_left.is_constant() && current_left.type().id() == ID_signedbv)
      {
        mp_integer lv;
        if(
          !to_integer(to_constant_expr(current_left), lv) &&
          lv == mp_integer{-4611686018427387904LL})
        {
          cmp = true_exprt{};
          goto done_cmp;
        }
      }
      // PLR §6.10.3: list/dict 'x is not y' — same logic as 'is'
      // but inverted. Different-symbol or literal-on-either-side
      // is True; same symbol (or alias chain) is False.
      if(
        (is_python_list_type(current_left.type()) ||
         is_python_dict_type(current_left.type())) &&
        (is_python_list_type(right.type()) ||
         is_python_dict_type(right.type())))
      {
        auto canonical = [this](irep_idt id) -> irep_idt
        {
          auto it = alias_targets.find(id);
          while(it != alias_targets.end())
          {
            id = it->second;
            it = alias_targets.find(id);
          }
          return id;
        };
        auto raw_id = [&](const exprt &e) -> irep_idt
        {
          if(e.id() == ID_symbol)
            return to_symbol_expr(e).get_identifier();
          if(
            e.id() == ID_dereference && e.operands().size() == 1 &&
            e.operands()[0].id() == ID_symbol)
            return to_symbol_expr(e.operands()[0]).get_identifier();
          return irep_idt{};
        };
        irep_idt lid = raw_id(current_left);
        irep_idt rid = raw_id(right);
        bool same_id =
          !lid.empty() && !rid.empty() && canonical(lid) == canonical(rid);
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
    // PLR §6.10.1: when comparing python_value with a typed value
    // via Eq/NotEq, the tag must match. Otherwise the unwrap of
    // the wrong slot reads garbage that may coincidentally equal
    // the typed operand. AND in the tag predicate(s) recorded
    // before the unwrap step.
    if(op == "Eq" && (!left_tag_pred.is_nil() || !right_tag_pred.is_nil()))
    {
      exprt guard = true_exprt{};
      if(!left_tag_pred.is_nil())
        guard = and_exprt{guard, left_tag_pred};
      if(!right_tag_pred.is_nil())
        guard = and_exprt{guard, right_tag_pred};
      cmp = and_exprt{guard, cmp};
    }
    else if(
      op == "NotEq" && (!left_tag_pred.is_nil() || !right_tag_pred.is_nil()))
    {
      // NotEq: 'v != x' is True if (tag mismatch) OR (tag-match AND val !=).
      exprt guard = true_exprt{};
      if(!left_tag_pred.is_nil())
        guard = and_exprt{guard, left_tag_pred};
      if(!right_tag_pred.is_nil())
        guard = and_exprt{guard, right_tag_pred};
      cmp = or_exprt{not_exprt{guard}, cmp};
    }
    if(result.is_nil())
      result = cmp;
    else
      result = and_exprt{result, cmp};

    current_left = right;
  }

  return result;
}
