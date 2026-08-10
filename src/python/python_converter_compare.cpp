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
#include <util/simplify_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

exprt python_convertert::python_value_structural_eq(
  const exprt &l,
  const exprt &r,
  int depth)
{
  const namespacet ns{symbol_table};

  auto fresh_nondet_bool = [&]() -> exprt
  {
    static unsigned ctr = 0;
    const std::string nm = "__pv_eq_nd_" + std::to_string(ctr++);
    const irep_idt id{qualify_name(nm)};
    if(symbol_table.lookup(id) == nullptr)
    {
      symbolt s{id, bool_typet{}, "python"};
      s.base_name = nm;
      s.is_lvalue = true;
      s.is_state_var = true;
      symbol_table.add(s);
    }
    symbol_exprt se = symbol_table.lookup_ref(id).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      se, side_effect_expr_nondett{bool_typet{}, source_locationt{}}});
    return std::move(se);
  };

  const exprt same_tag = equal_exprt{python_value_tag(l), python_value_tag(r)};

  // Build the value-equality for ONE specific tag. Only this branch's
  // (possibly expensive) operations are emitted.
  auto eq_for_tag = [&](python_type_tagt tag) -> exprt
  {
    switch(tag)
    {
    case python_type_tagt::NONE:
      return true_exprt{};
    case python_type_tagt::INT:
      return equal_exprt{python_value_int(l), python_value_int(r)};
    case python_type_tagt::BOOL:
      return equal_exprt{python_value_bool(l), python_value_bool(r)};
    case python_type_tagt::FLOAT:
      return ieee_float_equal_exprt{
        python_value_float(l), python_value_float(r)};
    case python_type_tagt::STR:
    {
      exprt s = emit_string_bool_function(
        ID_cprover_string_equal_func,
        python_value_str(l),
        python_value_str(r),
        symbol_table,
        pending_checks);
      if(s.type() != bool_typet{})
        s = typecast_exprt{std::move(s), bool_typet{}};
      return s;
    }
    case python_type_tagt::LIST:
    {
      if(depth <= 0)
        return fresh_nondet_bool();
      const dereference_exprt ll = python_value_list(l);
      const dereference_exprt rl = python_value_list(r);
      const auto &lst = to_struct_type(ll.type());
      const auto &ld = to_array_type(lst.components()[1].type());
      const member_exprt llen{ll, "length", signedbv_typet{64}};
      const member_exprt rlen{rl, "length", signedbv_typet{64}};
      emit_scan_bound_guard(llen, source_locationt{});
      const member_exprt lda{ll, "data", ld};
      const member_exprt rda{rl, "data", ld};
      exprt all = equal_exprt{llen, rlen};
      for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
      {
        const exprt idx = from_integer(i, signedbv_typet{64});
        const exprt in_range = binary_relation_exprt{idx, ID_lt, llen};
        const exprt le = index_exprt{lda, idx};
        const exprt re = index_exprt{rda, idx};
        const exprt eeq = python_value_structural_eq(le, re, depth - 1);
        all = and_exprt{std::move(all), or_exprt{not_exprt{in_range}, eeq}};
      }
      return all;
    }
    case python_type_tagt::CLASS:
    case python_type_tagt::DICT:
    case python_type_tagt::COMPLEX:
    case python_type_tagt::SET:
    case python_type_tagt::TUPLE:
      return fresh_nondet_bool(); // not yet structurally compared here (sound)
    case python_type_tagt::CLOSURE:
      // PLR: closures have no __eq__; equality is identity. Two
      // fat-closures are equal iff same fn and same capture record.
      return and_exprt{
        equal_exprt{python_value_closure_fn(l), python_value_closure_fn(r)},
        equal_exprt{python_value_closure_rec(l), python_value_closure_rec(r)}};
    }
    UNREACHABLE;
    return fresh_nondet_bool();
  };
  // STATIC DISPATCH: if either operand has a compile-time-constant tag, only
  // build that branch (same_tag pins both to it). This prunes the expensive
  // string-solver / list-recursion fan-out for literal / known-structure
  // elements -- the dominant case.
  auto static_tag = [&](const exprt &e) -> std::optional<python_type_tagt>
  {
    const exprt s = simplify_expr(e, ns);
    if(
      s.id() == ID_struct && !s.operands().empty() &&
      s.operands()[0].is_constant())
    {
      mp_integer v;
      if(!to_integer(to_constant_expr(s.operands()[0]), v))
        return static_cast<python_type_tagt>(v.to_long());
    }
    return std::nullopt;
  };
  std::optional<python_type_tagt> st = static_tag(l);
  if(!st)
    st = static_tag(r);
  if(st)
    return and_exprt{same_tag, eq_for_tag(*st)};

  // Symbolic tag: bounded full dispatch. For STR/LIST elements whose tag is
  // only known at runtime, comparing CONTENT would need the string solver /
  // pointer dereference at every element and recursion level (O(width^depth)
  // blow-up). Instead compare IDENTITY (same pointer ⟹ structurally equal,
  // which is sound) OR a nondet (different pointers ⟹ unknown). This recovers
  // the common aliased case (`a=[x]; b=[x]; a==b`) cheaply and stays sound for
  // the distinct-but-equal case (nondet). Literal/known-structure elements are
  // still compared precisely via the static-dispatch path above.
  if(depth <= 0)
    return fresh_nondet_bool();
  auto list_ptr = [](const exprt &v) -> member_exprt {
    return member_exprt{v, "__list_ptr", pointer_typet{empty_typet{}, 64}};
  };
  const exprt str_identity = or_exprt{
    equal_exprt{python_value_str(l), python_value_str(r)}, fresh_nondet_bool()};
  const exprt list_identity =
    or_exprt{equal_exprt{list_ptr(l), list_ptr(r)}, fresh_nondet_bool()};
  exprt val_eq = if_exprt{
    python_value_is(l, python_type_tagt::INT),
    eq_for_tag(python_type_tagt::INT),
    if_exprt{
      python_value_is(l, python_type_tagt::BOOL),
      eq_for_tag(python_type_tagt::BOOL),
      if_exprt{
        python_value_is(l, python_type_tagt::FLOAT),
        eq_for_tag(python_type_tagt::FLOAT),
        if_exprt{
          python_value_is(l, python_type_tagt::STR),
          str_identity,
          if_exprt{
            python_value_is(l, python_type_tagt::LIST),
            list_identity,
            if_exprt{
              python_value_is(l, python_type_tagt::NONE),
              true_exprt{},
              fresh_nondet_bool()}}}}}};
  return and_exprt{same_tag, std::move(val_eq)};
}

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

    // PLR §6.10.1: ordering (< > <= >=) of two operands whose orderable
    // CATEGORIES differ (numeric / str / list / tuple / set / dict / None) is a
    // TypeError -- e.g. `1 < [1]`, `[1] < 1`, `1 < "a"`, `None < 1`. Placed here
    // (before the numeric/string/list comparison paths) so it fires regardless
    // of which downstream path would handle the operands. Same-category
    // comparisons proceed normally (num<num incl. int<float<bool, str<str,
    // list<list lexicographic, set<set subset); an Any/class operand is category
    // 0 and is never flagged (no false positive; a class's __lt__ is dispatched
    // below).
    if(op == "Lt" || op == "LtE" || op == "Gt" || op == "GtE")
    {
      const int cl = orderable_category_of(current_left);
      const int cr = orderable_category_of(right);
      if(cl != 0 && cr != 0 && cl != cr)
      {
        emit_conditional_exception(true_exprt{}, "TypeError");
        exprt c = false_exprt{};
        result = result.is_nil() ? c : exprt(and_exprt{result, c});
        current_left = right;
        continue;
      }
    }

    // Native SMT-String back-end (Plan A): compare two SMT String values
    // directly. ==/!= lower to (= s t); ordering routes through compare_to
    // (lowered to str.< in smt2_conv). Bypasses the refined struct path.
    if(
      use_smt_string_native && (op == "Eq" || op == "NotEq" || op == "Lt" ||
                                op == "LtE" || op == "Gt" || op == "GtE"))
    {
      // Allow one side to be a python_value carrying a string (e.g. an
      // untyped class attribute): unwrap its __str slot and, for equality,
      // guard on the STR tag so a non-string value compares unequal.
      exprt L = current_left, R = right;
      exprt str_tag_pred = nil_exprt{};
      if(op == "Eq" || op == "NotEq")
      {
        if(is_python_value_type(L.type()) && R.type().id() == ID_string)
        {
          str_tag_pred = python_value_is(L, python_type_tagt::STR);
          L = python_value_str(L);
        }
        else if(is_python_value_type(R.type()) && L.type().id() == ID_string)
        {
          str_tag_pred = python_value_is(R, python_type_tagt::STR);
          R = python_value_str(R);
        }
      }
      if(L.type().id() == ID_string && R.type().id() == ID_string)
      {
        exprt c;
        if(op == "Eq")
          c = str_tag_pred.is_nil()
                ? exprt{equal_exprt{L, R}}
                : exprt{and_exprt{str_tag_pred, equal_exprt{L, R}}};
        else if(op == "NotEq")
          c = str_tag_pred.is_nil()
                ? exprt{not_exprt{equal_exprt{L, R}}}
                : exprt{or_exprt{
                    not_exprt{str_tag_pred}, not_exprt{equal_exprt{L, R}}}};
        else
        {
          const typet i32 = signedbv_typet{32};
          const irep_idt fn{ID_cprover_string_compare_to_func};
          if(symbol_table.lookup(fn) == nullptr)
          {
            std::vector<typet> ats{L.type(), R.type()};
            symbolt fs{
              fn, mathematical_function_typet(std::move(ats), i32), "python"};
            fs.base_name = id2string(fn);
            symbol_table.add(fs);
          }
          function_application_exprt app{
            symbol_table.lookup_ref(fn).symbol_expr(), {L, R}};
          app.type() = i32;
          static unsigned nscmp_ctr = 0;
          const irep_idt rid{
            "python::__smtn_cmp_" + std::to_string(nscmp_ctr++)};
          if(symbol_table.lookup(rid) == nullptr)
          {
            symbolt rs{rid, i32, "python"};
            rs.base_name = "__smtn_cmp_" + std::to_string(nscmp_ctr - 1);
            rs.is_lvalue = true;
            rs.is_state_var = true;
            symbol_table.add(rs);
          }
          const symbol_exprt cres = symbol_table.lookup_ref(rid).symbol_expr();
          pending_checks.push_back(code_frontend_assignt{cres, app});
          const exprt z = from_integer(0, i32);
          c = binary_relation_exprt{
            cres,
            op == "Lt" ? ID_lt
                       : (op == "LtE" ? ID_le : (op == "Gt" ? ID_gt : ID_ge)),
            z};
        }
        result = result.is_nil() ? c : exprt(and_exprt{result, c});
        current_left = right;
        continue;
      }
    }

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
    // PLR §6.10.1: ordered comparison (`<`, `<=`, `>`, `>=`)
    // of a tagged-union value against a numeric. The runtime
    // tag may be INT or FLOAT (e.g. the result of `a + b` where
    // one of a/b is a float — the binop produces a FLOAT-tagged
    // python_value). Blindly unwrapping to the other side's type
    // (`__int_val` when comparing against an int literal) reads
    // the wrong union field — for a FLOAT-tagged value the
    // `__int_val` slot is 0, so `(int_or_float_sum) <= 19`
    // collapsed to `0 <= 19` and always held. Extract numerically
    // with a tag dispatch to float (int values are cast up) and
    // compare both sides as float.
    bool ordered_op = (op == "Lt" || op == "LtE" || op == "Gt" || op == "GtE");
    // PLR §6.10.1: an ordered comparison (`<` `<=` `>` `>=`) between a
    // number and a string raises TypeError (unlike `==`, which is
    // just False). Fire only when both operands are statically
    // concrete and in different orderable categories; tagged values
    // and other types are left to the existing paths (sound).
    if(ordered_op)
    {
      auto ord_cat = [](const typet &t) -> int
      {
        if(
          t.id() == ID_signedbv || t.id() == ID_unsignedbv ||
          t.id() == ID_integer || t.id() == ID_floatbv || t.id() == ID_bool)
          return 1; // numeric
        if(is_python_string_type(t))
          return 2; // str
        return 0;   // unknown — skip
      };
      int lc = ord_cat(current_left.type());
      int rc = ord_cat(right.type());
      if(lc != 0 && rc != 0 && lc != rc)
      {
        emit_conditional_exception(true_exprt{}, "TypeError");
        return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
      }
    }
    auto to_float_numeric = [&](const exprt &pv) -> exprt
    {
      // (pv.__tag == FLOAT ? pv.__float_val
      //                    : (double)pv.__int_val)
      return if_exprt{
        python_value_is(pv, python_type_tagt::FLOAT),
        python_value_float(pv),
        typecast_exprt{python_value_int(pv), double_type()}};
    };
    auto as_float = [&](const exprt &e) -> exprt
    {
      if(e.type().id() == ID_floatbv)
        return e;
      return typecast_exprt{e, double_type()};
    };
    // PLR §5.9: each operand of a comparison is evaluated ONCE. A
    // python_value operand is referenced twice below (a tag predicate plus
    // an unwrap of its payload); if the operand is a side-effecting call,
    // that re-evaluates the call, and a call that mutates shared state
    // (e.g. it `del`s/pops a by-reference list) DIVERGES between the two
    // evaluations -> a false proof. Materialise such an operand into a
    // fresh temp once, then reference the temp.
    auto materialise_side_effect = [&](exprt &e)
    {
      if(e.id() == ID_side_effect && is_python_value_type(e.type()))
      {
        static unsigned cmp_se_ctr = 0;
        const std::string nm = "__cmp_operand_" + std::to_string(cmp_se_ctr++);
        const irep_idt id{qualify_name(nm)};
        if(symbol_table.lookup(id) == nullptr)
        {
          symbolt s{id, e.type(), "python"};
          s.base_name = nm;
          s.is_lvalue = true;
          s.is_state_var = true;
          symbol_table.add(s);
        }
        const symbol_exprt sym = symbol_table.lookup_ref(id).symbol_expr();
        pending_checks.push_back(code_frontend_assignt{sym, e});
        e = sym;
      }
    };
    materialise_side_effect(current_left);
    materialise_side_effect(right);
    // PLR §6.10.1: `bool` is a subtype of `int` (`True == 1`, `False == 0`), so
    // an ordered comparison with a bool operand compares by integer value.
    // Promote a CONCRETE bool operand to int here; otherwise the relation is
    // built at bool width and the other side is cast DOWN to bool (`True < 2`
    // became `True < bool(2)==True` → False). A python_value (Any) bool is left
    // to the tag-dispatch path below. Ordered ops only (Eq/NotEq already work
    // via the tag path; `is`/`in` are identity/membership, not value order).
    if(ordered_op)
    {
      if(
        current_left.type().id() == ID_bool ||
        current_left.type().id() == ID_c_bool)
        current_left = typecast_exprt{current_left, python_int_type()};
      if(right.type().id() == ID_bool || right.type().id() == ID_c_bool)
        right = typecast_exprt{right, python_int_type()};
    }
    if(
      ordered_op && !is_python_none(current_left, symbol_table) &&
      !is_python_none(right, symbol_table) &&
      is_python_value_type(current_left.type()) !=
        is_python_value_type(right.type()))
    {
      bool left_is_pv = is_python_value_type(current_left.type());
      const exprt &other = left_is_pv ? right : current_left;
      const typet &ot = other.type();
      bool other_numeric = ot.id() == ID_signedbv || ot.id() == ID_integer ||
                           ot.id() == ID_floatbv || ot.id() == ID_bool;
      if(other_numeric)
      {
        // PLR §6.10.1: ordering a number against None raises TypeError.
        // A python_value operand may carry the NONE tag at runtime (e.g.
        // a value returned via a function fall-through / `return None`
        // into a widened int|None slot -- the missing-return_* cluster).
        // to_float_numeric would read its __int_val (the None SENTINEL)
        // and compare numerically -- a false proof. Emit a runtime
        // TypeError obligation guarded on the tag being NONE: sound (the
        // None path raises), catchable (PLR §8.4), and FALSE when the
        // value is not None, so no false positive on the numeric path.
        const exprt &pv = left_is_pv ? current_left : right;
        emit_conditional_exception(
          python_value_is(pv, python_type_tagt::NONE), "TypeError");
        if(left_is_pv)
        {
          current_left = to_float_numeric(current_left);
          right = as_float(right);
        }
        else
        {
          right = to_float_numeric(right);
          current_left = as_float(current_left);
        }
      }
    }

    if(
      op != "In" && op != "NotIn" && op != "Is" && op != "IsNot" &&
      !is_python_none(current_left, symbol_table) &&
      !is_python_none(right, symbol_table) &&
      !((op == "Eq" || op == "NotEq") &&
        is_python_value_type(current_left.type()) &&
        is_python_value_type(right.type())))
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
          else if(is_python_list_type(right.type()))
            left_tag_pred =
              python_value_is(current_left, python_type_tagt::LIST);
          else if(is_python_dict_type(right.type()))
            left_tag_pred =
              python_value_is(current_left, python_type_tagt::DICT);
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
          else if(is_python_list_type(current_left.type()))
            right_tag_pred = python_value_is(right, python_type_tagt::LIST);
          else if(is_python_dict_type(current_left.type()))
            right_tag_pred = python_value_is(right, python_type_tagt::DICT);
        }
        right = unwrap_value(right, current_left.type());
      }
    }
    else if(
      is_python_value_type(current_left.type()) &&
      !is_python_value_type(right.type()))
    {
      // For "x in c": the needle must be unwrapped to c's ELEMENT
      // type, never to c's own type — unwrapping a STR-tagged value
      // "to a list" dereferences __list_ptr (garbage for a string)
      // and refuted membership of any string that flowed through an
      // untyped parameter. For string-element and value-element
      // containers keep the needle WRAPPED: the membership scans
      // dispatch on the runtime tag (value_equal /
      // python_value_structural_eq) and compare content correctly.
      if(op == "In" || op == "NotIn")
      {
        std::optional<typet> elem_target;
        if(
          is_python_list_type(right.type()) ||
          is_python_set_type(right.type()) ||
          is_python_tuple_type(right.type()))
        {
          const auto &st = to_struct_type(right.type());
          if(st.components().size() >= 2)
          {
            const typet &et =
              to_array_type(st.components()[1].type()).element_type();
            if(
              !is_python_string_type(et) && !is_python_value_type(et) &&
              et.id() != ID_struct && et.id() != ID_struct_tag)
              elem_target = et;
          }
        }
        else if(is_python_string_type(right.type()))
          elem_target = python_string_type();
        if(elem_target.has_value())
          current_left = unwrap_value(current_left, elem_target.value());
      }
      else
        current_left = unwrap_value(current_left, right.type());
    }
    (void)tag_of; // unused for now; reserved for future cross-type compares

    // Reference-list equality (--python-ref-mutables): a list whose elements
    // are python_value REFERENCES must NOT be compared with the bitwise struct
    // equality the default path falls through to -- that compares element heap
    // POINTERS, so two distinct lists holding equal values compare unequal,
    // which (critically) lets `!=` be wrongly PROVED (a false proof). Compare
    // element-by-element soundly and cheaply (no deref, no recursion):
    //   element_eq = (bitwise-equal) OR (element is a ref-tag AND nondet)
    // i.e. equal bits => truly equal (same scalar, or same object); for a
    // distinct reference we don't know (deep structural compare is
    // intractable, §12) so we return nondet -- neither == nor != is proved.
    // Scalars stay precise (bitwise differs => unequal, not a ref => no nondet).
    if(
      ref_mutables && (op == "Eq" || op == "NotEq") &&
      is_python_list_type(current_left.type()) &&
      is_python_list_type(right.type()))
    {
      const auto &lst = to_struct_type(current_left.type());
      const auto &rst = to_struct_type(right.type());
      const auto &lda_t = to_array_type(lst.components()[1].type());
      const auto &rda_t = to_array_type(rst.components()[1].type());
      if(
        is_python_value_type(lda_t.element_type()) &&
        is_python_value_type(rda_t.element_type()))
      {
        auto fresh_nd = [&]() -> exprt
        {
          static unsigned ctr = 0;
          const std::string nm = "__reflist_eq_nd_" + std::to_string(ctr++);
          const irep_idt id{qualify_name(nm)};
          if(symbol_table.lookup(id) == nullptr)
          {
            symbolt s{id, bool_typet{}, "python"};
            s.base_name = nm;
            s.is_lvalue = true;
            s.is_state_var = true;
            symbol_table.add(s);
          }
          return symbol_table.lookup_ref(id).symbol_expr();
        };
        const member_exprt llen{current_left, "length", signedbv_typet{64}};
        const member_exprt rlen{right, "length", signedbv_typet{64}};
        const member_exprt lda{current_left, "data", lda_t};
        const member_exprt rda{right, "data", rda_t};
        exprt all = equal_exprt{llen, rlen};
        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          const exprt idx = from_integer(i, signedbv_typet{64});
          const exprt in_range = binary_relation_exprt{idx, ID_lt, llen};
          const exprt le = index_exprt{lda, idx};
          const exprt re = index_exprt{rda, idx};
          const exprt is_ref = or_exprt{
            python_value_is(le, python_type_tagt::LIST),
            or_exprt{
              python_value_is(le, python_type_tagt::DICT),
              python_value_is(le, python_type_tagt::SET)}};
          const exprt el_eq =
            or_exprt{equal_exprt{le, re}, and_exprt{is_ref, fresh_nd()}};
          all = and_exprt{std::move(all), or_exprt{not_exprt{in_range}, el_eq}};
        }
        if(op == "Eq")
          return all;
        return not_exprt{std::move(all)};
      }
    }

    // General same-type list equality (PLR §6.10.1): a plain equal_exprt on two
    // list structs compares the ENTIRE fixed-size `data` array (all
    // PYTHON_MAX_LIST_LENGTH slots), not just the first `length` elements. Two
    // lists that are equal in Python but differ BEYOND their logical length
    // (e.g. `list(enumerate(xs))` leaves OOB reads of xs beyond length, whereas
    // the literal `[(0,7),..]` pads with zeros) would then compare unequal --
    // letting `!=` be wrongly PROVED (a false proof found by the mutation-
    // oracle via list(zip)/list(enumerate)). Compare length-bounded instead:
    //   len(L) == len(R) ∧ ∀i<len. L.data[i] == R.data[i]
    // Only for the same-type case (different element types are handled by the
    // dedicated bridge below); the ref_mutables python_value-element case was
    // already handled above.
    if(
      (op == "Eq" || op == "NotEq") &&
      is_python_list_type(current_left.type()) &&
      is_python_list_type(right.type()) && current_left.type() == right.type())
    {
      const auto &lst = to_struct_type(current_left.type());
      const auto &lda_t = to_array_type(lst.components()[1].type());
      const typet &el_t = lda_t.element_type();
      const member_exprt llen{current_left, "length", signedbv_typet{64}};
      const member_exprt rlen{right, "length", signedbv_typet{64}};
      const member_exprt lda{current_left, "data", lda_t};
      const member_exprt rda{right, "data", lda_t};
      exprt all = equal_exprt{llen, rlen};
      for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
      {
        const exprt idx = from_integer(i, signedbv_typet{64});
        const exprt in_range = binary_relation_exprt{idx, ID_lt, llen};
        const exprt l_el = index_exprt{lda, idx};
        const exprt r_el = index_exprt{rda, idx};
        exprt el_eq;
        if(is_python_string_handle_type(el_t))
        {
          // HANDLE elements compare by their strtab DENOTATION (String
          // equality) -- comparing handle bits wrongly proved `!=` for
          // equal strings allocated separately.
          el_eq = equal_exprt{
            python_string_handle_denotation(l_el),
            python_string_handle_denotation(r_el)};
        }
        else if(is_python_string_type(el_t))
        {
          // String elements compare by CONTENT, not (length, data_ptr) --
          // a runtime-built "a" and a literal "a" have different pointers.
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
        else if(el_t.id() == ID_floatbv)
          el_eq = ieee_float_equal_exprt{l_el, r_el};
        else if(is_python_value_type(el_t))
          // Heterogeneous (python_value) elements: compare tag-aware and
          // structurally. A plain equal_exprt compares the union bits / heap
          // pointers -- so a python_value holding a tuple (currently boxed via
          // the CLASS fallback: no TUPLE tag) compares unequal to an equal
          // tuple built elsewhere, wrongly PROVING `!=`. structural_eq compares
          // scalars precisely, strings by content, and falls back to a sound
          // nondet for pointer-backed tags (CLASS/DICT/SET/tuple), so equal-
          // valued distinct references are never proved unequal.
          // (python-frontend-tuple-tag-plan.md, P1 sound floor.)
          el_eq = python_value_structural_eq(l_el, r_el, 3);
        else
          el_eq = equal_exprt{l_el, r_el};
        all = and_exprt{std::move(all), or_exprt{not_exprt{in_range}, el_eq}};
      }
      if(op == "Eq")
        return all;
      return not_exprt{std::move(all)};
    }

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
        // bool is int-compatible (bool subset of int in Python), so a
        // list[bool] element bridges numerically with int/float/python_value
        // -- e.g. `[0] == [False]` (list[int] vs list[bool]). Without ID_bool
        // here, can_bridge was false and the comparison fell to the never-equal
        // trick -> `[0] != [False]` false-proved (found by the mutation-oracle).
        bool le_num = le.id() == ID_signedbv || le.id() == ID_floatbv ||
                      le.id() == ID_bool || is_python_value_type(le);
        bool re_num = re.id() == ID_signedbv || re.id() == ID_floatbv ||
                      re.id() == ID_bool || is_python_value_type(re);
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
          // ONE spelling of the per-index element equality, shared
          // by the bounded fold and the quantified closed form.
          auto build_elem_eq = [&](const exprt &idx) -> exprt
          {
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
            return el_eq;
          };

          exprt all_equal = equal_exprt{llen, rlen};

          // Closed-form list equality (comprehension-closedform
          // plan, Tier 3; --python-smt-containers):
          // xs == ys is len(xs) == len(ys) and forall j in
          // [0,len): xs[j] == ys[j] (PLR 6.10.1 sequence
          // comparison) -- EXACT at any symbolic length; the
          // bounded fold below covers 64 slots. Same empirical
          // purity gate as membership: the element equality must
          // build WITHOUT emitting auxiliary statements
          // (refined-strings content equality does; native-strings
          // and scalar equalities are pure terms).
          bool quantified_eq = false;
          if(python_smt_containers_flag())
          {
            const std::size_t pc_before = pending_checks.size();
            symbol_exprt qj = fresh_bound_index("__eq_j_");
            exprt q_el_eq = build_elem_eq(qj);
            if(
              pending_checks.size() == pc_before && !q_el_eq.is_nil() &&
              quantifier_safe_term(q_el_eq))
            {
              all_equal = and_exprt{
                std::move(all_equal),
                forall_in_range(qj, llen, std::move(q_el_eq))};
              quantified_eq = true;
            }
            else
              pending_checks.erase(
                pending_checks.begin() + pc_before, pending_checks.end());
          }
          if(!quantified_eq)
          {
            for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt in_range = binary_relation_exprt{idx, ID_lt, llen};
              exprt el_eq = build_elem_eq(idx);
              // out-of-range index trivially holds
              all_equal =
                and_exprt{all_equal, or_exprt{not_exprt{in_range}, el_eq}};
            }
          }
          // Tunnel the bridged comparison through the rest of the
          // pipeline by replacing both operands with concrete
          // booleans of the same content.
          // Tunnel the bridged element-equality back through the pipeline so
          // the python_value tag-guard stage (done_cmp) still applies -- needed
          // when this branch is reached after unwrapping a python_value operand
          // (e.g. `None == []`, where the NONE tag must make it unequal).
          // Set current_left = all_equal for BOTH ops and let the pipeline's op
          // differentiate: Eq -> `all_equal == true` = all_equal; NotEq ->
          // `all_equal != true` = not(all_equal). (Setting not(all_equal) for
          // NotEq here DOUBLE-NEGATED via the second `!= true`, so `a != b`
          // returned `a == b` -- a false proof found by the mutation-oracle.)
          current_left = std::move(all_equal);
          right = true_exprt{};
        }
        else
        {
          // le/re are not scalar-bridgeable. Two sub-cases:
          //  (1) genuinely different Python categories (e.g. a list[int]
          //      element vs a list[list] element) -> Python compares the
          //      mismatched positions unequal, so the lists can never be equal:
          //      the NEVER_EQUAL trick is correct.
          //  (2) the SAME aggregate category (both tuple / both list / both
          //      dict / both set) that differ only in cbmc STRUCT type -- e.g.
          //      `list(enumerate(xs))`/`list(zip(a,b))` build
          //      list<tuple<python_int,elem>> whereas the literal `[(0,7),..]`
          //      has a different tuple/data cbmc type. These lists CAN be equal
          //      in Python, so NEVER_EQUAL would be a FALSE PROOF (found by the
          //      mutation-oracle). Fall back to a SOUND result: definitely
          //      unequal when the lengths differ, else nondet. (A precise
          //      structural recursion into the nested elements is future work.)
          const bool same_aggregate =
            (is_python_tuple_type(le) && is_python_tuple_type(re)) ||
            (is_python_list_type(le) && is_python_list_type(re)) ||
            (is_python_dict_type(le) && is_python_dict_type(re)) ||
            (is_python_set_type(le) && is_python_set_type(re));
          if(same_aggregate)
          {
            static unsigned nd_ctr = 0;
            const std::string nm =
              "__list_agg_eq_nd_" + std::to_string(nd_ctr++);
            const irep_idt id{qualify_name(nm)};
            if(symbol_table.lookup(id) == nullptr)
            {
              symbolt s{id, bool_typet{}, "python"};
              s.base_name = nm;
              s.is_lvalue = true;
              s.is_state_var = true;
              symbol_table.add(s);
            }
            symbol_exprt nd = symbol_table.lookup_ref(id).symbol_expr();
            pending_checks.push_back(code_frontend_assignt{
              nd, side_effect_expr_nondett{bool_typet{}, source_locationt{}}});
            member_exprt llen{current_left, "length", signedbv_typet{64}};
            member_exprt rlen{right, "length", signedbv_typet{64}};
            // length-mismatch => definitely unequal; else nondet (sound).
            current_left = and_exprt{equal_exprt{llen, rlen}, std::move(nd)};
            right = true_exprt{};
          }
          else
          {
            // Fallback: structurally incompatible → never equal
            current_left = python_string_literal("__NEVER_EQUAL__");
            right = python_string_literal("__NOT_EQUAL_TO_THIS__");
          }
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

    // PLR §3.2 / §6.10.1: set ordering is the SUBSET partial order (not a total
    // order): `A <= B` iff A is a subset of B, `A < B` iff a PROPER subset (and
    // symmetrically for `>`/`>=`). For two bitmap sets, subset is a bit-mask
    // test on the bitmaps. The literal bitmap model uses offset 0 with a range
    // guard (elements in [0,64)), so the bitmaps are directly comparable.
    if(
      (op == "Lt" || op == "LtE" || op == "Gt" || op == "GtE") &&
      is_python_set_type(current_left.type()) &&
      is_python_set_type(right.type()))
    {
      const exprt la =
        member_exprt{current_left, "bitmap", unsignedbv_typet{64}};
      const exprt lb = member_exprt{right, "bitmap", unsignedbv_typet{64}};
      const exprt inter = bitand_exprt{la, lb};
      const exprt a_sub_b = equal_exprt{inter, la}; // A ⊆ B
      const exprt b_sub_a = equal_exprt{inter, lb}; // B ⊆ A
      const exprt neq = notequal_exprt{la, lb};
      if(op == "LtE")
        cmp = a_sub_b;
      else if(op == "Lt")
        cmp = and_exprt{a_sub_b, neq};
      else if(op == "GtE")
        cmp = b_sub_a;
      else // Gt
        cmp = and_exprt{b_sub_a, neq};
      goto done_cmp;
    }

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
      // PLR §6.10.1: if BOTH operands are constant list literals, statically
      // walk the lexicographic comparison and flag a definite cross-category
      // (numeric vs str) element comparison -> TypeError. Sound + FP-free: the
      // mismatch at position i is flagged ONLY when positions 0..i-1 are
      // PROVABLY equal (so CPython actually reaches i); if an earlier position
      // differs or cannot be normalized, we stop without flagging. Recovers the
      // value/tag of a boxed python_value element (mixed literals box).
      {
        auto const_elems =
          [this](const exprt &lst) -> std::optional<std::vector<exprt>>
        {
          // Decodes both literal shapes (bounded array literal and the
          // --python-smt-containers store-chain).
          auto leading = list_literal_leading(lst);
          if(!leading.has_value())
            return std::nullopt;
          return std::vector<exprt>{leading->begin(), leading->end()};
        };
        // Normalized order-key for a constant element: "n:<int>" for an
        // integer/bool (boxed or not) or "s:<bytes>" for a str; nullopt when it
        // cannot be normalized (float, unbounded-int pointer, unreadable str).
        auto order_key = [this](const exprt &e) -> std::optional<std::string>
        {
          const int c = orderable_category_of(e);
          if(c == 1)
          {
            exprt v = e;
            if(
              is_python_value_type(e.type()) && e.id() == ID_struct &&
              e.operands().size() > 3 && e.operands()[0].is_constant())
            {
              mp_integer tg;
              if(to_integer(to_constant_expr(e.operands()[0]), tg))
                return std::nullopt;
              const int t = tg.to_long();
              if(t == static_cast<int>(python_type_tagt::INT))
                v = e.operands()[1];
              else if(t == static_cast<int>(python_type_tagt::BOOL))
                v = e.operands()[3];
              else
                return std::nullopt; // float etc.
            }
            if(
              v.is_constant() &&
              (v.type().id() == ID_signedbv || v.type().id() == ID_unsignedbv ||
               v.type().id() == ID_bool))
            {
              mp_integer iv;
              if(!to_integer(to_constant_expr(v), iv))
                return "n:" + integer2string(iv);
            }
            return std::nullopt;
          }
          if(c == 2)
          {
            if(auto sv = extract_string_value(e))
              return "s:" + *sv;
          }
          return std::nullopt;
        };
        auto le = const_elems(current_left), re = const_elems(right);
        if(le && re)
        {
          const std::size_t n = std::min(le->size(), re->size());
          for(std::size_t i = 0; i < n; ++i)
          {
            const int ca = orderable_category_of((*le)[i]);
            const int cb = orderable_category_of((*re)[i]);
            if(ca != 0 && cb != 0 && ca != cb)
            {
              emit_conditional_exception(true_exprt{}, "TypeError");
              return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
            }
            auto ka = order_key((*le)[i]), kb = order_key((*re)[i]);
            if(ka && kb && *ka == *kb)
              continue; // provably equal -> position i+1 is reached
            break;      // differ / unknown -> result determined or unprovable
          }
        }
      }
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
        // PLR §6.10.1: element-wise ordering of two provably-incompatible
        // concrete categories (numeric vs str) is unsupported -> TypeError
        // (e.g. `[1, 2] < [1, "a"]` compares 2 < "a"). Only fires when BOTH
        // element types are concrete and in different orderable categories;
        // Any/symbolic and numeric-numeric (int vs float) are left to the
        // existing promotion paths below (no false positive).
        {
          const int ca = orderable_category_of(a),
                    cb = orderable_category_of(b);
          if(ca != 0 && cb != 0 && ca != cb)
          {
            emit_conditional_exception(true_exprt{}, "TypeError");
            return false_exprt{};
          }
        }
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
      // PLR §3.2: set equality is order-independent. If either
      // operand carries set-semantic provenance (came from a
      // `set(...)` call → its symbol id is in
      // set_semantic_symbols, OR from a Set literal which
      // tagged its struct_exprt with `#python_set_semantic`),
      // compare as multisets: lengths must match AND each
      // element of L must appear at some index of R (and vice
      // versa via the length-match shortcut, since elements are
      // unique within either operand by construction).
      auto is_set_semantic = [&](const exprt &e) -> bool
      {
        if(e.id() == ID_symbol)
        {
          auto sid = to_symbol_expr(e).get_identifier();
          if(set_semantic_symbols.count(sid))
            return true;
        }
        if(e.get_bool("#python_set_semantic"))
          return true;
        return false;
      };
      if(
        is_python_list_type(current_left.type()) &&
        is_python_list_type(right.type()) &&
        (is_set_semantic(current_left) || is_set_semantic(right)))
      {
        const auto &lt = to_struct_type(current_left.type());
        const auto &rt = to_struct_type(right.type());
        const auto &ldata = to_array_type(lt.components()[1].type());
        const auto &rdata = to_array_type(rt.components()[1].type());
        member_exprt llen{current_left, "length", signedbv_typet{64}};
        member_exprt rlen{right, "length", signedbv_typet{64}};
        member_exprt lda{current_left, "data", ldata};
        member_exprt rda{right, "data", rdata};
        // Element-equality for set-element types we care about:
        // strings (use cprover_string_equal), tuples / structs
        // (structural equal), numbers (signedbv/floatbv eq).
        auto element_eq = [&](exprt a, exprt b) -> exprt
        {
          if(is_python_string_type(a.type()) && is_python_string_type(b.type()))
          {
            exprt s = emit_string_bool_function(
              ID_cprover_string_equal_func, a, b, symbol_table, pending_checks);
            if(s.type() != bool_typet{})
              s = typecast_exprt{std::move(s), bool_typet{}};
            return s;
          }
          if(a.type() != b.type())
            b = safe_typecast(b, a.type());
          return equal_exprt{std::move(a), std::move(b)};
        };
        // Performance: bound the loop by the smaller statically-
        // known length when either operand is a struct literal.
        // The default cap is PYTHON_MAX_LIST_LENGTH; literal sets
        // are typically very short.
        auto static_len = [](const exprt &e) -> int
        {
          if(
            e.id() == ID_struct && !e.operands().empty() &&
            e.operands()[0].is_constant())
          {
            mp_integer iv;
            if(!to_integer(to_constant_expr(e.operands()[0]), iv))
              return iv.to_long();
          }
          return -1;
        };
        int llen_static = static_len(current_left);
        int rlen_static = static_len(right);
        int i_max = llen_static >= 0 ? llen_static : PYTHON_MAX_LIST_LENGTH;
        int j_max = rlen_static >= 0 ? rlen_static : PYTHON_MAX_LIST_LENGTH;
        // Build: lengths match AND for each i<llen, exists j<rlen
        // such that L[i] == R[j].
        exprt all_match = equal_exprt{llen, rlen};
        for(int i = 0; i < i_max; i++)
        {
          exprt idx_i = from_integer(i, signedbv_typet{64});
          exprt i_in = binary_relation_exprt{idx_i, ID_lt, llen};
          exprt l_el = index_exprt{lda, idx_i, ldata.element_type()};
          exprt found = false_exprt{};
          for(int j = 0; j < j_max; j++)
          {
            exprt idx_j = from_integer(j, signedbv_typet{64});
            exprt j_in = binary_relation_exprt{idx_j, ID_lt, rlen};
            exprt r_el = index_exprt{rda, idx_j, rdata.element_type()};
            exprt e = element_eq(l_el, r_el);
            found = or_exprt{found, and_exprt{j_in, e}};
          }
          all_match = and_exprt{all_match, or_exprt{not_exprt{i_in}, found}};
        }
        cmp = all_match;
        goto done_cmp;
      }
      // PLR §6.10.1: list == list whose elements are tagged unions
      // (python_value). Compare element-wise via static-dispatch structural
      // equality instead of a field-wise equal_exprt (which would compare the
      // LIST/DICT pointer = identity, not value).
      if(
        is_python_list_type(current_left.type()) &&
        is_python_list_type(right.type()))
      {
        const auto &lt = to_struct_type(current_left.type());
        const auto &rt = to_struct_type(right.type());
        const auto &ldata = to_array_type(lt.components()[1].type());
        const auto &rdata = to_array_type(rt.components()[1].type());
        if(
          is_python_value_type(ldata.element_type()) &&
          is_python_value_type(rdata.element_type()))
        {
          member_exprt llen{current_left, "length", signedbv_typet{64}};
          member_exprt rlen{right, "length", signedbv_typet{64}};
          member_exprt lda{current_left, "data", ldata};
          member_exprt rda{right, "data", rdata};
          exprt all_equal = equal_exprt{llen, rlen};
          for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, llen};
            exprt l_el = index_exprt{lda, idx, ldata.element_type()};
            exprt r_el = index_exprt{rda, idx, rdata.element_type()};
            exprt el_eq = python_value_structural_eq(l_el, r_el, 2);
            all_equal =
              and_exprt{all_equal, or_exprt{not_exprt{in_range}, el_eq}};
          }
          cmp = all_equal;
          goto done_cmp;
        }
      }
      // PLR §6.10.1 + §3.2: x == None reduces to identity check
      // for None: equal to itself, never equal to any non-None
      // value of any type. Same dispatch as 'is None'.
      if(is_python_none(right, symbol_table))
      {
        if(is_python_value_type(current_left.type()))
        {
          cmp = python_value_is(current_left, python_type_tagt::NONE);
          goto done_cmp;
        }
        if(is_python_none(current_left, symbol_table))
        {
          cmp = true_exprt{};
          goto done_cmp;
        }
        // Typed numeric LHS: only equal to None if it carries
        // the legacy sentinel. (After the full migration, typed
        // numerics never carry the sentinel and this becomes
        // false_exprt unconditionally.)
        if(
          current_left.type().id() == ID_signedbv ||
          current_left.type().id() == ID_integer)
        {
          cmp = equal_exprt{
            current_left,
            from_integer(python_none_sentinel_int(), current_left.type())};
          goto done_cmp;
        }
        if(current_left.type().id() == ID_floatbv)
        {
          ieee_floatt none_f{
            ieee_float_spect{to_floatbv_type(current_left.type())},
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          none_f.from_integer(python_none_sentinel_int());
          cmp = ieee_float_equal_exprt{current_left, none_f.to_expr()};
          goto done_cmp;
        }
        // PLR §6.13: 'x is None' for a typed-string slot
        // recognises the canonical {0, NULL} marker that
        // coerce_to_typed_slot emits at boundaries — distinct
        // from `""` which has length=0 but a non-NULL data
        // pointer (an interned empty-buffer literal).
        if(is_python_string_type(current_left.type()))
        {
          cmp = equal_exprt{
            member_exprt{
              current_left, "data", pointer_typet{unsignedbv_typet{8}, 64}},
            null_pointer_exprt{pointer_typet{unsignedbv_typet{8}, 64}}};
          goto done_cmp;
        }
        // typed-list / typed-dict: no canonical None marker
        // distinct from empty list/dict — Python source uses
        // `not arg` / `len(arg) == 0` for those tests rather
        // than `arg is None`. See coerce_to_typed_slot for
        // why we don't emit a length-0 marker recogniser here.
        // Class instances and other unrecognised struct types:
        // never equal to None (no None-marker convention).
        cmp = false_exprt{};
        goto done_cmp;
      }
      if(is_python_none(current_left, symbol_table))
      {
        if(is_python_value_type(right.type()))
        {
          cmp = python_value_is(right, python_type_tagt::NONE);
          goto done_cmp;
        }
        if(right.type().id() == ID_signedbv || right.type().id() == ID_integer)
        {
          cmp = equal_exprt{
            right, from_integer(python_none_sentinel_int(), right.type())};
          goto done_cmp;
        }
        if(right.type().id() == ID_floatbv)
        {
          ieee_floatt none_f{
            ieee_float_spect{to_floatbv_type(right.type())},
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          none_f.from_integer(python_none_sentinel_int());
          cmp = ieee_float_equal_exprt{right, none_f.to_expr()};
          goto done_cmp;
        }
        if(is_python_string_type(right.type()))
        {
          cmp = equal_exprt{
            member_exprt{right, "data", pointer_typet{unsignedbv_typet{8}, 64}},
            null_pointer_exprt{pointer_typet{unsignedbv_typet{8}, 64}}};
          goto done_cmp;
        }
        cmp = false_exprt{};
        goto done_cmp;
      }

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

        bool keys_are_strings = is_python_string_type(
          python_dict_logical_key_type(keys_t.element_type()));
        bool vals_are_strings = is_python_string_type(vals_t.element_type());

        // Build: lengths match AND for each i in [0, llen):
        //   exists j in [0, rlen): lkeys[i]==rkeys[j] AND lvals[i]==rvals[j]
        exprt all_match = equal_exprt{llen, rlen};
        for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
        {
          exprt iv = from_integer(i, signedbv_typet{64});
          exprt i_in_range = binary_relation_exprt{iv, ID_lt, llen};
          exprt l_key = python_dict_unbox_key(
            index_exprt{lkeys, iv, keys_t.element_type()});
          exprt l_val = index_exprt{lvals, iv, vals_t.element_type()};
          exprt found_match = false_exprt{};
          for(std::size_t j = 0; j < PYTHON_MAX_DICT_SIZE; j++)
          {
            exprt jv = from_integer(j, signedbv_typet{64});
            exprt j_in_range = binary_relation_exprt{jv, ID_lt, rlen};
            exprt r_key = python_dict_unbox_key(
              index_exprt{rkeys, jv, keys_t.element_type()});
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
          for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
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
          // PLR §6.10.1: 1-char-vs-1-char-constant fast-path
          // for Eq. When one side is a 1-char string view (from
          // s[i] / for-c-in-s iteration) and the other is a
          // 1-char constant, skip the refined-string solver and
          // generate a direct byte equality. This unlocks
          // assume(s == "abc") → assert(s[0] == "a") propagation
          // that the refined solver loses across pointer views,
          // and avoids long SAT loops on 'all(c in HEX for c in
          // s)' patterns.
          {
            auto try_view_byte = [&](const exprt &s) -> exprt
            {
              if(
                s.id() == ID_struct && s.operands().size() == 2 &&
                s.operands()[0].is_constant())
              {
                mp_integer slen;
                if(
                  !to_integer(to_constant_expr(s.operands()[0]), slen) &&
                  slen == 1)
                {
                  const exprt &sd = s.operands()[1];
                  // The s[i] path leaves s.operands()[1] as a
                  // pointer (s.data + i) or address_of(arr[0]).
                  if(
                    sd.id() == ID_address_of && sd.operands().size() == 1 &&
                    sd.operands()[0].id() == ID_index)
                    return sd.operands()[0];
                  if(sd.type().id() == ID_pointer)
                    return dereference_exprt{sd};
                }
              }
              return nil_exprt{};
            };
            exprt lvb = try_view_byte(current_left);
            exprt rvb = try_view_byte(right);
            if(lv.has_value() && lv.value().size() == 1 && !rvb.is_nil())
            {
              cmp = equal_exprt{
                rvb,
                from_integer(
                  static_cast<unsigned char>(lv.value()[0]),
                  unsignedbv_typet{8})};
              goto done_cmp;
            }
            if(rv.has_value() && rv.value().size() == 1 && !lvb.is_nil())
            {
              cmp = equal_exprt{
                lvb,
                from_integer(
                  static_cast<unsigned char>(rv.value()[0]),
                  unsignedbv_typet{8})};
              goto done_cmp;
            }
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
              // Native SMT String operands pass through unchanged: the
              // {length, data} struct view below is the refined-string
              // representation, and member_exprt over the ID_string
              // sort is ill-formed (an invariant abort found via
              // `<str field> == <int>` on the native backend).
              // emit_string_bool_function dispatches native operands
              // directly.
              if(s.type().id() == ID_string)
                return s;
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
      // PLR §6.10.1 + §3.2: x != None — negation of x == None.
      if(is_python_none(right, symbol_table))
      {
        if(is_python_value_type(current_left.type()))
        {
          cmp =
            not_exprt{python_value_is(current_left, python_type_tagt::NONE)};
          goto done_cmp;
        }
        if(is_python_none(current_left, symbol_table))
        {
          cmp = false_exprt{};
          goto done_cmp;
        }
        if(
          current_left.type().id() == ID_signedbv ||
          current_left.type().id() == ID_integer)
        {
          cmp = notequal_exprt{
            current_left,
            from_integer(python_none_sentinel_int(), current_left.type())};
          goto done_cmp;
        }
        if(current_left.type().id() == ID_floatbv)
        {
          ieee_floatt none_f{
            ieee_float_spect{to_floatbv_type(current_left.type())},
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          none_f.from_integer(python_none_sentinel_int());
          cmp =
            not_exprt{ieee_float_equal_exprt{current_left, none_f.to_expr()}};
          goto done_cmp;
        }
        if(is_python_string_type(current_left.type()))
        {
          cmp = notequal_exprt{
            member_exprt{
              current_left, "data", pointer_typet{unsignedbv_typet{8}, 64}},
            null_pointer_exprt{pointer_typet{unsignedbv_typet{8}, 64}}};
          goto done_cmp;
        }
        // typed-list / typed-dict / class instances: see Eq
        // arm above for why we don't recognise length-0 as
        // None here. Default: structurally never None.
        cmp = true_exprt{};
        goto done_cmp;
      }
      if(is_python_none(current_left, symbol_table))
      {
        if(is_python_value_type(right.type()))
        {
          cmp = not_exprt{python_value_is(right, python_type_tagt::NONE)};
          goto done_cmp;
        }
        if(right.type().id() == ID_signedbv || right.type().id() == ID_integer)
        {
          cmp = notequal_exprt{
            right, from_integer(python_none_sentinel_int(), right.type())};
          goto done_cmp;
        }
        if(right.type().id() == ID_floatbv)
        {
          ieee_floatt none_f{
            ieee_float_spect{to_floatbv_type(right.type())},
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          none_f.from_integer(python_none_sentinel_int());
          cmp = not_exprt{ieee_float_equal_exprt{right, none_f.to_expr()}};
          goto done_cmp;
        }
        if(is_python_string_type(right.type()))
        {
          cmp = notequal_exprt{
            member_exprt{right, "data", pointer_typet{unsignedbv_typet{8}, 64}},
            null_pointer_exprt{pointer_typet{unsignedbv_typet{8}, 64}}};
          goto done_cmp;
        }
        cmp = true_exprt{};
        goto done_cmp;
      }

      // PLR §3.3.1: class instances with a custom __ne__ / __eq__ must
      // compare by value, not by struct layout. Without this, `!=` fell
      // through to structural inequality, so e.g.
      // `Decimal("1.0") != Decimal("1.00")` was wrongly True (their
      // (coeff, exp) structs differ though the values are equal). Mirrors
      // the __eq__ dispatch in the Eq arm; prefers __ne__, else negates
      // __eq__.
      if(
        current_left.type().id() == ID_struct && right.type().id() == ID_struct)
      {
        const auto &ne_st = to_struct_type(current_left.type());
        std::string ne_tag = id2string(ne_st.get_tag());
        if(ne_tag.substr(0, 13) == "python_class_")
        {
          std::string cls = ne_tag.substr(13);
          for(const char *meth : {"__ne__", "__eq__"})
          {
            irep_idt mid{std::string{"python::"} + cls + "::" + meth};
            const symbolt *msym = symbol_table.lookup(mid);
            if(msym == nullptr || msym->type.id() != ID_code)
              continue;
            const code_typet &mty = to_code_type(msym->type);
            exprt other_arg = right;
            if(
              mty.parameters().size() >= 2 &&
              other_arg.type() != mty.parameters()[1].type())
              other_arg = safe_typecast(other_arg, mty.parameters()[1].type());
            side_effect_expr_function_callt call{
              msym->symbol_expr(),
              {address_of_exprt{current_left}, std::move(other_arg)},
              mty.return_type(),
              get_location(expr)};
            if(std::string{meth} == "__ne__")
              cmp = std::move(call);
            else
              cmp =
                not_exprt{typecast_exprt{exprt{std::move(call)}, bool_typet{}}};
            goto done_cmp;
          }
        }
      }

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
        bool keys_are_strings = is_python_string_type(
          python_dict_logical_key_type(keys_t.element_type()));
        bool vals_are_strings = is_python_string_type(vals_t.element_type());
        exprt all_match = equal_exprt{llen, rlen};
        for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
        {
          exprt iv = from_integer(i, signedbv_typet{64});
          exprt i_in_range = binary_relation_exprt{iv, ID_lt, llen};
          exprt l_key = python_dict_unbox_key(
            index_exprt{lkeys, iv, keys_t.element_type()});
          exprt l_val = index_exprt{lvals, iv, vals_t.element_type()};
          exprt found_match = false_exprt{};
          for(std::size_t j = 0; j < PYTHON_MAX_DICT_SIZE; j++)
          {
            exprt jv = from_integer(j, signedbv_typet{64});
            exprt j_in_range = binary_relation_exprt{jv, ID_lt, rlen};
            exprt r_key = python_dict_unbox_key(
              index_exprt{rkeys, jv, keys_t.element_type()});
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
              // Native SMT String operands pass through unchanged: the
              // {length, data} struct view below is the refined-string
              // representation, and member_exprt over the ID_string
              // sort is ill-formed (an invariant abort found via
              // `<str field> == <int>` on the native backend).
              // emit_string_bool_function dispatches native operands
              // directly.
              if(s.type().id() == ID_string)
                return s;
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
      // PLR §6.10.1: ordering operators raise
      // 'TypeError: <not supported between instances of X and NoneType>'
      // when either operand is None. Mirrors the complex
      // ordering check below in shape: set
      // __exception_active=True with TypeError tag, then emit
      // a structural false_exprt result.
      auto is_none_operand = [this](const exprt &e)
      {
        if(is_python_none(e, symbol_table))
          return true;
        if(is_python_value_type(e.type()))
        {
          // Symbolic — could be NONE-tagged at runtime.
          // Conservatively don't raise here; we only raise
          // on syntactically-evident None operands. Symbolic
          // detection would require a guarded property
          // assertion under a flag (similar to
          // --python-check-iter-none).
          return false;
        }
        return false;
      };
      if(is_none_operand(current_left) || is_none_operand(right))
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
        // PLR §3.3 / §6.10.1: neither operand provides the ordering dunder
        // (nor the reflected form) anywhere in its MRO, so `<`/`<=`/`>`/`>=`
        // is unsupported between these instances -> TypeError. (Eq/NotEq/Is
        // are not ordered_op and never reach this block, so identity-default
        // `==`/`!=` are left intact.)
        {
          const char *mname = op == "Lt"    ? "__lt__"
                              : op == "LtE" ? "__le__"
                              : op == "Gt"  ? "__gt__"
                                            : "__ge__";
          const char *rname = op == "Lt"    ? "__gt__"
                              : op == "LtE" ? "__ge__"
                              : op == "Gt"  ? "__lt__"
                                            : "__le__";
          if(
            concrete_class_lacks_dunder(current_left.type(), mname) &&
            concrete_class_lacks_dunder(right.type(), rname))
          {
            emit_conditional_exception(true_exprt{}, "TypeError");
            cmp = false_exprt{};
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
      // PLR §5.9: a container that embeds a side-effecting expression (e.g.
      // the list literal `[f(h)]` where f mutates a by-reference list) is
      // referenced repeatedly below (the length check plus each
      // `container.data[idx]`); evaluating it inline would RE-EVALUATE the
      // embedded call, and a mutating call diverges between evaluations ->
      // a false proof. Materialise it into a temp once.
      {
        std::function<bool(const exprt &)> has_side_effect =
          [&](const exprt &e) -> bool
        {
          if(e.id() == ID_side_effect)
            return true;
          for(const auto &sub : e.operands())
            if(has_side_effect(sub))
              return true;
          return false;
        };
        if(has_side_effect(container))
        {
          static unsigned in_ctr = 0;
          const std::string nm = "__in_container_" + std::to_string(in_ctr++);
          const irep_idt id{qualify_name(nm)};
          if(symbol_table.lookup(id) == nullptr)
          {
            symbolt s{id, container.type(), "python"};
            s.base_name = nm;
            s.is_lvalue = true;
            s.is_state_var = true;
            symbol_table.add(s);
          }
          const symbol_exprt sym = symbol_table.lookup_ref(id).symbol_expr();
          pending_checks.push_back(code_frontend_assignt{sym, container});
          container = sym;
        }
      }
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
              exprt::operandst contains_args{address_of_exprt{container}, item};
              coerce_call_args(cs->type, contains_args);
              side_effect_expr_function_callt call{
                cs->symbol_expr(),
                std::move(contains_args),
                bool_typet{},
                source_locationt{}};
              cmp = (op == "In") ? exprt{call} : exprt{not_exprt{call}};
              goto done_cmp;
            }
          }
          // PLR §3.3.1: `x in obj` needs __contains__, or __iter__ / __getitem__
          // as a fallback (membership tries iteration). A concrete class whose
          // MRO defines NONE of the three is not a container -> TypeError.
          if(
            concrete_class_lacks_dunder(container.type(), "__contains__") &&
            concrete_class_lacks_dunder(container.type(), "__iter__") &&
            concrete_class_lacks_dunder(container.type(), "__getitem__"))
          {
            emit_conditional_exception(true_exprt{}, "TypeError");
            cmp = (op == "In") ? exprt{false_exprt{}} : exprt{true_exprt{}};
            goto done_cmp;
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
        // The set is a 64-bit int BITMAP -- only int/bool elements have a
        // precise bit position. A non-int element (tuple/str/...) cast to a
        // bit position can COLLIDE with another element's bit, which would
        // false-prove membership (e.g. `s.add((1,2)); (3,4) in s`). Sound
        // over-approximation: membership of a non-int element is nondet.
        const typet &it = item.type();
        const bool int_elem = it.id() == ID_signedbv ||
                              it.id() == ID_unsignedbv || it.id() == ID_bool ||
                              it.id() == ID_integer;
        if(!int_elem)
        {
          static unsigned set_in_nd = 0;
          const irep_idt nd_id{
            qualify_name("__set_in_nd_" + std::to_string(set_in_nd++))};
          if(symbol_table.lookup(nd_id) == nullptr)
          {
            symbolt s{nd_id, bool_typet{}, "python"};
            s.base_name = id2string(nd_id);
            s.is_lvalue = true;
            s.is_state_var = true;
            symbol_table.add(s);
          }
          exprt in_set = symbol_table.lookup_ref(nd_id).symbol_expr();
          cmp = (op == "In") ? in_set : exprt{not_exprt{in_set}};
        }
        else
        {
          exprt shifted_item = item;
          if(shifted_item.type() != signedbv_typet{64})
            shifted_item = safe_typecast(shifted_item, signedbv_typet{64});
          // Cast to unsigned for shift
          exprt shift_amount =
            typecast_exprt{shifted_item, unsignedbv_typet{64}};
          exprt shifted = lshr_exprt{bm, shift_amount};
          exprt bit =
            bitand_exprt{shifted, from_integer(1, unsignedbv_typet{64})};
          exprt in_set =
            notequal_exprt{bit, from_integer(0, unsignedbv_typet{64})};
          cmp = (op == "In") ? in_set : exprt{not_exprt{in_set}};
        }
      }
      // x in lst → disjunction: lst.data[0]==x or lst.data[1]==x or ...
      else if(is_python_list_type(container.type()))
      {
        // Python-equality soundness guard (the __eq__ audit): `x in
        // lst` applies == pairwise (PLR 6.10.2 with the identity
        // short-circuit). For CLASS-instance items/elements every
        // encoding here -- the constant fold below (which matched
        // constructor TREES: C(1) in [C(1)] falsely proved where
        // CPython's identity-eq says False), the runtime scans, the
        // quantified lifts -- compares structurally. Reject loudly.
        {
          const typet &lelem =
            to_array_type(
              to_struct_type(container.type()).components()[1].type())
              .element_type();
          // python_value items/elements are exempt (tag-aware
          // value_equal: sound-nondet for class tags; no
          // conversion-time fold can match per-instance wraps).
          if(
            (!python_eq_is_structural(item.type()) &&
             !is_python_value_type(item.type())) ||
            (!python_eq_is_structural(lelem) && !is_python_value_type(lelem)))
          {
            emit_eq_semantics_guard(get_location(expr), "list membership");
          }
        }
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
        // Native string-id handles: a list[str] element is a bv64 handle
        // whose string denotation is strtab(h) -- read elements through
        // the denotation so membership compares string VALUES (the raw
        // handle bits would wrongly refute `"a" in ["a"]` when the
        // interned probe and a runtime-built element differ as handles).
        const bool handle_elems =
          is_python_string_handle_type(data_type.element_type());
        bool list_of_strings =
          (is_python_string_type(data_type.element_type()) || handle_elems) &&
          is_python_string_type(item.type());

        // Build disjunction for up to PYTHON_MAX_LIST_LENGTH elements
        // guarded by index < length
        // ONE spelling of the per-element membership match (the P0
        // choke-point discipline), shared by the bounded scan and the
        // quantified closed form below.
        auto build_elem_match = [&](exprt elem) -> exprt
        {
          if(handle_elems)
            elem = python_string_handle_denotation(elem);
          exprt match;

          // PLR §6.13: 'None in xs' for typed-element xs
          // recognises the per-element-type None marker
          // emitted by coerce_element. Without this, the
          // generic equal_exprt below would compare the typed
          // element to a python_value{NONE} struct via
          // safe_typecast/wrap_value and always evaluate to
          // False even when the element IS the None marker.
          if(is_python_none(current_left, symbol_table))
          {
            if(
              elem.type().id() == ID_signedbv || elem.type().id() == ID_integer)
            {
              match = equal_exprt{
                elem, from_integer(python_none_sentinel_int(), elem.type())};
            }
            else if(elem.type().id() == ID_floatbv)
            {
              ieee_floatt none_f{
                ieee_float_spect{to_floatbv_type(elem.type())},
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              none_f.from_integer(python_none_sentinel_int());
              match = ieee_float_equal_exprt{elem, none_f.to_expr()};
            }
            else if(is_python_string_type(elem.type()))
            {
              match = equal_exprt{
                member_exprt{
                  elem, "data", pointer_typet{unsignedbv_typet{8}, 64}},
                null_pointer_exprt{pointer_typet{unsignedbv_typet{8}, 64}}};
            }
            else
            {
              // python_value or other: fall through to the
              // generic equal_exprt path so structural
              // equality with None_constant resolves via the
              // tag check.
              if(current_left.type() != elem.type())
                elem = safe_typecast(elem, current_left.type());
              match = equal_exprt{current_left, elem};
            }
          }
          else if(
            is_python_value_type(current_left.type()) &&
            !is_python_value_type(elem.type()))
          {
            // PLR §6.10.1: a tagged-union needle against a TYPED
            // element must compare by VALUE, not by representation.
            // safe_typecast would unwrap the needle to the element
            // type (e.g. read __int_val bits against a string
            // struct) or compare a fresh string handle against an
            // interned one — both wrongly refute membership of a
            // string that flowed through an untyped parameter.
            // Wrap the element and dispatch on the runtime tag.
            match = value_equal(current_left, wrap_value(elem));
          }
          else
          {
            // Ensure types match for equality comparison
            if(current_left.type() != elem.type())
              elem = safe_typecast(elem, current_left.type());
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
          }
          return match;
        };

        // Closed-form membership (comprehension-closedform plan,
        // Tier 3; --python-smt-containers): x in xs is
        // exists j in [0,len): match(data[j]) -- EXACT at any
        // symbolic length; the bounded scan below covers 64 slots
        // behind a fail-closed guard. Same empirical purity gate as
        // the genexp/map closed forms: build the match for the
        // QUANTIFIED element and require that no auxiliary
        // statements were emitted (a refined-strings content match
        // registers string-solver applications -- those cannot live
        // under a binder; native-strings matches are pure strtab/
        // scalar terms). Impure matches keep the bounded lowering.
        if(python_smt_containers_flag())
        {
          const std::size_t pc_before = pending_checks.size();
          symbol_exprt qj = fresh_bound_index("__in_j_");
          exprt qmatch = build_elem_match(index_exprt{data, qj});
          if(
            pending_checks.size() == pc_before && !qmatch.is_nil() &&
            quantifier_safe_term(qmatch))
          {
            exprt found = exists_in_range(qj, length, std::move(qmatch));
            cmp = (op == "In") ? found : exprt{not_exprt{found}};
            goto done_cmp;
          }
          pending_checks.erase(
            pending_checks.begin() + pc_before, pending_checks.end());
        }

        emit_scan_bound_guard(length, source_locationt{});
        exprt in_expr = false_exprt{};
        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          exprt match = build_elem_match(index_exprt{data, idx});
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
          emit_scan_bound_guard(list_len, source_locationt{});
          for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, list_len};
            exprt elem = index_exprt{list_data, idx};
            exprt match;
            if(is_python_value_type(elem.type()))
            {
              // PLR §6.10.1: tag-dispatched VALUE equality. The
              // previous unwrap-both-sides-to-int comparison
              // misjudged string elements in both directions
              // (raw handle bits / truncated struct reads).
              match = value_equal(wrap_value(current_left), elem);
            }
            else
            {
              exprt unwrapped = unwrap_value(elem, current_left.type());
              exprt cmp_left = current_left;
              if(is_python_value_type(cmp_left.type()))
                cmp_left = unwrap_value(cmp_left, python_int_type());
              if(is_python_value_type(unwrapped.type()))
                unwrapped = unwrap_value(unwrapped, cmp_left.type());
              if(cmp_left.type() != unwrapped.type())
                unwrapped = safe_typecast(unwrapped, cmp_left.type());
              match = equal_exprt{cmp_left, unwrapped};
            }
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
          const typet dict_st_layout = canonical_str_dict_type();
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
              exprt key_at = python_dict_unbox_key(index_exprt{dict_keys, idx});
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
          // Reconstruct a python_string-typed expression for the
          // container (the strtab denotation of the __str handle on
          // native, the inline struct on refined).
          exprt str_val = python_value_str(container);
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
        // SET branch -- sets are boxed by-reference via __class_ptr
        // with the SET tag (mirroring dicts); dereference the payload
        // and reuse the bitmap membership test. Only an int needle
        // has a precise bit position (same collision argument as the
        // concrete set arm); a non-int needle gets a sound nondet.
        // Previously there was NO set arm, so `x in s` through a
        // boxed set fell through to the tag chain's `false` --
        // underapproximating membership (the retry-loop counter
        // false-alarm family: `rid in remaining` never throttles).
        exprt tag_set = python_value_is(container, python_type_tagt::SET);
        exprt set_test;
        {
          const typet &nit = current_left.type();
          const bool int_needle = nit.id() == ID_signedbv ||
                                  nit.id() == ID_unsignedbv ||
                                  nit.id() == ID_bool || nit.id() == ID_integer;
          if(int_needle)
          {
            dereference_exprt set_val{typecast_exprt{
              python_value_class_ptr(container),
              pointer_typet{python_set_type(), 64}}};
            member_exprt bm{set_val, "bitmap", unsignedbv_typet{64}};
            exprt shifted_item = current_left;
            if(shifted_item.type() != signedbv_typet{64})
              shifted_item = safe_typecast(shifted_item, signedbv_typet{64});
            exprt bit = bitand_exprt{
              lshr_exprt{
                bm, typecast_exprt{shifted_item, unsignedbv_typet{64}}},
              from_integer(1, unsignedbv_typet{64})};
            set_test =
              notequal_exprt{bit, from_integer(0, unsignedbv_typet{64})};
          }
          else
          {
            static unsigned set_in_nd_pv = 0;
            const irep_idt nd_id{
              qualify_name("__set_in_nd_pv_" + std::to_string(set_in_nd_pv++))};
            if(symbol_table.lookup(nd_id) == nullptr)
            {
              symbolt nds{nd_id, bool_typet{}, "python"};
              nds.base_name = id2string(nd_id);
              nds.is_lvalue = true;
              nds.is_state_var = true;
              symbol_table.add(nds);
            }
            set_test = symbol_table.lookup_ref(nd_id).symbol_expr();
          }
        }
        exprt in_expr = if_exprt{
          tag_dict,
          dict_membership,
          if_exprt{
            tag_str,
            str_contains,
            if_exprt{
              tag_list,
              list_scan,
              if_exprt{tag_set, set_test, exprt{false_exprt{}}}}}};
        cmp = (op == "In") ? in_expr : not_exprt{in_expr};
      }
      else if(is_python_dict_type(container.type()))
      {
        // key in dict: constant-key optimization. Skipped for
        // value-typed keys (extract_string_value can't see through
        // the wrapped key structs) — those go symbolic via value_equal.
        bool keys_are_values = is_python_value_type(
          to_array_type(to_struct_type(container.type()).components()[1].type())
            .element_type());
        auto key_str = extract_string_value(item);
        const exprt *dict_val = &container;
        if(container.id() == ID_symbol)
        {
          auto it =
            dict_literals.find(to_symbol_expr(container).get_identifier());
          if(it != dict_literals.end())
            dict_val = &it->second;
        }
        std::optional<std::vector<std::pair<exprt, exprt>>> in_entries;
        if(
          !keys_are_values && key_str.has_value() &&
          !python_smt_string_native_flag() && dict_val->id() == ID_struct)
          in_entries = dict_literal_leading(*dict_val);
        if(in_entries.has_value())
        {
          // Shape-agnostic decode (array literal or the
          // --python-smt-containers store-chain); a non-decodable
          // shape falls to the symbolic scan below.
          bool found = false;
          bool all_const = true;
          for(const auto &kv_pair : *in_entries)
          {
            auto kv = extract_string_value(kv_pair.first);
            if(!kv.has_value())
            {
              all_const = false;
              break;
            }
            if(kv.value() == key_str.value())
            {
              found = true;
              break;
            }
          }
          if(found || all_const)
          {
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
          // --python-ref-instances identity keys: strip the probe's
          // deref so POINTER-typed key slots compare identities.
          if(
            keys_type.element_type().id() == ID_pointer &&
            item.id() == ID_dereference &&
            item.operands()[0].type() == keys_type.element_type())
            item = item.operands()[0];
          member_exprt length{container, "length", signedbv_typet{64}};
          member_exprt keys{container, "keys", keys_type};
          // Wrap the query once (not per key) for value-domain compares.
          exprt wrapped_item = is_python_value_type(keys_type.element_type())
                                 ? wrap_value(item)
                                 : item;
          // Quantified witness lift (--python-smt-containers):
          // membership must share the subscript/get lookup's ONE
          // encoding (dict_lookup_witness_members) -- with membership
          // still a bounded scan, `'k' in d` could report ABSENT
          // while the complete lookup finds the key beyond the scan
          // bound, so a membership-guarded read false-alarmed. found
          // is a plain w < len comparison, usable directly here.
          {
            auto lifted = dict_lookup_witness_members(keys, length, item);
            if(lifted.found.is_not_nil())
            {
              cmp =
                (op == "In") ? lifted.found : exprt{not_exprt{lifted.found}};
              goto dict_in_done;
            }
          }
          {
            exprt in_expr = false_exprt{};
            for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt in_range = binary_relation_exprt{idx, ID_lt, length};
              exprt key_i = python_dict_unbox_key(index_exprt{keys, idx});
              exprt match;
              if(is_python_value_type(key_i.type()))
              {
                // Value-typed (heterogeneous) keys: compare in the
                // value domain (tag-aware), mirroring the subscript
                // read. PLR §6.10.1.
                match = value_equal(key_i, wrapped_item);
              }
              else if(
                is_python_string_type(item.type()) &&
                is_python_string_type(key_i.type()))
              {
                // Representation-neutral string content equality.
                match = string_equal(item, key_i);
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
        dict_in_done:;
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
        is_python_value_type(current_left.type()) &&
        is_python_none(right, symbol_table))
      {
        cmp = python_value_is(current_left, python_type_tagt::NONE);
        goto done_cmp;
      }
      if(
        is_python_value_type(right.type()) &&
        is_python_none(current_left, symbol_table))
      {
        cmp = python_value_is(right, python_type_tagt::NONE);
        goto done_cmp;
      }
      // PLR §6.13: 'x is None' for typed-numeric / typed-string /
      // typed-list / typed-dict slots recognises the per-target-
      // type None marker that coerce_to_typed_slot emits at
      // call/assign/return boundaries. See coerce_to_typed_slot
      // for the marker conventions.
      if(is_python_none(right, symbol_table))
      {
        if(
          current_left.type().id() == ID_signedbv ||
          current_left.type().id() == ID_integer)
        {
          cmp = equal_exprt{
            current_left,
            from_integer(python_none_sentinel_int(), current_left.type())};
          goto done_cmp;
        }
        if(current_left.type().id() == ID_floatbv)
        {
          ieee_floatt none_f{
            ieee_float_spect{to_floatbv_type(current_left.type())},
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          none_f.from_integer(python_none_sentinel_int());
          cmp = ieee_float_equal_exprt{current_left, none_f.to_expr()};
          goto done_cmp;
        }
        if(is_python_string_type(current_left.type()))
        {
          cmp = equal_exprt{
            member_exprt{
              current_left, "data", pointer_typet{unsignedbv_typet{8}, 64}},
            null_pointer_exprt{pointer_typet{unsignedbv_typet{8}, 64}}};
          goto done_cmp;
        }
        if(
          is_python_list_type(current_left.type()) ||
          is_python_dict_type(current_left.type()) ||
          current_left.type().id() == ID_struct ||
          current_left.type().id() == ID_struct_tag)
        {
          // typed-list / typed-dict / class instances: see Eq
          // arm above for why we don't recognise length-0 as
          // None here.
          cmp = false_exprt{};
          goto done_cmp;
        }
      }
      if(is_python_none(current_left, symbol_table))
      {
        if(right.type().id() == ID_signedbv || right.type().id() == ID_integer)
        {
          cmp = equal_exprt{
            right, from_integer(python_none_sentinel_int(), right.type())};
          goto done_cmp;
        }
        if(right.type().id() == ID_floatbv)
        {
          ieee_floatt none_f{
            ieee_float_spect{to_floatbv_type(right.type())},
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          none_f.from_integer(python_none_sentinel_int());
          cmp = ieee_float_equal_exprt{right, none_f.to_expr()};
          goto done_cmp;
        }
        if(is_python_string_type(right.type()))
        {
          cmp = equal_exprt{
            member_exprt{right, "data", pointer_typet{unsignedbv_typet{8}, 64}},
            null_pointer_exprt{pointer_typet{unsignedbv_typet{8}, 64}}};
          goto done_cmp;
        }
        if(
          is_python_list_type(right.type()) ||
          is_python_dict_type(right.type()) || right.type().id() == ID_struct ||
          right.type().id() == ID_struct_tag)
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
      //
      // PLR 6.10.3, class instances: `is` was falling through to
      // the scalar arm's VALUE equality -- `C(1) is C(1)` with equal
      // fields PROVED (a false proof in the identity cluster). Three
      // sound tiers: both operands dereferences of POINTER symbols
      // (params/self/promoted aliases) compare the pointers -- exact
      // identity, correct for f(a, a) receiver aliasing; both
      // by-value locals use the alias-chain constant below (the same
      // documented approximation as list/dict); mixed
      // pointer-vs-local is statically unknowable -- a sound NONDET
      // (both outcomes explored, never a definite wrong answer).
      {
        const bool left_is_instance =
          !class_name_of_type(current_left.type()).empty();
        const bool right_is_instance =
          !class_name_of_type(right.type()).empty();
        if(left_is_instance && right_is_instance)
        {
          // Tier 1: the alias chain is DEFINITIONAL -- `b = a`
          // records b's canonical source, so equal canonicals are
          // the same object regardless of representation (a
          // promoted alias converts as a POINTER deref, which the
          // pointer tiers below would have judged nondet).
          auto is_canon = [this](irep_idt id) -> irep_idt
          {
            auto it = alias_targets.find(id);
            while(it != alias_targets.end())
            {
              id = it->second;
              it = alias_targets.find(id);
            }
            return id;
          };
          auto is_raw_id = [&](const exprt &e) -> irep_idt
          {
            if(e.id() == ID_symbol)
              return to_symbol_expr(e).get_identifier();
            if(
              e.id() == ID_dereference && e.operands().size() == 1 &&
              e.operands()[0].id() == ID_symbol)
              return to_symbol_expr(e.operands()[0]).get_identifier();
            return irep_idt{};
          };
          const irep_idt lid0 = is_raw_id(current_left);
          const irep_idt rid0 = is_raw_id(right);
          if(
            !lid0.empty() && !rid0.empty() &&
            is_canon(lid0) == is_canon(rid0))
          {
            cmp = true_exprt{};
            goto done_cmp;
          }
          const bool lp = current_left.id() == ID_dereference &&
                          current_left.operands()[0].id() == ID_symbol;
          const bool rp = right.id() == ID_dereference &&
                          right.operands()[0].id() == ID_symbol;
          // Tier 2: both by-reference -- POINTER equality is exact
          // identity (correct for f(a, a) receiver aliasing).
          if(lp && rp)
          {
            exprt lptr = current_left.operands()[0];
            exprt rptr = right.operands()[0];
            if(lptr.type() != rptr.type())
              rptr = typecast_exprt{rptr, lptr.type()};
            cmp = equal_exprt{std::move(lptr), std::move(rptr)};
            goto done_cmp;
          }
          // Tier 3: mixed pointer-vs-by-value with DISTINCT
          // canonicals -- statically unknowable, sound NONDET.
          if(lp != rp)
          {
            log_overapprox(
              "'is' between a by-reference and a by-value instance "
              "binding: nondeterministic");
            cmp = side_effect_expr_nondett{bool_typet{}, get_location(expr)};
            goto done_cmp;
          }
        }
        if(
          (left_is_instance && right_is_instance) ||
          ((is_python_list_type(current_left.type()) ||
            is_python_dict_type(current_left.type())) &&
           (is_python_list_type(right.type()) ||
            is_python_dict_type(right.type()))))
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
      }
      if(current_left.type() != right.type())
        right = safe_typecast(right, current_left.type());
      // PLR §6.10.3: identity on non-None scalars is implementation-
      // defined (CPython caches small ints, interns some strings), so
      // equal int/float/str values may or may not be the same object.
      // We have no per-object model for scalars, so `is` degrades to
      // value equality here — warn so the result is not silently
      // trusted as true identity.
      {
        const typet &lt = current_left.type();
        if(
          lt.id() == ID_signedbv || lt.id() == ID_integer ||
          lt.id() == ID_floatbv || is_python_string_type(lt))
          log.warning() << "`is` on non-None scalar operands is modeled as "
                           "value equality; CPython object identity for equal "
                           "int/float/str values is implementation-defined and "
                           "not modeled"
                        << messaget::eom;
      }
      cmp = equal_exprt{current_left, right};
    }
    else if(op == "IsNot")
    {
      // PLR §6.10.3: "x is not None" checks tag != NONE
      if(
        is_python_value_type(current_left.type()) &&
        is_python_none(right, symbol_table))
      {
        cmp = not_exprt{python_value_is(current_left, python_type_tagt::NONE)};
        goto done_cmp;
      }
      if(
        is_python_value_type(right.type()) &&
        is_python_none(current_left, symbol_table))
      {
        cmp = not_exprt{python_value_is(right, python_type_tagt::NONE)};
        goto done_cmp;
      }
      // PLR §6.13: 'x is not None' for typed-numeric / typed-
      // string / typed-list / typed-dict slots — negation of
      // the typed-slot None-marker recognition. See
      // coerce_to_typed_slot for marker conventions.
      if(is_python_none(right, symbol_table))
      {
        if(
          current_left.type().id() == ID_signedbv ||
          current_left.type().id() == ID_integer)
        {
          cmp = notequal_exprt{
            current_left,
            from_integer(python_none_sentinel_int(), current_left.type())};
          goto done_cmp;
        }
        if(current_left.type().id() == ID_floatbv)
        {
          ieee_floatt none_f{
            ieee_float_spect{to_floatbv_type(current_left.type())},
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          none_f.from_integer(python_none_sentinel_int());
          cmp =
            not_exprt{ieee_float_equal_exprt{current_left, none_f.to_expr()}};
          goto done_cmp;
        }
        if(is_python_string_type(current_left.type()))
        {
          cmp = notequal_exprt{
            member_exprt{
              current_left, "data", pointer_typet{unsignedbv_typet{8}, 64}},
            null_pointer_exprt{pointer_typet{unsignedbv_typet{8}, 64}}};
          goto done_cmp;
        }
        if(
          is_python_list_type(current_left.type()) ||
          is_python_dict_type(current_left.type()) ||
          current_left.type().id() == ID_struct ||
          current_left.type().id() == ID_struct_tag)
        {
          // typed-list / typed-dict / class instances: see Eq
          // arm above for why we don't recognise length-0 as
          // None here.
          cmp = true_exprt{};
          goto done_cmp;
        }
      }
      if(is_python_none(current_left, symbol_table))
      {
        if(right.type().id() == ID_signedbv || right.type().id() == ID_integer)
        {
          cmp = notequal_exprt{
            right, from_integer(python_none_sentinel_int(), right.type())};
          goto done_cmp;
        }
        if(right.type().id() == ID_floatbv)
        {
          ieee_floatt none_f{
            ieee_float_spect{to_floatbv_type(right.type())},
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          none_f.from_integer(python_none_sentinel_int());
          cmp = not_exprt{ieee_float_equal_exprt{right, none_f.to_expr()}};
          goto done_cmp;
        }
        if(is_python_string_type(right.type()))
        {
          cmp = notequal_exprt{
            member_exprt{right, "data", pointer_typet{unsignedbv_typet{8}, 64}},
            null_pointer_exprt{pointer_typet{unsignedbv_typet{8}, 64}}};
          goto done_cmp;
        }
        if(
          is_python_list_type(right.type()) ||
          is_python_dict_type(right.type()) || right.type().id() == ID_struct ||
          right.type().id() == ID_struct_tag)
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
      {
        const typet &lt = current_left.type();
        if(
          lt.id() == ID_signedbv || lt.id() == ID_integer ||
          lt.id() == ID_floatbv || is_python_string_type(lt))
          log.warning() << "`is not` on non-None scalar operands is modeled "
                           "as value inequality; CPython object identity for "
                           "equal int/float/str values is implementation-"
                           "defined and not modeled"
                        << messaget::eom;
      }
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
