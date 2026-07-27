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

// PLR §6.7: the operand-type verdict for a binary/augmented operator, SHARED by
// convert_bin_op and binop_operand_type_error so the rule lives in ONE place.
// `ok` = compatible; `nondet` = incompatible but AMBIGUOUS in our model (return
// nondet, don't flag — e.g. set-vs-int bitwise, where a "set" may be a
// nondet-int-backed value); `error` = a PROVABLE TypeError (emit).
// Any/`python_value` operands are never `error`.
namespace
{
enum class binop_verdict
{
  ok,
  nondet,
  error
};

binop_verdict compute_binop_verdict(
  const std::string &op,
  const exprt &left,
  const exprt &right,
  bool ref_mutables)
{
  const bool l_is_list = is_python_list_type(left.type());
  const bool r_is_list = is_python_list_type(right.type());
  const bool l_is_tuple = is_python_tuple_type(left.type());
  const bool r_is_tuple = is_python_tuple_type(right.type());
  const bool l_is_dict = is_python_dict_type(left.type());
  const bool r_is_dict = is_python_dict_type(right.type());
  const bool l_is_set = is_python_set_type(left.type());
  const bool r_is_set = is_python_set_type(right.type());
  const bool l_is_str = is_python_string_type(left.type());
  const bool r_is_str = is_python_string_type(right.type());
  const bool l_is_complex =
    left.type().id() == ID_struct &&
    to_struct_type(left.type()).get_tag() == "python_complex";
  const bool r_is_complex =
    right.type().id() == ID_struct &&
    to_struct_type(right.type()).get_tag() == "python_complex";
  // PLR §3.2: a PROVABLE None operand (constant None) supports no arithmetic /
  // bitwise operator. A python_value that only MIGHT be None at runtime is not
  // flagged (is_python_none_constant is false for a non-constant).
  const bool l_is_none = is_python_none_constant(left);
  const bool r_is_none = is_python_none_constant(right);
  const bool l_is_num =
    left.type().id() == ID_signedbv || left.type().id() == ID_integer ||
    left.type().id() == ID_floatbv || left.type().id() == ID_bool;
  const bool r_is_num =
    right.type().id() == ID_signedbv || right.type().id() == ID_integer ||
    right.type().id() == ID_floatbv || right.type().id() == ID_bool;
  const bool l_is_float = left.type().id() == ID_floatbv;
  const bool r_is_float = right.type().id() == ID_floatbv;
  const bool l_is_intlike = left.type().id() == ID_signedbv ||
                            left.type().id() == ID_integer ||
                            left.type().id() == ID_bool;
  const bool r_is_intlike = right.type().id() == ID_signedbv ||
                            right.type().id() == ID_integer ||
                            right.type().id() == ID_bool;
  const bool bitwise_op = op == "BitAnd" || op == "BitOr" || op == "BitXor" ||
                          op == "LShift" || op == "RShift";
  const bool l_is_value = is_python_value_type(left.type());
  const bool r_is_value = is_python_value_type(right.type());
  bool incompatible = false;
  if(l_is_list && !r_is_list)
    if(
      !(op == "Mult" && (r_is_intlike || r_is_value)) &&
      !(op == "Add" && r_is_value && ref_mutables))
      incompatible = true;
  if(r_is_list && !l_is_list)
    if(
      !(op == "Mult" && (l_is_intlike || l_is_value)) &&
      !(op == "Add" && l_is_value && ref_mutables))
      incompatible = true;
  // PLR §6.3: a tuple mixed with a concrete non-tuple is a TypeError for + (and
  // only tuple*int repetition is valid for *). tuple+tuple concat is handled
  // before this verdict; python_value stays dynamic (may hold a tuple). This
  // also prevents the mistyped `tuple(iterable)` nondet (currently a python_int)
  // from reaching the arithmetic builder as `int + tuple` and tripping the
  // CBMC-core "add/sub with mixed types" invariant (a crash).
  if(l_is_tuple != r_is_tuple && !l_is_value && !r_is_value)
    if(!(op == "Mult" && (l_is_tuple ? r_is_intlike : l_is_intlike)))
      incompatible = true;
  if(l_is_dict != r_is_dict)
    incompatible = true;
  if(l_is_set != r_is_set && (l_is_num || r_is_num))
    incompatible = true;
  if(l_is_str && !r_is_str)
    if(!(op == "Mult" && (r_is_intlike || r_is_value)))
      incompatible = true;
  if(r_is_str && !l_is_str)
    if(!(op == "Mult" && (l_is_intlike || l_is_value)))
      incompatible = true;
  if(l_is_str && r_is_str && op == "Mult")
    incompatible = true;
  if(l_is_complex && (r_is_str || r_is_list || r_is_dict))
    incompatible = true;
  if(r_is_complex && (l_is_str || l_is_list || l_is_dict))
    incompatible = true;
  if(bitwise_op && (l_is_float || r_is_float))
    incompatible = true;
  if(l_is_none || r_is_none) // None supports no arithmetic/bitwise operator
    incompatible = true;
  if(!incompatible)
    return binop_verdict::ok;
  const bool arith = op == "Add" || op == "Sub" || op == "Mult" ||
                     op == "Div" || op == "FloorDiv" || op == "Mod" ||
                     op == "Pow";
  // A bitwise/shift operator with a CONCRETE operand that is definitely NOT a
  // set is an unambiguous TypeError: float / str / None / dict / complex.
  const bool l_bad_bitwise =
    l_is_float || l_is_str || l_is_dict || l_is_none || l_is_complex;
  const bool r_bad_bitwise =
    r_is_float || r_is_str || r_is_dict || r_is_none || r_is_complex;
  // PLR §6.10.1 / §3.2: a bitwise/shift op is valid only between two int-likes
  // or (for &|^) two sets. So an int-like operand combined with a REAL `list`
  // is always a TypeError (`1 & [1]`). A non-int set literal is modelled as a
  // python_list, so we EXCLUDE a `list` operand that is set-semantic-tagged
  // (`#python_set_semantic`, set by the set-literal builder): `set|set` (incl.
  // `frozenset(..) | {..}`) must stay nondet. A set stored in a symbol loses the
  // tag but `int & set` is *also* a TypeError, so firing on it is still sound;
  // the only excluded case is a provable set literal, which keeps `set|set`
  // from mis-firing. A concrete BITMAP set operand (`is_python_set_type`) is
  // likewise not treated as a real list.
  const bool l_real_list =
    l_is_list && !l_is_set && !left.get_bool("#python_set_semantic");
  const bool r_real_list =
    r_is_list && !r_is_set && !right.get_bool("#python_set_semantic");
  const bool bitwise_int_vs_real_list =
    bitwise_op &&
    ((l_is_intlike && r_real_list) || (r_is_intlike && l_real_list));
  const bool fire = arith || l_is_none || r_is_none ||
                    (bitwise_op && (l_bad_bitwise || r_bad_bitwise)) ||
                    bitwise_int_vs_real_list;
  return fire ? binop_verdict::error : binop_verdict::nondet;
}
} // namespace

// PLR §6.7: whether `op` on these operand types is a PROVABLE TypeError. Shares
// compute_binop_verdict with convert_bin_op; used by the augmented-assign path.
bool python_convertert::binop_operand_type_error(
  const std::string &op,
  const exprt &left,
  const exprt &right) const
{
  return compute_binop_verdict(op, left, right, ref_mutables) ==
         binop_verdict::error;
}

exprt python_convertert::convert_bin_op(const jsont &expr)
{
  exprt left = convert_expression(json_member(expr, "left"));
  // PLR §6.16 evaluation order: operands are evaluated left-to-right. If the
  // LEFT operand is a side-effecting call AND the RIGHT operand is a READ
  // (Name / Attribute / Subscript) whose value the call could mutate,
  // materialise the left into a temp NOW -- before converting the right -- so
  // the right's read and its tag obligation observe the left's effects. Without
  // this, `e.gm() + e.x` (gm() retags the union field e.x to str) checked e.x's
  // STALE int tag before the call ran (union-use-after-mutation false proof).
  // Gated on the right being a READ so a `f() + g()` (both calls -- e.g.
  // `fib(n-1) + fib(n-2)`) is NOT materialised (the right is freshly computed,
  // not a stale read; and the extra temp hurts recursive-unwinding convergence).
  {
    const std::string rty =
      json_string(json_member(json_member(expr, "right"), "_type"));
    const std::string lty =
      json_string(json_member(json_member(expr, "left"), "_type"));
    const bool right_is_read =
      rty == "Name" || rty == "Attribute" || rty == "Subscript";
    // PLR §6.16: the LEFT operand is evaluated (read) BEFORE the right. If the
    // left is a READ and the right is a side-effecting CALL that could mutate
    // the left's source, the left must be SNAPSHOTTED before the right runs --
    // otherwise the (lazy) symbol reference reads the right's post-mutation
    // value (e.g. `x + g()` where g() rebinds x: r must be the OLD x, not the
    // new one). Covers the direct-call right; an await is treated likewise.
    const bool left_is_read =
      lty == "Name" || lty == "Attribute" || lty == "Subscript";
    const bool right_is_call = rty == "Call" || rty == "Await";
    // Restrict the left-read snapshot to MODULE scope: only a module-level
    // global can be rebound by the right call (a callee cannot rebind a
    // caller's local/param), so snapshotting a function-local left here is
    // unnecessary AND breaks recursive-call unwinding convergence (e.g.
    // `n * fact(n-1)`). At module level the left name IS global -> snapshot.
    const bool left_read_global = left_is_read && current_function.empty();
    if(
      (left.id() == ID_side_effect && right_is_read) ||
      (left_read_global && right_is_call))
    {
      static unsigned binop_lhs_ctr = 0;
      const std::string nm = "__binop_lhs_" + std::to_string(binop_lhs_ctr++);
      const irep_idt tid{qualify_name(nm)};
      if(symbol_table.lookup(tid) == nullptr)
      {
        symbolt s{tid, left.type(), "python"};
        s.base_name = nm;
        s.is_lvalue = true;
        s.is_state_var = true;
        symbol_table.add(s);
      }
      const symbol_exprt lsym = symbol_table.lookup_ref(tid).symbol_expr();
      pending_checks.push_back(code_frontend_assignt{lsym, left});
      left = lsym;
    }
  }
  exprt right = convert_expression(json_member(expr, "right"));
  std::string op = json_string(json_member(json_member(expr, "op"), "_type"));

  if(left.is_nil() || right.is_nil())
    return nil_exprt{};

  // PLR §6.3.2: tuple + tuple concatenation. The arithmetic paths below do not
  // handle it -- falling through builds a plus_exprt on tuple STRUCTS, which
  // trips the CBMC-core "add/sub with mixed types" invariant (a crash) when the
  // two tuple types differ (e.g. `tuple(xs) + (3,)`). Fold precisely when both
  // operands are constant tuple struct_exprts; otherwise return a sound nondet
  // tuple (matching the already-sound behaviour of `(1,2)+(3,)`).
  if(
    op == "Add" && is_python_tuple_type(left.type()) &&
    is_python_tuple_type(right.type()))
  {
    if(left.id() == ID_struct && right.id() == ID_struct)
    {
      struct_typet::componentst comps;
      exprt::operandst vals;
      std::size_t idx = 0;
      const auto append = [&](const exprt &src)
      {
        const auto &st = to_struct_type(src.type());
        for(std::size_t i = 0; i < src.operands().size(); i++)
        {
          comps.push_back(struct_typet::componentt{
            "_" + std::to_string(idx++), st.components()[i].type()});
          vals.push_back(src.operands()[i]);
        }
      };
      append(left);
      append(right);
      struct_typet tt{comps};
      tt.set_tag("python_tuple");
      return struct_exprt{std::move(vals), tt};
    }
    return side_effect_expr_nondett{left.type(), get_location(expr)};
  }

  // PLR §6.3.2: tuple * int repetition. Unmodelled, the arithmetic path returned
  // the tuple UNCHANGED, so `t * 0` kept its elements -> len(t*0) != 0 and
  // t.index() found elements when the tuple should be empty (false proofs found
  // by the mutation-oracle). Fold precisely when the tuple is a constant struct
  // and the count is a constant integer (n copies; n<=0 -> the empty tuple);
  // otherwise a sound nondet tuple.
  const auto is_int_like = [](const typet &t) {
    return t.id() == ID_signedbv || t.id() == ID_integer || t.id() == ID_bool;
  };
  if(
    op == "Mult" &&
    ((is_python_tuple_type(left.type()) && is_int_like(right.type())) ||
     (is_python_tuple_type(right.type()) && is_int_like(left.type()))))
  {
    const exprt &tup = is_python_tuple_type(left.type()) ? left : right;
    const exprt &cnt = is_python_tuple_type(left.type()) ? right : left;
    // Resolve a Name to its tracked constant tuple literal.
    const exprt *tval = &tup;
    if(tup.id() == ID_symbol)
    {
      auto it = tuple_literals.find(to_symbol_expr(tup).get_identifier());
      if(it != tuple_literals.end() && it->second.id() == ID_struct)
        tval = &it->second;
    }
    mp_integer n;
    if(
      tval->id() == ID_struct && cnt.is_constant() &&
      !to_integer(to_constant_expr(cnt), n))
    {
      if(n < 0)
        n = 0;
      const auto &st = to_struct_type(tval->type());
      struct_typet::componentst comps;
      exprt::operandst vals;
      std::size_t idx = 0;
      for(mp_integer k = 0; k < n; ++k)
        for(std::size_t i = 0; i < tval->operands().size(); i++)
        {
          comps.push_back(struct_typet::componentt{
            "_" + std::to_string(idx++), st.components()[i].type()});
          vals.push_back(tval->operands()[i]);
        }
      struct_typet tt{comps};
      tt.set_tag("python_tuple");
      return struct_exprt{std::move(vals), tt};
    }
    return side_effect_expr_nondett{tup.type(), get_location(expr)};
  }

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

      // PLR §3.3.8: both __op__ (left) and __r op__ (right) failed. If a
      // concrete user-class operand provably cannot handle the operator (its
      // MRO defines neither the direct nor the reflected dunder) and the other
      // operand is not Any, the operation is a definite TypeError. Builtins
      // never reflect-handle a user class, so class-vs-builtin is caught too;
      // python_value (Any) operands are never flagged (no false positive).
      // Inherited dunders are respected via class_mro_defines, so this does not
      // fire when the method is inherited.
      const bool left_cant =
        concrete_class_lacks_dunder(left.type(), du->second.first.c_str()) &&
        concrete_class_lacks_dunder(left.type(), du->second.second.c_str());
      const bool right_cant =
        concrete_class_lacks_dunder(right.type(), du->second.first.c_str()) &&
        concrete_class_lacks_dunder(right.type(), du->second.second.c_str());
      if(
        (left_cant && !is_python_value_type(right.type())) ||
        (right_cant && !is_python_value_type(left.type())))
      {
        emit_conditional_exception(true_exprt{}, "TypeError");
        return side_effect_expr_nondett{python_int_type(), source_locationt{}};
      }
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
    // PLR §6.7: shared operand-type verdict (compute_binop_verdict above).
    binop_verdict v = compute_binop_verdict(op, left, right, ref_mutables);
    if(v != binop_verdict::ok)
    {
      if(v == binop_verdict::error)
        emit_conditional_exception(true_exprt{}, "TypeError");
      log_overapprox(
        "BinOp " + op + " on incompatible types — returning nondet");
      return side_effect_expr_nondett{python_int_type(), get_location(expr)};
    }
  }

  // PLR §6.7: tag obligation for a tagged-union operand reaching a
  // numeric binary operation. A python_value can hold a str / None /
  // list / dict at runtime; combining such a value with a concrete
  // numeric via +, -, /, //, %, ** raises TypeError under CPython
  // (e.g. None + 1, "x" + 1). The operand-promotion path below would
  // instead read __int_val unconditionally and silently produce a
  // number — a false negative. Emit a CONDITIONAL exception guarded
  // by the operand's RUNTIME tag: it fires exactly when the tag is
  // non-numeric, so there is no false positive when the value happens
  // to be numeric. Mult is excluded (str/list * int is repetition,
  // so a non-numeric tag is legitimate there). The other operand
  // must be a concrete numeric (not itself a union or str) so we do
  // not flag valid str/list concatenation (str-union + str).
  {
    bool arith = op == "Add" || op == "Sub" || op == "Div" ||
                 op == "FloorDiv" || op == "Mod" || op == "Pow";
    // Shift / bitwise ops accept only INT and BOOL operands -- a FLOAT, str,
    // None, etc. behind a tagged union raises TypeError (e.g. `x >> 1` where x
    // is str or float; b3/b5). This is STRICTER than `arith` (which also
    // accepts FLOAT/COMPLEX), so it needs its own int-only obligation.
    bool bitshift = op == "LShift" || op == "RShift" || op == "BitAnd" ||
                    op == "BitOr" || op == "BitXor";
    bool l_val = is_python_value_type(left.type());
    bool r_val = is_python_value_type(right.type());
    bool l_num = left.type().id() == ID_signedbv ||
                 left.type().id() == ID_integer ||
                 left.type().id() == ID_floatbv || left.type().id() == ID_bool;
    bool r_num =
      right.type().id() == ID_signedbv || right.type().id() == ID_integer ||
      right.type().id() == ID_floatbv || right.type().id() == ID_bool;
    bool l_int = left.type().id() == ID_signedbv ||
                 left.type().id() == ID_integer || left.type().id() == ID_bool;
    bool r_int = right.type().id() == ID_signedbv ||
                 right.type().id() == ID_integer ||
                 right.type().id() == ID_bool;
    auto tag_is_numeric = [](const exprt &v) -> exprt
    {
      return or_exprt{
        or_exprt{
          python_value_is(v, python_type_tagt::INT),
          python_value_is(v, python_type_tagt::FLOAT)},
        or_exprt{
          python_value_is(v, python_type_tagt::BOOL),
          python_value_is(v, python_type_tagt::COMPLEX)}};
    };
    auto tag_is_int_like = [](const exprt &v) -> exprt
    {
      return or_exprt{
        python_value_is(v, python_type_tagt::INT),
        python_value_is(v, python_type_tagt::BOOL)};
    };
    exprt non_numeric = nil_exprt{};
    if(arith && l_val && r_num)
      non_numeric = not_exprt{tag_is_numeric(left)};
    else if(arith && r_val && l_num)
      non_numeric = not_exprt{tag_is_numeric(right)};
    else if(bitshift && l_val && r_int)
      non_numeric = not_exprt{tag_is_int_like(left)};
    else if(bitshift && r_val && l_int)
      non_numeric = not_exprt{tag_is_int_like(right)};
    // BOTH operands tagged-union: for the STRICTLY-numeric ops (Sub/Div/
    // FloorDiv/Pow -- where NO non-numeric operand is ever valid, unlike Add
    // (str/list concatenation), Mod (str %-formatting), or bitwise (set ops)),
    // fire if EITHER operand is non-numeric. Closes a12 (`p.a - p.b` where both
    // union fields were retagged to str). Runtime-tag-guarded: both genuinely
    // numeric -> no fire.
    else if(
      (op == "Sub" || op == "Div" || op == "FloorDiv" || op == "Pow") &&
      l_val && r_val)
      non_numeric =
        not_exprt{and_exprt{tag_is_numeric(left), tag_is_numeric(right)}};
    if(!non_numeric.is_nil())
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
      {
        pending_checks.push_back(code_frontend_assignt{
          exc_sym->symbol_expr(),
          or_exprt{exc_sym->symbol_expr(), non_numeric}});
        if(exc_type_sym != nullptr)
        {
          long h = exception_type_hash("TypeError");
          pending_checks.push_back(code_frontend_assignt{
            exc_type_sym->symbol_expr(),
            if_exprt{
              non_numeric,
              from_integer(h, exc_type_sym->type),
              exc_type_sym->symbol_expr()}});
        }
      }
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
    // PLR §6.7: complex + str / complex * str / etc. is a
    // TypeError. Detect non-numeric RHS and emit the
    // exception flag without attempting a numeric promotion.
    if(
      is_python_string_type(right.type()) ||
      is_python_list_type(right.type()) || is_python_tuple_type(right.type()) ||
      is_python_dict_type(right.type()))
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
            exc_type_sym->symbol_expr(), from_integer(h, exc_type_sym->type)});
        }
      }
      return left; // value irrelevant; exception will dominate
    }
    struct_typet ct = to_struct_type(left.type());
    exprt real_part = safe_typecast(right, double_type());
    right = struct_exprt{{real_part, safe_zero(double_type())}, ct};
  }
  if(!is_complex(left.type()) && is_complex(right.type()))
  {
    if(
      is_python_string_type(left.type()) || is_python_list_type(left.type()) ||
      is_python_tuple_type(left.type()) || is_python_dict_type(left.type()))
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
            exc_type_sym->symbol_expr(), from_integer(h, exc_type_sym->type)});
        }
      }
      return right;
    }
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
      // PLR §6.7: division by zero on complex raises
      // ZeroDivisionError. The denominator is c²+d²; a zero
      // denominator means c == 0 AND d == 0. Set the
      // exception flag so try/except catches it.
      const symbolt *exc_sym_d =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym_d =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym_d != nullptr)
      {
        ieee_floatt fz{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        fz.make_zero();
        exprt zero = fz.to_expr();
        exprt is_zero_denom = and_exprt{
          ieee_float_equal_exprt{rr, zero}, ieee_float_equal_exprt{ri, zero}};
        pending_checks.push_back(code_frontend_assignt{
          exc_sym_d->symbol_expr(),
          or_exprt{exc_sym_d->symbol_expr(), is_zero_denom}});
        if(exc_type_sym_d != nullptr)
        {
          long h = exception_type_hash("ZeroDivisionError");
          pending_checks.push_back(code_frontend_assignt{
            exc_type_sym_d->symbol_expr(),
            if_exprt{
              is_zero_denom,
              from_integer(h, exc_type_sym_d->type),
              exc_type_sym_d->symbol_expr()}});
        }
      }
      return struct_exprt{
        {div_exprt{real_num, denom}, div_exprt{imag_num, denom}}, ct};
    }
    if(op == "Pow")
    {
      // PLR §6.5: complex-base power. We don't model the value
      // exactly (the exponent may be non-integer or non-constant
      // and cmath's exact semantics are intricate), but we *do*
      // surface ZeroDivisionError when the base is (0+0j) and
      // the exponent has a negative real part with zero imag
      // part (i.e. the exponent looks like a negative real
      // number, the form an int exponent takes after promotion
      // to complex above). Without this guard, the frontend
      // silently produces a nondet result and a downstream
      // assertion validates anything, so the bug is invisible.
      auto is_zero_const = [](const exprt &e)
      {
        // Look through trivial typecasts to find a constant.
        const exprt *p = &e;
        while(p->id() == ID_typecast && p->operands().size() == 1)
          p = &p->operands()[0];
        if(!p->is_constant())
          return false;
        if(p->type().id() == ID_floatbv)
        {
          ieee_floatt v{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          v.from_expr(to_constant_expr(*p));
          return v.is_zero();
        }
        if(
          p->type().id() == ID_signedbv || p->type().id() == ID_unsignedbv ||
          p->type().id() == ID_integer)
        {
          mp_integer v;
          if(!to_integer(to_constant_expr(*p), v))
            return v == 0;
        }
        return false;
      };
      auto is_neg_const = [](const exprt &e)
      {
        // Look through typecasts to find the underlying value;
        // unary_minus(constant_pos) and direct negative constants
        // both count as "negative".
        const exprt *p = &e;
        while(p->id() == ID_typecast && p->operands().size() == 1)
          p = &p->operands()[0];
        if(p->id() == ID_unary_minus && p->operands().size() == 1)
        {
          const exprt *inner = &p->operands()[0];
          while(inner->id() == ID_typecast && inner->operands().size() == 1)
            inner = &inner->operands()[0];
          if(inner->is_constant())
          {
            // -<positive> is negative; -0 is zero (not negative).
            if(inner->type().id() == ID_floatbv)
            {
              ieee_floatt v{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              v.from_expr(to_constant_expr(*inner));
              return !v.is_zero();
            }
            if(
              inner->type().id() == ID_signedbv ||
              inner->type().id() == ID_unsignedbv ||
              inner->type().id() == ID_integer)
            {
              mp_integer v;
              if(!to_integer(to_constant_expr(*inner), v))
                return v > 0;
            }
          }
          return false;
        }
        if(!p->is_constant())
          return false;
        if(p->type().id() == ID_floatbv)
        {
          ieee_floatt v{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          v.from_expr(to_constant_expr(*p));
          return v.get_sign() && !v.is_zero() && !v.is_NaN();
        }
        if(p->type().id() == ID_signedbv || p->type().id() == ID_integer)
        {
          mp_integer v;
          if(!to_integer(to_constant_expr(*p), v))
            return v < 0;
        }
        return false;
      };
      // Look through left/right struct_exprt to find the actual
      // real/imag constant components. The member_exprt forms (lr,
      // li, rr, ri) are NOT constants by themselves.
      bool base_is_zero = false, exp_is_neg = false;
      if(left.id() == ID_struct && left.operands().size() == 2)
        base_is_zero = is_zero_const(left.operands()[0]) &&
                       is_zero_const(left.operands()[1]);
      if(right.id() == ID_struct && right.operands().size() == 2)
        exp_is_neg = is_neg_const(right.operands()[0]) &&
                     is_zero_const(right.operands()[1]);
      if(base_is_zero && exp_is_neg)
      {
        // Direct property assertion (FALSE) — equivalent to a
        // raise ZeroDivisionError that's never caught here.
        add_check(
          false_exprt{},
          "exception",
          "ZeroDivisionError: 0.0 to a negative power",
          source_locationt{});
      }
      // PLR §6.5: complex Pow constant-fold via
      //   z**w = exp(w * log(z))
      // when all four components (left.real, left.imag,
      // right.real, right.imag) are compile-time constants.
      // 'left' / 'right' here are struct_exprts after the
      // numeric→complex promotion above; for symbol bases like
      // 'z = complex(4, 0); w = z**0.5' we additionally look
      // through the complex_literals snapshot so the symbol's
      // recorded struct value is recovered.
      auto try_components =
        [&](const exprt &e) -> std::optional<std::pair<double, double>>
      {
        const exprt *p = &e;
        if(p->id() == ID_symbol && is_complex(p->type()))
        {
          auto it = complex_literals.find(to_symbol_expr(*p).get_identifier());
          if(it != complex_literals.end())
            p = &it->second;
        }
        if(
          is_complex(p->type()) && p->id() == ID_struct &&
          p->operands().size() == 2)
        {
          auto rr = try_eval_double(p->operands()[0]);
          auto ii = try_eval_double(p->operands()[1]);
          if(rr.has_value() && ii.has_value())
            return std::make_pair(rr.value(), ii.value());
        }
        return std::nullopt;
      };
      auto bc = try_components(left);
      auto ec = try_components(right);
      // PLR §6.5: exact integer power via repeated multiplication.
      // For an integer exponent n, z**n is exact field-wise
      // arithmetic — unlike the exp(w·log z) form below, which
      // injects floating-point error and so breaks exact result
      // comparisons (e.g. (0+1j)**2 == -1+0j). This also works for
      // a *symbolic* base: only the exponent need be a constant
      // integer (covers z**0/1, bool exponents via 0/1, and
      // negative powers as 1 / z**|n|).
      if(
        ec.has_value() && ec->second == 0.0 &&
        ec->first == std::floor(ec->first) && std::fabs(ec->first) <= 64.0)
      {
        const long n = static_cast<long>(ec->first);
        if(n == 0)
          return struct_exprt{
            {double_to_floatbv(1.0), double_to_floatbv(0.0)}, ct};
        // Materialise each intermediate into a temp to keep the
        // expression size linear in |n| (a bare nested product
        // references the accumulator twice per step → 2^|n|).
        auto fresh_complex = [&](exprt val) -> exprt
        {
          static unsigned cpow_ctr = 0;
          std::string tn = "__cpow_" + std::to_string(cpow_ctr++);
          irep_idt ti{qualify_name(tn)};
          if(symbol_table.lookup(ti) == nullptr)
          {
            symbolt ts{ti, ct, "python"};
            ts.base_name = tn;
            ts.is_lvalue = true;
            ts.is_state_var = true;
            symbol_table.add(ts);
          }
          symbol_exprt s = symbol_table.lookup_ref(ti).symbol_expr();
          pending_checks.push_back(code_frontend_assignt{s, std::move(val)});
          return std::move(s);
        };
        auto cmul = [&](const exprt &x, const exprt &y) -> exprt
        {
          member_exprt xr{x, "real", double_type()};
          member_exprt xi{x, "imag", double_type()};
          member_exprt yr{y, "real", double_type()};
          member_exprt yi{y, "imag", double_type()};
          return struct_exprt{
            {minus_exprt{mult_exprt{xr, yr}, mult_exprt{xi, yi}},
             plus_exprt{mult_exprt{xr, yi}, mult_exprt{xi, yr}}},
            ct};
        };
        const long an = n < 0 ? -n : n;
        exprt base_t = fresh_complex(left);
        exprt acc = base_t;
        for(long i = 1; i < an; ++i)
          acc = fresh_complex(cmul(acc, base_t));
        if(n > 0)
          return acc;
        // Negative exponent: (1+0j) / acc = (ar - ai·j) / |acc|².
        member_exprt ar{acc, "real", double_type()};
        member_exprt ai{acc, "imag", double_type()};
        exprt denom = plus_exprt{mult_exprt{ar, ar}, mult_exprt{ai, ai}};
        // PLR §6.5: (0+0j) ** negative raises ZeroDivisionError
        // (it computes 1 / 0). Raise when the accumulated
        // magnitude is zero, mirroring the complex Div handler.
        {
          const symbolt *exc_sym =
            symbol_table.lookup("python::__exception_active");
          const symbolt *exc_type_sym =
            symbol_table.lookup("python::__exception_type");
          if(exc_sym != nullptr)
          {
            exprt is_zero_denom =
              ieee_float_equal_exprt{denom, safe_zero(double_type())};
            pending_checks.push_back(code_frontend_assignt{
              exc_sym->symbol_expr(),
              or_exprt{exc_sym->symbol_expr(), is_zero_denom}});
            if(exc_type_sym != nullptr)
            {
              long h = exception_type_hash("ZeroDivisionError");
              pending_checks.push_back(code_frontend_assignt{
                exc_type_sym->symbol_expr(),
                if_exprt{
                  is_zero_denom,
                  from_integer(h, exc_type_sym->type),
                  exc_type_sym->symbol_expr()}});
            }
          }
        }
        return struct_exprt{
          {div_exprt{ar, denom}, div_exprt{unary_minus_exprt{ai}, denom}}, ct};
      }
      if(bc.has_value() && ec.has_value())
      {
        const double a = bc->first, b = bc->second;
        const double cc = ec->first, dd = ec->second;
        double rr_v, ii_v;
        if(a == 0.0 && b == 0.0)
        {
          if(dd == 0.0 && cc > 0.0)
          {
            rr_v = 0.0;
            ii_v = 0.0;
          }
          else if(dd == 0.0 && cc == 0.0)
          {
            // (0+0j) ** 0 == (1+0j) per Python.
            rr_v = 1.0;
            ii_v = 0.0;
          }
          else
          {
            // PLR §6.5: (0+0j) ** w raises ZeroDivisionError
            // when w has negative real part (1/(0+0j) is
            // zero division) or any non-zero imaginary part.
            add_check(
              false_exprt{},
              "exception",
              "ZeroDivisionError: 0.0 to a negative or complex power",
              source_locationt{});
            return side_effect_expr_nondett{ct, source_locationt{}};
          }
        }
        else
        {
          const double mag = std::sqrt(a * a + b * b);
          const double arg_ = std::atan2(b, a);
          const double log_re = std::log(mag);
          const double log_im = arg_;
          const double prod_re = cc * log_re - dd * log_im;
          const double prod_im = cc * log_im + dd * log_re;
          const double exp_re = std::exp(prod_re);
          rr_v = exp_re * std::cos(prod_im);
          ii_v = exp_re * std::sin(prod_im);
        }
        return struct_exprt{
          {double_to_floatbv(rr_v), double_to_floatbv(ii_v)}, ct};
      }
      return side_effect_expr_nondett{ct, source_locationt{}};
    }
    // Other ops: return nondet complex
    return side_effect_expr_nondett{ct, source_locationt{}};
  }

  // PLib stdtypes: "str" type, §6.7: binary arithmetic
  // String concatenation: s1 + s2 produces a new string containing the
  // PLR §6.5: printf-style `"fmt" % arg` -- validate each conversion against
  // the corresponding arg's concrete type (%d/%f on a str -> TypeError). Gated
  // on a constant format string; %s/%r/%a accept anything; symbolic args never
  // flagged. Single-arg form only (a tuple RHS is left unchecked -- conservative).
  // Emit-only: the formatted-string result is produced by the existing path.
  if(is_python_string_type(left.type()) && op == "Mod")
  {
    if(auto fmt = extract_string_value(left))
    {
      std::vector<char> codes;
      const std::string &f = *fmt;
      for(std::size_t i = 0; i < f.size(); ++i)
      {
        if(f[i] != '%')
          continue;
        if(i + 1 < f.size() && f[i + 1] == '%')
        {
          ++i;
          continue;
        }
        std::size_t j = i + 1;
        while(j < f.size() && !std::isalpha(static_cast<unsigned char>(f[j])))
          ++j;
        if(j < f.size())
        {
          codes.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(f[j]))));
          i = j;
        }
      }
      // Single (non-tuple) arg maps to a single conversion.
      if(codes.size() == 1 && !is_python_tuple_type(right.type()))
      {
        const char *exc = format_code_violation(
          codes[0], value_format_category(right.type()), /*percent=*/true);
        if(exc != nullptr)
          emit_conditional_exception(true_exprt{}, exc);
      }
    }
  }

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
    if(use_smt_string_native)
    {
      // Native SMT-String back-end (Plan A): s + t is a native str.++; the
      // result is an SMT String carrying its own length (no truncation). Wrap
      // it with an exact length hint (len(left)+len(right)) so that an exact
      // len() relation over the result is decidable -- without the hint the
      // solver must reason across the int2bv(str.len ...) boundary, which
      // times out (`len(s + t) == len(s) + len(t)` would not discharge).
      const exprt concat = string_concat(left, right);
      return bind_string_length_hint(
        concat,
        plus_exprt{
          native_or_member_string_length(left),
          native_or_member_string_length(right)},
        get_location(expr));
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
        pending_checks,
        loop_depth > 0,
        current_function);
    }
    typet str_type = python_string_type();
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

  // PLR §6.7: string repeat. `s * n` and `n * s` produce a new
  // string consisting of `n` copies of `s`. We constant-fold the
  // common case where both operands are known at conversion time
  // (via extract_string_value + try_eval_double).
  if(
    op == "Mult" &&
    ((is_python_string_type(left.type()) &&
      (right.type().id() == ID_signedbv || right.type().id() == ID_bool)) ||
     (is_python_string_type(right.type()) &&
      (left.type().id() == ID_signedbv || left.type().id() == ID_bool))))
  {
    exprt &str_op = is_python_string_type(left.type()) ? left : right;
    exprt &num_op = is_python_string_type(left.type()) ? right : left;
    auto sv = extract_string_value(str_op);
    if(!sv.has_value() && str_op.id() == ID_symbol)
    {
      auto it = string_constants.find(to_symbol_expr(str_op).get_identifier());
      if(it != string_constants.end())
        sv = it->second;
    }
    auto nv = try_eval_double(num_op);
    if(sv.has_value() && nv.has_value())
    {
      long long n = static_cast<long long>(nv.value());
      if(n <= 0)
        return python_string_literal(std::string{});
      // Bail out (return nondet via fall-through to the rest of the
      // BinOp dispatcher) when the result would exceed
      // PYTHON_MAX_STRING_LENGTH; the runtime path then governs.
      if(
        static_cast<long long>(sv->size()) * n <=
        static_cast<long long>(PYTHON_MAX_STRING_LENGTH))
      {
        std::string result;
        result.reserve(sv->size() * static_cast<std::size_t>(n));
        for(long long i = 0; i < n; ++i)
          result += sv.value();
        return python_string_literal(result);
      }
    }
    // Native SMT-String back-end: a (possibly symbolic) string times a
    // compile-time constant n is n native concats. Symbolic n is nonlinear in
    // length and left to the fall-through nondet.
    if(str_op.type().id() == ID_string && nv.has_value())
    {
      const long long n = static_cast<long long>(nv.value());
      if(n <= 0)
        return constant_exprt{irep_idt{""}, string_typet{}};
      if(n <= static_cast<long long>(PYTHON_MAX_STRING_LENGTH))
      {
        exprt result = str_op;
        for(long long i = 1; i < n; ++i)
          result = string_concat(result, str_op);
        // Exact length hint: len(s * n) == len(s) * n, so a len() relation over
        // the repeated string is decidable (avoids int2bv-over-sum timeouts).
        return bind_string_length_hint(
          result,
          mult_exprt{
            native_or_member_string_length(str_op),
            from_integer(n, signedbv_typet{64})},
          get_location(expr));
      }
    }
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
    // PLR §6.1.4: float arithmetic with mixed operands promotes
    // ints to floats. When the value is INT-tagged, its
    // __float_val is undefined (zero in our default
    // initialisation), so we must promote __int_val → float
    // before float_plus. Pick the float field for FLOAT-tagged
    // operands and convert the int field for INT-tagged
    // operands.
    exprt left_promoted = if_exprt{
      python_value_is(left, python_type_tagt::FLOAT),
      left_float,
      typecast_exprt{left_int, left_float.type()}};
    exprt right_promoted = if_exprt{
      python_value_is(right, python_type_tagt::FLOAT),
      right_float,
      typecast_exprt{right_int, right_float.type()}};

    exprt int_result, float_result;
    if(op == "Add")
    {
      int_result = plus_exprt{left_int, right_int};
      float_result = plus_exprt{left_promoted, right_promoted};
    }
    else if(op == "Sub")
    {
      int_result = minus_exprt{left_int, right_int};
      float_result = minus_exprt{left_promoted, right_promoted};
    }
    else
    {
      int_result = mult_exprt{left_int, right_int};
      float_result = mult_exprt{left_promoted, right_promoted};
    }

    // Return tagged union with appropriate type
    exprt int_wrapped =
      make_python_value(python_type_tagt::INT, box_int_for_storage(int_result));
    exprt float_wrapped =
      make_python_value(python_type_tagt::FLOAT, float_result);
    return if_exprt{either_float, float_wrapped, int_wrapped};
  }

  // Unwrap tagged-union values to concrete types for operations.
  // Special case: 'list * value' / 'value * list' is a repeat, so
  // a tagged-union 'value' must be unwrapped to int (NOT to the
  // other operand's list type via __list_ptr — that breaks the
  // repeat semantics).
  if(is_python_value_type(left.type()))
  {
    typet target =
      right.type().id() != ID_struct && right.type().id() != ID_struct_tag
        ? right.type()
        : python_int_type();
    if(is_python_list_type(right.type()) && op == "Mult")
      target = python_int_type();
    left = unwrap_value(left, target);
  }
  if(is_python_value_type(right.type()))
  {
    typet target = left.type();
    if(is_python_list_type(left.type()) && op == "Mult")
      target = python_int_type();
    right = unwrap_value(right, target);
  }

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
    typet str_type = python_string_type();
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
  // List concatenation: [1,2] + [3,4] → [1,2,3,4].
  // Under reference semantics an operand may be a python_value LIST reference
  // (e.g. `[1] + r` where r iterates a list[python_value]); deref it to the
  // concrete list first. If the two element types differ (e.g. list[int] +
  // list[python_value]), promote both to list[python_value] so the merged
  // data array is type-uniform.
  {
    exprt cl = left;
    exprt cr = right;
    if(ref_mutables && op == "Add" && is_python_value_type(cl.type()))
      cl = python_value_list(cl);
    if(ref_mutables && op == "Add" && is_python_value_type(cr.type()))
      cr = python_value_list(cr);
    if(
      ref_mutables && is_python_list_type(cl.type()) &&
      is_python_list_type(cr.type()) && op == "Add")
    {
      if(cl.type() != cr.type())
      {
        cl = rebuild_list_as_pv(cl);
        cr = rebuild_list_as_pv(cr);
      }
      left = cl;
      right = cr;
    }
  }
  // Constant-fold list concatenation when BOTH operands resolve to constant
  // list literals (directly, or a Name via list_literals). Concatenation always
  // produces a FRESH list in Python, so folding to a new constant struct is
  // sound regardless of operand aliasing, and it keeps length/content trackable
  // (via list_literals on the assign) -- closing sorted/min/max-over-concat
  // (mixed-category KNOWNBUG), len(), and index folding through `a + b`.
  // Requirements learned from an earlier reverted spike: resolve Names to their
  // literal, and PROMOTE mismatched element types to python_value (via
  // rebuild_list_as_pv) so the merged data array is type-uniform (else a
  // list[int] + list[str] merge is malformed).
  if(op == "Add")
  {
    auto resolve_const_list = [&](const exprt &e) -> std::optional<exprt>
    {
      if(e.id() == ID_struct && is_python_list_type(e.type()))
        return e;
      if(e.id() == ID_symbol)
      {
        auto it = list_literals.find(to_symbol_expr(e).get_identifier());
        if(
          it != list_literals.end() && it->second.id() == ID_struct &&
          is_python_list_type(it->second.type()))
          return it->second;
      }
      return std::nullopt;
    };
    auto is_const_struct = [](const exprt &s)
    {
      return s.operands().size() >= 2 && s.operands()[0].is_constant() &&
             s.operands()[1].id() == ID_array;
    };
    std::optional<exprt> lco = resolve_const_list(left);
    std::optional<exprt> rco = resolve_const_list(right);
    if(lco && rco && is_const_struct(*lco) && is_const_struct(*rco))
    {
      mp_integer nl, nr;
      if(
        !to_integer(to_constant_expr(lco->operands()[0]), nl) &&
        !to_integer(to_constant_expr(rco->operands()[0]), nr) && nl >= 0 &&
        nr >= 0 && nl + nr <= (long)PYTHON_MAX_LIST_LENGTH)
      {
        exprt lc = *lco, rc = *rco;
        // Promote to a common element type when they differ.
        if(lc.type() != rc.type())
        {
          lc = rebuild_list_as_pv(lc);
          rc = rebuild_list_as_pv(rc);
        }
        if(lc.type() == rc.type() && is_const_struct(lc) && is_const_struct(rc))
        {
          const exprt &ld = lc.operands()[1];
          const exprt &rd = rc.operands()[1];
          const auto &mdt =
            to_array_type(to_struct_type(lc.type()).components()[1].type());
          if(
            ld.operands().size() == PYTHON_MAX_LIST_LENGTH &&
            rd.operands().size() == PYTHON_MAX_LIST_LENGTH)
          {
            const long nll = nl.to_long(), nrl = nr.to_long();
            exprt::operandst merged;
            merged.reserve(PYTHON_MAX_LIST_LENGTH);
            for(long k = 0; k < (long)PYTHON_MAX_LIST_LENGTH; ++k)
            {
              if(k < nll)
                merged.push_back(ld.operands()[k]);
              else if(k < nll + nrl)
                merged.push_back(rd.operands()[k - nll]);
              else
                merged.push_back(ld.operands()[k]); // unread padding
            }
            array_exprt merged_data{std::move(merged), mdt};
            return struct_exprt{
              {from_integer(nl + nr, signedbv_typet{64}), merged_data},
              lc.type()};
          }
        }
      }
    }
  }

  if(
    is_python_list_type(left.type()) && is_python_list_type(right.type()) &&
    op == "Add")
  {
    // The general concat below reads BOTH operands' data with the LEFT
    // operand's element type. When the element types DIFFER (e.g.
    // `list(zip(..))` [list of tuples] + `[5]` [list of int]) that reads the
    // right operand's elements at the wrong type -> a definite WRONG value,
    // which lets `!=` be wrongly PROVED (a false proof found by the mutation-
    // oracle). Constant different-type concat was already promoted+folded
    // above; a non-constant different-type concat cannot be soundly merged
    // (python_value has no TUPLE tag to unify tuple elements), so
    // over-approximate soundly with a nondet list.
    if(left.type() != right.type())
      return side_effect_expr_nondett{
        python_list_type(python_value_type()), get_location(expr)};
    const auto &list_st = to_struct_type(left.type());
    const auto &data_type = to_array_type(list_st.components()[1].type());
    typet elem_type = data_type.element_type();

    member_exprt left_len{left, "length", signedbv_typet{64}};
    member_exprt right_len{right, "length", signedbv_typet{64}};
    member_exprt left_data{left, "data", data_type};
    member_exprt right_data{right, "data", data_type};

    exprt new_len = plus_exprt{left_len, right_len};
    // The result holds at most PYTHON_MAX_LIST_LENGTH elements; a longer
    // concatenation is reported (python-model-bound) and cut, not silently
    // truncated (which would leave length > modelled data).
    emit_count_capacity_guard(
      pending_checks, new_len, PYTHON_MAX_LIST_LENGTH, get_location(expr));
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
    // Constant-fold `xs * n` over a CONSTANT list with a CONSTANT n -> a
    // CONSTANT struct (keeps list_literals trackable, enables chained
    // `(xs*n)[::-k]` folding; same root as the concat/slice folds).
    {
      const exprt *cl = nullptr;
      if(left.id() == ID_struct)
        cl = &left;
      else if(left.id() == ID_symbol)
      {
        auto it = list_literals.find(to_symbol_expr(left).get_identifier());
        if(it != list_literals.end() && it->second.id() == ID_struct)
          cl = &it->second;
      }
      auto nv = try_eval_double(right);
      if(
        cl != nullptr && cl->operands().size() >= 2 &&
        cl->operands()[0].is_constant() && cl->operands()[1].id() == ID_array &&
        nv.has_value() && *nv == std::floor(*nv))
      {
        mp_integer src_len;
        if(
          !to_integer(to_constant_expr(cl->operands()[0]), src_len) &&
          src_len > 0)
        {
          const long long sl = src_len.to_long();
          long long nn = (long long)*nv;
          if(nn < 0)
            nn = 0;
          const long long total = sl * nn;
          if(total >= 0 && total <= (long long)PYTHON_MAX_LIST_LENGTH)
          {
            const exprt &cdata = cl->operands()[1];
            const auto &dt =
              to_array_type(to_struct_type(cl->type()).components()[1].type());
            exprt::operandst el;
            el.reserve(PYTHON_MAX_LIST_LENGTH);
            for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
            {
              if((long long)i < total)
                el.push_back(cdata.operands()[(long long)i % sl]);
              else
                el.push_back(safe_zero(dt.element_type()));
            }
            return struct_exprt{
              {from_integer(total, signedbv_typet{64}),
               array_exprt{std::move(el), dt}},
              cl->type()};
          }
        }
      }
    }
    struct_typet list_type = to_struct_type(left.type());
    const auto &data_type = to_array_type(list_type.components()[1].type());
    member_exprt old_len{left, "length", signedbv_typet{64}};
    member_exprt old_data{left, "data", data_type};
    // PLR §3.2: unwrap python_value to int when needed (the
    // right-hand side could be a tagged union holding an int).
    // The Mult-aware unwrap path above already handles this for
    // python_value-typed operands; the cast here also handles
    // any other numeric operand (signedbv, floatbv, bool).
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
    // Report (python-model-bound) + cut a repetition whose result exceeds
    // PYTHON_MAX_LIST_LENGTH, rather than silently filling only `cap` slots
    // while the length field claims more.
    emit_count_capacity_guard(
      pending_checks, new_len, PYTHON_MAX_LIST_LENGTH, get_location(expr));
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

  // PLR §6.7: bool is a numeric subtype of int. For arithmetic
  // operations, coerce bool operands to int so `True + True == 2`
  // rather than `True | True == True`.
  if(
    left.type().id() == ID_bool && right.type().id() == ID_bool &&
    (op == "Add" || op == "Sub" || op == "Mult" || op == "FloorDiv" ||
     op == "Mod" || op == "Pow" || op == "BitAnd" || op == "BitOr" ||
     op == "BitXor" || op == "LShift" || op == "RShift"))
  {
    left = safe_typecast(left, python_int_type());
    right = safe_typecast(right, python_int_type());
  }

  if(op == "Add")
  {
    if(
      (left.type().id() == ID_struct || left.type().id() == ID_struct_tag) &&
      !is_python_string_type(left.type()) && !is_python_list_type(left.type()))
    {
      // Opaque struct + struct (e.g., datetime arithmetic). The
      // SMT2 back-end with use_datatypes can't lower this to a
      // primitive operation. Sound over-approximation: nondet
      // result of the left's type.
      log_overapprox(
        "binary '+' on opaque struct types — returning nondet result");
      return side_effect_expr_nondett{left.type(), source_locationt{}};
    }
    if(left.type().id() == ID_signedbv && right.type().id() == ID_signedbv)
      emit_int_overflow_guard(
        not_exprt{binary_overflow_exprt{left, ID_overflow_plus, right}},
        get_location(expr));
    return plus_exprt{left, right};
  }
  else if(op == "Sub")
  {
    if(
      (left.type().id() == ID_struct || left.type().id() == ID_struct_tag) &&
      !is_python_string_type(left.type()) && !is_python_list_type(left.type()))
    {
      log_overapprox(
        "binary '-' on opaque struct types — returning nondet result");
      return side_effect_expr_nondett{left.type(), source_locationt{}};
    }
    if(left.type().id() == ID_signedbv && right.type().id() == ID_signedbv)
      emit_int_overflow_guard(
        not_exprt{binary_overflow_exprt{left, ID_overflow_minus, right}},
        get_location(expr));
    return minus_exprt{left, right};
  }
  else if(op == "Mult")
  {
    if(
      (left.type().id() == ID_struct || left.type().id() == ID_struct_tag) &&
      !is_python_string_type(left.type()) && !is_python_list_type(left.type()))
    {
      log_overapprox(
        "binary '*' on opaque struct types — returning nondet result");
      return side_effect_expr_nondett{left.type(), source_locationt{}};
    }
    if(left.type().id() == ID_signedbv && right.type().id() == ID_signedbv)
      emit_int_overflow_guard(
        not_exprt{binary_overflow_exprt{left, ID_overflow_mult, right}},
        get_location(expr));
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
    // Mathematical integers (--python-unbounded-ints): SMT-LIB Int
    // division is EUCLIDEAN (remainder always >= 0), Python floor
    // division rounds toward -infinity (PLR §6.7); they differ for a
    // NEGATIVE divisor with a nonzero remainder (7 // -2: Euclidean -3,
    // Python -4) -- falling through to div_exprt was a value
    // miscomputation (a latent false-proof vector). Construct the
    // floored quotient from the floored remainder via EXACT division
    // (b divides a - r_f, and all division semantics agree on exact
    // division): r_f = (b < 0 && r_e != 0) ? r_e + b : r_e;
    // q_f = (a - r_f) / b.
    if(left.type().id() == ID_integer && right.type().id() == ID_integer)
    {
      const exprt r_e = euclidean_mod_exprt{left, right};
      const exprt zero = from_integer(0, integer_typet{});
      exprt r_f = if_exprt{
        and_exprt{
          binary_relation_exprt{right, ID_lt, zero}, notequal_exprt{r_e, zero}},
        plus_exprt{r_e, right},
        r_e};
      return div_exprt{minus_exprt{left, std::move(r_f)}, right};
    }
    // PLR §6.7: float floor division is floor(left / right) (the result is a
    // float). Without the floor this returned plain true division
    // (`7.0 // 2.0 == 3.5` instead of `3.0`) -- a correctness false proof.
    // Same float-floor encoding as the float Mod branch below: truncate toward
    // zero via an int64 round-trip, then subtract 1 when the quotient is below
    // the truncation (i.e. it was negative and non-integral).
    if(left.type().id() == ID_floatbv || right.type().id() == ID_floatbv)
    {
      exprt fl = left, fr = right;
      if(fl.type().id() != ID_floatbv)
        fl = typecast_exprt{fl, double_type()};
      if(fr.type().id() != ID_floatbv)
        fr = typecast_exprt{fr, double_type()};
      exprt quotient = div_exprt{fl, fr};
      exprt truncated = typecast_exprt{
        typecast_exprt{quotient, signedbv_typet{64}}, double_type()};
      return if_exprt{
        binary_relation_exprt{quotient, ID_lt, truncated},
        minus_exprt{truncated, double_to_floatbv(1.0)},
        truncated};
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
    // Mathematical integers (--python-unbounded-ints): Python modulo
    // takes the DIVISOR's sign (PLR §6.7); SMT-LIB's Int mod is
    // Euclidean (always >= 0). Correct for a negative divisor:
    // r_f = (b < 0 && r_e != 0) ? r_e + b : r_e. Falling through to
    // nondet made every `a % b` unconstrained under the flag (spurious
    // failures AND both-ways branching).
    if(left.type().id() == ID_integer && right.type().id() == ID_integer)
    {
      const exprt r_e = euclidean_mod_exprt{left, right};
      const exprt zero = from_integer(0, integer_typet{});
      return if_exprt{
        and_exprt{
          binary_relation_exprt{right, ID_lt, zero}, notequal_exprt{r_e, zero}},
        plus_exprt{r_e, right},
        r_e};
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
    if(
      right.is_constant() &&
      (right.type().id() == ID_signedbv || right.type().id() == ID_integer))
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
    // PLR §6.5: Complex power is handled in the
    // is_complex(left) && is_complex(right) block above. If
    // the early block didn't fold, fall through to nondet.
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
      // Integer result. Compute EXACTLY with mp_integer when the base and
      // exponent are exact integer constants -- std::pow + cast-to-long-long
      // both loses precision above 2**53 AND truncates beyond 64-bit (a silent
      // false proof, e.g. 10**19). Strip int->float promotion typecasts to
      // recover the integer constants.
      auto strip_cast = [](exprt e) -> exprt
      {
        while(e.id() == ID_typecast && e.operands().size() == 1)
          e = to_typecast_expr(e).op();
        return e;
      };
      const exprt lb = strip_cast(left), rb = strip_cast(right);
      mp_integer base_i, exp_i;
      if(
        lb.is_constant() && rb.is_constant() &&
        !to_integer(to_constant_expr(lb), base_i) &&
        !to_integer(to_constant_expr(rb), exp_i) && exp_i >= 0)
      {
        // PLR §6.5: x ** 0 == 1 for every x (including 0 ** 0); avoid calling
        // power(base, 0) (whose result for 0**0 is convention-dependent).
        if(exp_i == 0)
          return from_integer(1, left.type());
        const mp_integer res = power(base_i, exp_i);
        if(left.type().id() == ID_integer)
          return from_integer(res, integer_typet{}); // unbounded: exact
        // DEFAULT 64-bit model: report+cut if the exact result exceeds the
        // signedbv[64] range, instead of silently wrapping.
        const mp_integer hi = power(2, 63) - 1;
        const mp_integer lo = -power(2, 63);
        if(res < lo || res > hi)
        {
          emit_int_overflow_guard(false_exprt{}, get_location(expr));
          return from_integer(0, left.type()); // path cut by the assume
        }
        return from_integer(res, left.type());
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
      if(
        left.is_constant() &&
        (left.type().id() == ID_signedbv || left.type().id() == ID_integer))
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
    // Variable exponent: build if-then-else chain for b=-16..16
    // (only for scalar types — complex handled above). The
    // negative-exponent branch produces a float (1.0 / base**|n|);
    // positive branches produce the same type as the base.
    {
      typet ft = double_type();
      ieee_floatt one_f{
        ieee_float_spect::double_precision(),
        ieee_floatt::rounding_modet::ROUND_TO_EVEN};
      one_f.from_integer(1);
      // Default: x**0 == 1. Promote to float so the negative-
      // arm if-exprts unify type. The Python BMC is fine with a
      // float 1.0 fallback for large exponents (we can't exactly
      // model 2**100 anyway).
      exprt result = one_f.to_expr();
      auto float_of = [&](const exprt &e) -> exprt
      {
        if(e.type().id() == ID_floatbv)
          return e;
        if(e.is_constant() && e.type().id() == ID_signedbv)
        {
          mp_integer rv;
          if(!to_integer(to_constant_expr(e), rv))
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_integer(rv);
            return fv.to_expr();
          }
        }
        return typecast_exprt{e, ft};
      };
      for(int i = 16; i >= 1; i--)
      {
        // Positive arm: n == i -> base**i
        exprt power = left;
        for(int j = 1; j < i; j++)
          power = mult_exprt{power, left};
        result = if_exprt{
          equal_exprt{right, from_integer(i, right.type())},
          float_of(power),
          result};
      }
      for(int i = 1; i <= 16; i++)
      {
        // Negative arm: n == -i -> 1.0 / base**i
        exprt power = left;
        for(int j = 1; j < i; j++)
          power = mult_exprt{power, left};
        exprt neg_pow = div_exprt{one_f.to_expr(), float_of(power)};
        result = if_exprt{
          equal_exprt{right, from_integer(-i, right.type())}, neg_pow, result};
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
      // Unbounded ints (--python-unbounded-ints, the SOUND mode): a 64-bit
      // bitwise is exact ONLY when both operands fit signedbv[64]. The old
      // unconditional cast TRUNCATED and wrapped (a leak: `(2**70) & (2**70)`
      // proved == 0). Fold constants exactly (arbitrary precision); for
      // symbolic operands select the 64-bit result when both fit, else a sound
      // nondet (the solver has no arbitrary-precision bitwise). In-range
      // bitwise (the common flags/masks case) stays precise; out-of-range is
      // over-approximated, never wrapped.
      mp_integer lv, rv;
      if(
        left.is_constant() && right.is_constant() &&
        !to_integer(to_constant_expr(left), lv) &&
        !to_integer(to_constant_expr(right), rv))
      {
        const mp_integer res = op == "BitOr"    ? bitwise_or(lv, rv)
                               : op == "BitAnd" ? bitwise_and(lv, rv)
                                                : bitwise_xor(lv, rv);
        return from_integer(res, integer_typet{});
      }
      const exprt l64 = typecast_exprt{left, signedbv_typet{64}};
      const exprt r64 = typecast_exprt{right, signedbv_typet{64}};
      exprt bw64;
      if(op == "BitOr")
        bw64 = bitor_exprt{l64, r64};
      else if(op == "BitAnd")
        bw64 = bitand_exprt{l64, r64};
      else
        bw64 = bitxor_exprt{l64, r64};
      const exprt lo = from_integer(-power(2, 63), integer_typet{});
      const exprt hi = from_integer(power(2, 63) - 1, integer_typet{});
      auto in64 = [&](const exprt &e) -> exprt
      {
        return and_exprt{
          binary_relation_exprt{e, ID_ge, lo},
          binary_relation_exprt{e, ID_le, hi}};
      };
      static unsigned bw_nd_ctr = 0;
      const irep_idt nd_id{
        qualify_name("__bitw_nd_" + std::to_string(bw_nd_ctr++))};
      if(symbol_table.lookup(nd_id) == nullptr)
      {
        symbolt s{nd_id, integer_typet{}, "python"};
        s.base_name = id2string(nd_id);
        s.is_lvalue = true;
        s.is_state_var = true;
        symbol_table.add(s);
      }
      const exprt nd = symbol_table.lookup_ref(nd_id).symbol_expr();
      return if_exprt{
        and_exprt{in64(left), in64(right)},
        typecast_exprt{bw64, integer_typet{}},
        nd};
    }
    if(op == "BitOr")
      return bitor_exprt{left, right};
    if(op == "BitAnd")
      return bitand_exprt{left, right};
    return bitxor_exprt{left, right};
  }
  else if(op == "LShift")
  {
    // Negative shift raises ValueError (both int widths).
    auto emit_neg_shift_check = [&]()
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
    };
    if(left.type().id() == ID_integer)
    {
      // Unbounded int (--python-unbounded-ints): x << n == x * 2**n in the
      // mathematical-integer domain. The old code truncated to signedbv64 and
      // WRAPPED (so `1 << 70` false-proved == 0 even in the sound mode). For a
      // constant non-negative shift, multiply by the exact 2**n; for a
      // symbolic/negative shift, over-approximate soundly with a nondet integer
      // rather than truncate-and-wrap.
      emit_neg_shift_check();
      mp_integer n;
      if(
        right.is_constant() && !to_integer(to_constant_expr(right), n) &&
        n >= 0)
        return mult_exprt{left, from_integer(power(2, n), integer_typet{})};
      return side_effect_expr_nondett{integer_typet{}, source_locationt{}};
    }
    emit_neg_shift_check();
    {
      // Overflow guard only for a non-negative constant shift amount: a
      // negative shift is a ValueError (handled above), and `overflow_shl`
      // with a negative shift trips a power() precondition in the simplifier.
      mp_integer sh;
      if(
        left.type().id() == ID_signedbv && right.is_constant() &&
        !to_integer(to_constant_expr(right), sh) && sh >= 0)
        emit_int_overflow_guard(
          not_exprt{binary_overflow_exprt{left, ID_overflow_shl, right}},
          get_location(expr));
    }
    return shl_exprt{left, right};
  }
  else if(op == "RShift")
  {
    auto emit_neg_shift_check = [&]()
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
    };
    if(left.type().id() == ID_integer)
    {
      // Unbounded int (--python-unbounded-ints): x >> n == floor(x / 2**n) in
      // the mathematical-integer domain. The old code truncated to signedbv64
      // and wrapped (so `(2**70) >> 5` false-proved == 0). For a constant
      // non-negative shift: for x >= 0 this is exact truncating division by
      // 2**n; for x < 0 Python floors (toward -inf) while integer division
      // truncates (toward 0), so fall back to a sound nondet there. A
      // symbolic/negative shift is also a sound nondet.
      emit_neg_shift_check();
      mp_integer n;
      if(
        right.is_constant() && !to_integer(to_constant_expr(right), n) &&
        n >= 0)
      {
        const exprt d = from_integer(power(2, n), integer_typet{});
        static unsigned rsh_ctr = 0;
        const irep_idt nd_id{
          qualify_name("__rshift_nd_" + std::to_string(rsh_ctr++))};
        if(symbol_table.lookup(nd_id) == nullptr)
        {
          symbolt s{nd_id, integer_typet{}, "python"};
          s.base_name = id2string(nd_id);
          s.is_lvalue = true;
          s.is_state_var = true;
          symbol_table.add(s);
        }
        const exprt nd = symbol_table.lookup_ref(nd_id).symbol_expr();
        return if_exprt{
          binary_relation_exprt{left, ID_ge, from_integer(0, integer_typet{})},
          div_exprt{left, d},
          nd};
      }
      return side_effect_expr_nondett{integer_typet{}, source_locationt{}};
    }
    emit_neg_shift_check();
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
      // PLR §3.3.8: a unary operator on a concrete class whose MRO defines no
      // matching dunder (__neg__/__pos__/__invert__) raises TypeError ('bad
      // operand type for unary ...').
      if(concrete_class_lacks_dunder(operand.type(), du->second.c_str()))
      {
        emit_conditional_exception(true_exprt{}, "TypeError");
        return side_effect_expr_nondett{operand.type(), source_locationt{}};
      }
    }
  }

  // PLR §6.6: `not x` is truth-value negation. Convert the operand to
  // bool via the SAME path that `assert`/`if` conditions use
  // (safe_typecast -> bool), which tag-dispatches a tagged-union
  // operand correctly — do NOT unwrap to int first, which reads the
  // __int_val slot and wrongly makes a present CLASS/STR/LIST/DICT
  // instance (e.g. an Optional[instance] result such as re.match's
  // Match | None) falsy. Using safe_typecast (rather than the heavier
  // python_truthiness disjunction) keeps `not` consistent with, and
  // as cheap as, the positive truth test, avoiding a refinement
  // blow-up on regex Match results.
  if(op == "Not")
    return not_exprt{safe_typecast(operand, bool_typet{})};

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
    return not_exprt{python_truthiness(operand)};
  else if(op == "Invert")
  {
    // PLR §6.7: ~x requires an integral operand; a CONCRETE float raises
    // TypeError ("bad operand type for unary ~: 'float'"). Emit it via the
    // shared exception flag (same as the binary operand-type obligations) and
    // return nondet so the GOTO stays well-typed.
    if(operand.type().id() == ID_floatbv)
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
          pending_checks.push_back(code_frontend_assignt{
            exc_type_sym->symbol_expr(),
            from_integer(
              exception_type_hash("TypeError"), exc_type_sym->type)});
      }
      return side_effect_expr_nondett{python_int_type(), get_location(expr)};
    }
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

  // Path-sensitive list-length idiom: when the first operand of
  // a short-circuiting 'and' is `len(L) >= N` or `len(L) > N`
  // (or the reversed `N <= len(L)` / `N < len(L)` / `len(L) ==
  // const N`), every subsequent operand can assume that `L` has
  // at least the corresponding number of elements. Record those
  // lower bounds in list_min_lengths for the duration of the
  // remaining operand conversions, then restore on exit.
  std::vector<irep_idt> length_bounds_added;
  auto record_length_bound = [&](const irep_idt &id, const mp_integer &n)
  {
    auto it = list_min_lengths.find(id);
    if(it == list_min_lengths.end() || it->second < n)
    {
      list_min_lengths[id] = n;
      length_bounds_added.push_back(id);
    }
  };
  auto extract_len_lower_bound = [&](const jsont &node)
  {
    if(!is_node_type(node, "Compare"))
      return;
    const jsont &ops_n = json_member(node, "ops");
    const jsont &cmps_n = json_member(node, "comparators");
    if(!ops_n.is_array() || !cmps_n.is_array())
      return;
    if(as_array(ops_n).size() != 1 || as_array(cmps_n).size() != 1)
      return;
    std::string op_name =
      json_string(json_member(*as_array(ops_n).begin(), "_type"));
    const jsont &left = json_member(node, "left");
    const jsont &right = *as_array(cmps_n).begin();

    auto is_len_call = [&](const jsont &n) -> std::string
    {
      if(!is_node_type(n, "Call"))
        return std::string{};
      const jsont &fn = json_member(n, "func");
      if(!is_node_type(fn, "Name"))
        return std::string{};
      if(json_string(json_member(fn, "id")) != "len")
        return std::string{};
      const jsont &args = json_member(n, "args");
      if(!args.is_array() || as_array(args).size() != 1)
        return std::string{};
      const jsont &a0 = *as_array(args).begin();
      if(!is_node_type(a0, "Name"))
        return std::string{};
      return json_string(json_member(a0, "id"));
    };
    auto extract_const_int = [&](const jsont &n) -> std::optional<mp_integer>
    {
      if(!is_node_type(n, "Constant"))
        return std::nullopt;
      const jsont &v = json_member(n, "value");
      if(!v.is_number())
        return std::nullopt;
      return mp_integer{std::stoll(v.value)};
    };

    // Forms:
    //   len(L) >= N  ⇒ len(L) ≥ N
    //   len(L) >  N  ⇒ len(L) ≥ N+1
    //   len(L) == N  ⇒ len(L) ≥ N
    //   N <= len(L)  ⇒ len(L) ≥ N
    //   N <  len(L)  ⇒ len(L) ≥ N+1
    std::string ln = is_len_call(left);
    if(!ln.empty())
    {
      auto cv = extract_const_int(right);
      if(!cv)
        return;
      mp_integer bound = *cv;
      if(op_name == "Gt")
        bound += 1;
      else if(op_name != "GtE" && op_name != "Eq")
        return;
      record_length_bound(irep_idt{qualify_name(ln)}, bound);
      return;
    }
    std::string rn = is_len_call(right);
    if(!rn.empty())
    {
      auto cv = extract_const_int(left);
      if(!cv)
        return;
      mp_integer bound = *cv;
      if(op_name == "Lt")
        bound += 1;
      else if(op_name != "LtE" && op_name != "Eq")
        return;
      record_length_bound(irep_idt{qualify_name(rn)}, bound);
    }
  };
  if(op == "And")
    extract_len_lower_bound(*as_array(values).begin());

  auto it = std::next(as_array(values).begin());
  for(; it != as_array(values).end(); ++it)
  {
    // PLR §6.11: 'and' / 'or' short-circuit. The right
    // operand is evaluated *only* when the left operand
    // doesn't decide the result. Side-effecting checks
    // emitted while converting the right operand (e.g.
    // KeyError on dict subscript, IndexError on list
    // subscript) must therefore be guarded by the same
    // condition that selects the right operand at runtime.
    //
    // Capture pending_checks before/after the right-operand
    // conversion, then wrap the difference in an
    // if-then-else guarded by:
    //   for 'and': condition = python_truthiness(result)
    //   for 'or' : condition = NOT(python_truthiness(result))
    std::size_t checks_before = pending_checks.size();
    exprt next = convert_expression(*it);
    if(op == "And" || op == "Or")
    {
      exprt left_truthy = python_truthiness(result);
      guard_pending_checks(
        checks_before,
        op == "And" ? left_truthy : exprt{not_exprt{left_truthy}});
    }
    // Each successive AND-operand can also enrich the bounds
    // for the operands that follow. (e.g. `len(L) >= 3 and
    // len(M) >= 2 and L[2] == M[1]`.)
    if(op == "And" && std::next(it) != as_array(values).end())
      extract_len_lower_bound(*it);
    if(result.is_nil() || next.is_nil())
    {
      for(const auto &id : length_bounds_added)
        list_min_lengths.erase(id);
      return nil_exprt{};
    }

    // PLR §6.11: "x or y" returns x if x is truthy, else y
    // "x and y" returns x if x is falsy, else y
    // The result IS one of the operands (not coerced to bool).
    // When the operand types differ, wrap both into python_value
    // so the resulting expression has a uniform type. Downstream
    // comparisons / arithmetic unwrap via unwrap_value.
    if(op == "And")
    {
      exprt cond = python_truthiness(result);
      // If result is falsy, return result; else return next.
      if(result.type() == next.type())
        result = if_exprt{cond, next, result};
      else
      {
        result = if_exprt{cond, wrap_value(next), wrap_value(result)};
      }
    }
    else if(op == "Or")
    {
      exprt cond = python_truthiness(result);
      // If result is truthy, return result; else return next.
      if(result.type() == next.type())
        result = if_exprt{cond, result, next};
      else
      {
        result = if_exprt{cond, wrap_value(result), wrap_value(next)};
      }
    }
    else
    {
      log.warning() << "Unsupported bool operator: " << op << messaget::eom;
      for(const auto &id : length_bounds_added)
        list_min_lengths.erase(id);
      return side_effect_expr_nondett{bool_typet{}, source_locationt{}};
    }
  }

  for(const auto &id : length_bounds_added)
    list_min_lengths.erase(id);
  return result;
}
