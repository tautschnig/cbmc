/// Python to GOTO converter — IfExp (PLR §6.13),
/// Subscript (PLR §6.2.4), Tuple/List display
/// (PLR §6.2.5), Attribute (PLR §6.2.3), Dict display
/// (PLR §6.2.7) implementation.
///
/// Extracted from python_converter.cpp to reduce the main file size.
/// All logic and class-member state remains unchanged — this file is
/// a pure source-split.

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/floatbv_expr.h>
#include <util/ieee_float.h>
#include <util/json.h>
#include <util/pointer_expr.h>
#include <util/pointer_offset_size.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/symbol.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

#include <cmath>

// True if `e` is safe to substitute at a program point later than where it was
// tracked: it must be invariant. A value that embeds a MUTABLE program variable
// (a reassignable lvalue — a local, a per-execution boxed-leaf materialisation
// pointer, etc.) is unsafe, because re-evaluating it at the later read observes
// the variable's CURRENT value, not its value when the dict was built (the
// dict-literal const-fold aliasing class: `n=1; d={"k":n}; n=2; d["k"]` and the
// boxed-leaf cases). Read-only constants and string-literal globals are safe.
bool python_convertert::value_is_const_foldable(const exprt &e) const
{
  if(e.id() == ID_symbol)
  {
    const symbolt *s = symbol_table.lookup(to_symbol_expr(e).get_identifier());
    if(s == nullptr)
      return false; // unknown symbol — be conservative
    // A reassignable program variable is unsafe; a read-only (const) symbol or
    // a string-literal/static-const global is safe.
    if(s->is_lvalue && !s->type.get_bool(ID_C_constant))
      return false;
  }
  for(const auto &op : e.operands())
    if(!value_is_const_foldable(op))
      return false;
  return true;
}

// Guard pending checks appended since `from` by `guard`.
void python_convertert::guard_pending_checks(
  std::size_t from,
  const exprt &guard)
{
  if(pending_checks.size() <= from)
    return;
  code_blockt block;
  for(std::size_t i = from; i < pending_checks.size(); ++i)
    block.add(std::move(pending_checks[i]));
  pending_checks.erase(pending_checks.begin() + from, pending_checks.end());
  pending_checks.push_back(code_ifthenelset{guard, std::move(block)});
}

// Emit a guarded Python exception into pending_checks.
void python_convertert::emit_conditional_exception(
  const exprt &cond,
  const char *exc_type)
{
  const symbolt *exc_sym = symbol_table.lookup("python::__exception_active");
  if(exc_sym == nullptr)
    return;
  code_blockt body;
  body.add(code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
  const symbolt *exc_type_sym = symbol_table.lookup("python::__exception_type");
  if(exc_type_sym != nullptr)
    body.add(code_frontend_assignt{
      exc_type_sym->symbol_expr(),
      from_integer(exception_type_hash(exc_type), exc_type_sym->type)});
  pending_checks.push_back(code_ifthenelset{cond, std::move(body)});
}

// Validate a dunder's return-type contract (PLR §3.3). See the header.
bool python_convertert::dunder_return_type_violation(
  const symbolt *dunder_sym,
  const char *kind)
{
  if(dunder_sym == nullptr || dunder_sym->type.id() != ID_code)
    return false;
  const typet &ret = to_code_type(dunder_sym->type).return_type();
  // Any / unannotated-inferred-as-Any: cannot prove a violation.
  if(is_python_value_type(ret))
    return false;
  const irep_idt id = ret.id();
  bool ok = false;
  if(std::string{kind} == "int")
    ok = id == ID_signedbv || id == ID_unsignedbv || id == ID_c_bool ||
         id == ID_bool;
  else if(std::string{kind} == "str")
    ok = is_python_string_type(ret);
  if(!ok)
  {
    emit_conditional_exception(true_exprt{}, "TypeError");
    return true;
  }
  return false;
}

// Opt-in (--python-raising-ops-check) modeling of an operation that
// CAN raise `exc_type` at runtime but whose success the frontend
// cannot prove (int()/float() of a non-constant string, os.* file
// ops, re with a non-str pattern, ...). Emits a nondet-guarded
// exception so the uncaught-exception / except path is explored.
// No-op unless the flag is set — the default models the op as
// silently succeeding (precision-favoring), so the ESBMC sweep is
// unaffected by default.
void python_convertert::emit_may_raise(const char *exc_type)
{
  if(!python_raising_ops_check)
    return;
  emit_conditional_exception(
    side_effect_expr_nondett{bool_typet{}, source_locationt{}}, exc_type);
}

// Shared call-site signature validation (PLR §8.7).

// PLR §8.7: the number of positional slots a `*`-unpacked call argument fills,
// when statically known — a list/tuple *literal* with no nested spread. Returns
// std::nullopt for anything whose length can't be proven (a Name, a call
// result, a nested spread), so the caller conservatively skips the arity check
// (no false positive).
std::optional<std::size_t>
python_convertert::static_unpack_length(const jsont &starred_value) const
{
  if(
    is_node_type(starred_value, "List") || is_node_type(starred_value, "Tuple"))
  {
    const jsont &elts = json_member(starred_value, "elts");
    if(elts.is_array())
    {
      for(const auto &e : as_array(elts))
        if(is_node_type(e, "Starred"))
          return std::nullopt; // nested spread — length not statically known
      return as_array(elts).size();
    }
  }
  return std::nullopt;
}

void python_convertert::validate_call_signature(
  const irep_idt &func_key,
  const jsont &expr,
  const jsont &args,
  std::size_t implicit_self)
{
  if(!function_signature_checkable.count(func_key))
    return;
  // Too many positional arguments (no *args, no *-unpacking we
  // cannot count statically).
  if(!function_vararg_index.count(func_key))
  {
    bool starred = false;
    std::size_t n_pos = implicit_self;
    if(args.is_array())
      for(const auto &a : as_array(args))
      {
        if(is_node_type(a, "Starred"))
        {
          // Fold a statically-known *-unpack length (list/tuple literal) into
          // the positional count; bail only when the length is unknown.
          auto len = static_unpack_length(json_member(a, "value"));
          if(len.has_value())
            n_pos += *len;
          else
          {
            starred = true;
            break;
          }
        }
        else
          ++n_pos;
      }
    if(!starred)
    {
      auto mp = function_max_positional.find(func_key);
      if(mp != function_max_positional.end() && n_pos > mp->second)
      {
        emit_conditional_exception(true_exprt{}, "TypeError");
        return;
      }
    }
  }
  // Unexpected keyword argument (no **kwargs). Match supplied
  // keyword names against the callee's parameter base names.
  if(!function_has_kwargs.count(func_key))
  {
    const symbolt *fs = symbol_table.lookup(func_key);
    if(fs == nullptr || fs->type.id() != ID_code)
      return;
    const auto &params = to_code_type(fs->type).parameters();
    const jsont &kws = json_member(expr, "keywords");
    if(kws.is_array())
      for(const auto &kw : as_array(kws))
      {
        const jsont &an = json_member(kw, "arg");
        if(an.is_null())
          continue; // **spread — cannot enumerate
        std::string kn = json_string(an);
        // PLR §8.7: a positional-only parameter (declared before `/`) passed
        // by keyword is a TypeError -- it cannot bind by name, and (this block
        // only runs when the callee has no **kwargs) there is nothing to
        // absorb it. e.g. `def f(x, /, y): ...; f(x=1, y=2)`.
        auto po = function_posonly_params.find(func_key);
        if(po != function_posonly_params.end() && po->second.count(kn) > 0)
        {
          emit_conditional_exception(true_exprt{}, "TypeError");
          return;
        }
        bool matched = false;
        for(const auto &p : params)
          if(id2string(p.get_base_name()) == kn)
          {
            matched = true;
            break;
          }
        if(!matched)
        {
          emit_conditional_exception(true_exprt{}, "TypeError");
          return;
        }
      }
  }

  // PLR §8.7: missing required positional argument, and "multiple
  // values for argument" (a param bound both by position and by
  // keyword). Bail on *args-spread / **kwargs-spread (cannot count
  // statically) and on a vararg callee.
  if(!function_vararg_index.count(func_key))
  {
    auto rp = function_required_positional.find(func_key);
    const symbolt *fs = symbol_table.lookup(func_key);
    if(
      rp != function_required_positional.end() && fs != nullptr &&
      fs->type.id() == ID_code)
    {
      const auto &params = to_code_type(fs->type).parameters();
      const std::size_t max_pos = function_max_positional[func_key];
      std::size_t n_pos = implicit_self;
      bool starred = false;
      if(args.is_array())
        for(const auto &a : as_array(args))
        {
          if(is_node_type(a, "Starred"))
          {
            auto len = static_unpack_length(json_member(a, "value"));
            if(len.has_value())
              n_pos += *len;
            else
            {
              starred = true;
              break;
            }
          }
          else
            ++n_pos;
        }
      std::set<std::string> kw_names;
      bool kw_spread = false;
      const jsont &kws = json_member(expr, "keywords");
      if(kws.is_array())
        for(const auto &kw : as_array(kws))
        {
          const jsont &an = json_member(kw, "arg");
          if(an.is_null())
          {
            kw_spread = true;
            break;
          }
          kw_names.insert(json_string(an));
        }
      if(!starred && !kw_spread)
      {
        auto pname = [&](std::size_t i) -> std::string
        {
          return i < params.size() ? id2string(params[i].get_base_name())
                                   : std::string{};
        };
        // Multiple values: a param bound by position is also named by
        // a keyword.
        for(std::size_t i = 0; i < n_pos && i < max_pos; ++i)
          if(kw_names.count(pname(i)))
          {
            emit_conditional_exception(true_exprt{}, "TypeError");
            return;
          }
        // Missing required: a required positional-or-keyword param is
        // bound neither by position nor by keyword.
        for(std::size_t i = 0; i < rp->second && i < max_pos; ++i)
          if(i >= n_pos && kw_names.count(pname(i)) == 0)
          {
            emit_conditional_exception(true_exprt{}, "TypeError");
            return;
          }
        // Missing required keyword-only argument (no default, not
        // supplied by keyword).
        auto rk = function_required_kwonly.find(func_key);
        if(rk != function_required_kwonly.end())
          for(const std::string &kn : rk->second)
            if(kw_names.count(kn) == 0)
            {
              emit_conditional_exception(true_exprt{}, "TypeError");
              return;
            }
      }
    }
  }
}

// PLR §6.13: Conditional expressions
// "x if C else y — first C is evaluated; if true, x is evaluated; else y."
exprt python_convertert::convert_if_exp(const jsont &expr)
{
  exprt test = convert_expression(json_member(expr, "test"));
  if(test.is_nil())
    return nil_exprt{};
  if(test.type() != bool_typet{})
    test = safe_typecast(test, bool_typet{});

  // PLR §6.13: only the selected branch is evaluated, so a
  // may-raise sub-expression in a branch must fire its check
  // only when that branch is taken. Guard each branch's pending
  // checks by the same condition the value is selected on.
  std::size_t before_body = pending_checks.size();
  exprt body = convert_expression(json_member(expr, "body"));
  guard_pending_checks(before_body, test);
  std::size_t before_orelse = pending_checks.size();
  exprt orelse = convert_expression(json_member(expr, "orelse"));
  guard_pending_checks(before_orelse, not_exprt{test});

  if(body.is_nil() || orelse.is_nil())
    return nil_exprt{};

  // PLR §6.13: if branch types differ, the conditional's type
  // is the union of both. Wrap each branch in the tagged-union
  // (python_value) ONLY when the branches span fundamentally
  // different categories (e.g. str vs int) where downstream
  // isinstance / type checks would lie. Numeric mismatches
  // (int vs float, bool vs int) stay typecast-promoted: the
  // existing test base relies on 'priority: float = 2.5 if c
  // else 1' silently widening the int 1 to 1.0.
  if(body.type() != orelse.type())
  {
    auto category = [this](const typet &t) -> int
    {
      // Numeric category (int / float / bool) — typecast-OK.
      if(
        t.id() == ID_signedbv || t.id() == ID_floatbv || t.id() == ID_bool ||
        t.id() == ID_unsignedbv || t.id() == ID_integer)
        return 0;
      if(is_python_string_type(t))
        return 1;
      if(is_python_list_type(t))
        return 2;
      if(is_python_dict_type(t))
        return 3;
      if(is_python_value_type(t))
        return 4;
      if(is_python_tuple_type(t))
        return 5;
      if(is_python_set_type(t))
        return 6;
      // Class-instance struct (and any other named struct, e.g. complex):
      // a distinct non-numeric category so a class-vs-None (or class-vs-other)
      // conditional wraps into the tagged union and preserves None, rather
      // than safe_typecast'ing None to the class type and erasing it (PLR
      // §3.2 / §6.13 -- a false-proof vector via dead None-guard code).
      if(t.id() == ID_struct || t.id() == ID_struct_tag)
        return 7;
      return -1;
    };
    int bcat = category(body.type());
    int ocat = category(orelse.type());
    // Wrap when both sides are categorisable AND they're in
    // different categories AND at least one is non-numeric
    // (a str/list/dict/python_value). Pure numeric mismatches
    // continue to typecast.
    bool different_categories =
      bcat >= 0 && ocat >= 0 && bcat != ocat && (bcat != 0 || ocat != 0);
    if(different_categories)
    {
      body = wrap_value(body);
      orelse = wrap_value(orelse);
    }
    else
      orelse = safe_typecast(orelse, body.type());
  }

  // PLR §6.13: if safe_typecast could not unify the branches
  // (e.g. list vs int), a raw if_exprt would violate CBMC's
  // goto_symex_state invariant that both branches share a
  // single type. Return a nondet of body.type() so the symex
  // graph stays well-typed; the caller's reasoning continues
  // with an over-approximation (sound — both branches are
  // possible at runtime in Python).
  if(body.type() != orelse.type())
  {
    log_overapprox("IfExp branches have incompatible types — returning nondet");
    return side_effect_expr_nondett{body.type(), get_location(expr)};
  }

  return if_exprt{test, body, orelse};
}

// PLR §6.3.2: Subscriptions
// "The primary must evaluate to an object that supports subscription."
exprt python_convertert::convert_subscript(const jsont &expr)
{
  exprt value = convert_expression(json_member(expr, "value"));

  if(value.is_nil())
    return nil_exprt{};

  // PLR §6.10: 'TypeError: 'NoneType' object is not
  // subscriptable'. Subscripting None raises TypeError. Set
  // __exception_active=True with TypeError tag and emit a
  // nondet python_value result. Mirrors len(None), iter-None,
  // None.attr, None() shapes.
  if(is_python_none(value, symbol_table))
  {
    const symbolt *exc_sym = symbol_table.lookup("python::__exception_active");
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
    return side_effect_expr_nondett{python_value_type(), get_location(expr)};
  }

  // PLR §3.3.1: custom __getitem__ dunder on class
  // instances. 'obj[key]' dispatches to
  // obj.__getitem__(key).
  {
    std::string tag;
    if(value.type().id() == ID_struct)
      tag = id2string(to_struct_type(value.type()).get_tag());
    else if(value.type().id() == ID_struct_tag)
      tag = id2string(to_struct_tag_type(value.type()).get_identifier());
    if(tag.substr(0, 13) == "python_class_")
    {
      std::string bare = tag.substr(13);
      for(const std::string &prefix :
          {std::string{"python::"} + tag + "::__getitem__",
           std::string{"python::"} + bare + "::__getitem__"})
      {
        const symbolt *gs = symbol_table.lookup(irep_idt{prefix});
        if(gs != nullptr)
        {
          exprt slice = convert_expression(json_member(expr, "slice"));
          if(slice.is_nil())
            return nil_exprt{};
          typet return_type = python_int_type();
          if(gs->type.id() == ID_code)
            return_type = to_code_type(gs->type).return_type();
          // Coerce the key to the __getitem__ parameter type (e.g. wrap a
          // string/int into python_value for an Any-typed key); otherwise a
          // native smt_string argument mismatches the declared parameter.
          exprt key_arg = slice;
          if(gs->type.id() == ID_code)
          {
            const auto &gparams = to_code_type(gs->type).parameters();
            if(gparams.size() >= 2)
              key_arg = coerce_call_argument(
                slice, gparams[1].type(), gparams[1].get_identifier());
          }
          return side_effect_expr_function_callt{
            gs->symbol_expr(),
            {address_of_exprt{value}, key_arg},
            return_type,
            get_location(expr)};
        }
      }
      // PLR §3.3.1: subscripting an instance requires __getitem__. If no
      // ancestor in the MRO defines it, `obj[key]` raises TypeError ('object is
      // not subscriptable') -- previously this fell through to a nondet read.
      if(!class_mro_defines(bare, "__getitem__"))
      {
        emit_conditional_exception(true_exprt{}, "TypeError");
        return side_effect_expr_nondett{
          python_value_type(), get_location(expr)};
      }
    }
  }

  // Dict subscript: d["key"] → scan keys array for match
  if(is_python_dict_type(value.type()))
  {
    exprt slice = convert_expression(json_member(expr, "slice"));
    if(!slice.is_nil())
    {
      // PLR §3.1, §3.2: per-key runtime-value override. When a
      // prior subscript-assign stored a value whose type didn't
      // match the dict's declared element type, the override
      // map holds the original RHS expression. Returning it
      // here lets `isinstance(d[k], V)` reflect the actual
      // stored value instead of the declared type.
      if(
        value.id() == ID_symbol &&
        is_node_type(json_member(expr, "slice"), "Constant"))
      {
        const jsont &slice_node = json_member(expr, "slice");
        const jsont &kv = json_member(slice_node, "value");
        std::string key_repr;
        if(kv.is_string())
          key_repr = "s:" + kv.value;
        else if(kv.is_number())
          key_repr = "n:" + kv.value;
        else if(kv.is_true())
          key_repr = "b:1";
        else if(kv.is_false())
          key_repr = "b:0";
        if(!key_repr.empty())
        {
          irep_idt did = to_symbol_expr(value).get_identifier();
          auto it = dict_runtime_value_overrides.find(did);
          if(it != dict_runtime_value_overrides.end())
          {
            auto kit = it->second.find(key_repr);
            if(kit != it->second.end())
              return kit->second;
          }
        }
      }

      // Constant-key optimization: resolve at conversion time
      auto key_str = extract_string_value(slice);
      if(key_str.has_value())
      {
        const exprt *dict_val = nullptr;
        if(value.id() == ID_struct)
          dict_val = &value;
        else if(value.id() == ID_symbol)
        {
          auto it = dict_literals.find(to_symbol_expr(value).get_identifier());
          if(it != dict_literals.end())
            dict_val = &it->second;
        }
        if(
          dict_val != nullptr && dict_val->operands().size() >= 3 &&
          dict_val->operands()[0].is_constant())
        {
          mp_integer len_val;
          if(!to_integer(to_constant_expr(dict_val->operands()[0]), len_val))
          {
            const exprt &keys_arr = dict_val->operands()[1];
            const exprt &vals_arr = dict_val->operands()[2];
            for(mp_integer i = 0; i < len_val; ++i)
            {
              auto idx = i.to_ulong();
              if(idx < keys_arr.operands().size())
              {
                auto kv = extract_string_value(keys_arr.operands()[idx]);
                if(kv.has_value() && kv.value() == key_str.value())
                {
                  const exprt &cv = vals_arr.operands()[idx];
                  // Do not const-fold a value that boxes a leaf behind a
                  // per-execution materialisation pointer — re-reading it
                  // aliases across instances. Fall through to the symbolic
                  // read of the per-instance copy instead.
                  if(value_is_const_foldable(cv))
                    return cv;
                }
              }
            }
          }
        }
      }
      // Constant-int-key optimization: if the slice is a
      // constant int and the dict literal has known int keys,
      // resolve at conversion time. Avoids emitting the
      // PYTHON_MAX_DICT_SIZE-wide key-match chain (which is
      // expensive for the SAT solver, especially when
      // multiple nested dict accesses chain together).
      if(slice.is_constant())
      {
        mp_integer slice_iv;
        if(!to_integer(to_constant_expr(slice), slice_iv))
        {
          const exprt *dict_val = nullptr;
          if(value.id() == ID_struct)
            dict_val = &value;
          else if(value.id() == ID_symbol)
          {
            auto it =
              dict_literals.find(to_symbol_expr(value).get_identifier());
            if(it != dict_literals.end())
              dict_val = &it->second;
          }
          if(
            dict_val != nullptr && dict_val->operands().size() >= 3 &&
            dict_val->operands()[0].is_constant())
          {
            mp_integer len_val;
            if(!to_integer(to_constant_expr(dict_val->operands()[0]), len_val))
            {
              const exprt &keys_arr = dict_val->operands()[1];
              const exprt &vals_arr = dict_val->operands()[2];
              for(mp_integer i = 0; i < len_val; ++i)
              {
                auto idx = i.to_ulong();
                if(idx < keys_arr.operands().size())
                {
                  const exprt &k = keys_arr.operands()[idx];
                  if(k.is_constant())
                  {
                    mp_integer kv;
                    if(!to_integer(to_constant_expr(k), kv) && kv == slice_iv)
                    {
                      const exprt &cv = vals_arr.operands()[idx];
                      if(
                        !is_python_list_type(cv.type()) &&
                        !is_python_dict_type(cv.type()) &&
                        value_is_const_foldable(cv))
                        return cv;
                    }
                  }
                }
              }
            }
          }
        }
      }

      const auto &dict_st = to_struct_type(value.type());
      const auto &keys_type = to_array_type(dict_st.components()[1].type());
      const auto &vals_type = to_array_type(dict_st.components()[2].type());
      member_exprt length{value, "length", signedbv_typet{64}};
      member_exprt keys{value, "keys", keys_type};
      member_exprt vals{value, "values", vals_type};

      // PLR §6.10.1: when the key type is python_string, struct
      // equality compares the data POINTERS, which differ between
      // a literal "0" and a runtime-constructed str(0) even when
      // the contents match. Use the string solver for content
      // equality so dict comprehensions like {str(i): v for ...}
      // can be looked up via d["0"].
      bool keys_are_strings = is_python_string_type(
        python_dict_logical_key_type(keys_type.element_type()));
      // PLR §6.10.1: value-typed (heterogeneous) keys cannot be matched
      // by a declared-type-gated equality — a str key would compare via
      // struct data-pointer (always miss) and a non-str key's unwrapped
      // __int_val could spuriously equal an int query. Compare in the
      // value domain (tag-aware) against the wrapped query instead.
      bool keys_are_values = is_python_value_type(keys_type.element_type());
      // Wrap the query once (not per key): value_equal compares it
      // against each value-typed key in the value domain.
      exprt wrapped_slice = keys_are_values ? wrap_value(slice) : slice;

      // Scan: result = values[i] where keys[i] == slice
      exprt result =
        safe_zero(vals_type.element_type()); // default if not found
      exprt found = false_exprt{};
      // Matched index (lowest matching, mirroring `result`'s if-chain
      // nesting) — used to return the value as an lvalue SLOT for
      // mutable-container values (dict-value-by-reference, Option 2).
      exprt found_idx = from_integer(0, signedbv_typet{64});
      for(int i = PYTHON_MAX_DICT_SIZE - 1; i >= 0; i--)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        exprt in_range = binary_relation_exprt{idx, ID_lt, length};
        exprt key_i = python_dict_unbox_key(index_exprt{keys, idx});
        exprt match;
        if(keys_are_values)
        {
          match = value_equal(key_i, wrapped_slice);
        }
        else if(keys_are_strings && is_python_string_type(slice.type()))
        {
          if(key_i.type() != slice.type())
            key_i = safe_typecast(key_i, slice.type());
          // Representation-neutral string content equality.
          match = string_equal(key_i, slice);
        }
        else
        {
          if(key_i.type() != slice.type())
            key_i = safe_typecast(key_i, slice.type());
          match = equal_exprt{key_i, slice};
        }
        exprt cond = and_exprt{in_range, match};
        result = if_exprt{cond, index_exprt{vals, idx}, result};
        // Only track the matched index for direct (int) keys — the only
        // case that returns the lvalue slot. Building this if-chain for
        // string/value keys (whose `cond` carries a string-solver
        // predicate) adds solver cost for no benefit.
        if(!keys_are_strings && !keys_are_values)
          found_idx = if_exprt{cond, idx, found_idx};
        found = or_exprt{found, cond};
      }
      // KeyError if key not found — unless the dict has a
      // guaranteed-present key matching this slice (from the
      // 'if K not in D: D[K] = ...' idiom tracked earlier), or
      // unless the dict was constructed via defaultdict / Counter
      // (in which case missing-key reads return the factory's
      // zero value instead of raising — PLR §8.5).
      bool skip_key_check = false;
      bool is_defaultdict = false;
      std::string dd_factory;
      if(value.id() == ID_symbol)
      {
        irep_idt dict_id = to_symbol_expr(value).get_identifier();
        auto gki = dict_guaranteed_keys.find(dict_id);
        if(gki != dict_guaranteed_keys.end())
        {
          // Build structural key of the slice AST for comparison.
          const jsont &slice_ast = json_member(expr, "slice");
          std::string slice_key;
          if(is_node_type(slice_ast, "Name"))
            slice_key = "Name:" + json_string(json_member(slice_ast, "id"));
          else if(is_node_type(slice_ast, "Constant"))
          {
            const jsont &cv = json_member(slice_ast, "value");
            if(cv.is_string())
              slice_key = "Const:" + cv.value;
          }
          if(!slice_key.empty() && gki->second.count(slice_key) > 0)
            skip_key_check = true;
        }
        auto ddi = defaultdict_factories.find(dict_id);
        if(ddi != defaultdict_factories.end())
        {
          is_defaultdict = true;
          dd_factory = ddi->second;
          skip_key_check = true;
        }
      }
      if(is_defaultdict)
      {
        // The earlier scan loop sets `result` to safe_zero on
        // miss. For defaultdict(int) / Counter that's already
        // 0 (the int factory's zero); for defaultdict(str) we
        // need an empty string struct, defaultdict(list) an
        // empty list, etc. Here we patch the on-miss path:
        // result = found ? scanned_result : factory_zero.
        exprt fz;
        const typet &vt = vals_type.element_type();
        if(dd_factory == "str" && is_python_string_type(vt))
        {
          // Empty string struct: { 0, NULL }
          fz = struct_exprt(
            {from_integer(0, signedbv_typet{64}),
             from_integer(0, pointer_typet(unsignedbv_typet{8}, 64))},
            vt);
        }
        else if(dd_factory == "list" && is_python_list_type(vt))
        {
          fz = safe_zero(vt);
        }
        else
        {
          // Default: 0 / safe_zero of the value type. Works for
          // defaultdict(int), Counter, defaultdict(float).
          fz = safe_zero(vt);
        }
        // result currently equals safe_zero on miss; replace
        // with the explicit factory_zero for documentation /
        // future divergence.
        result = if_exprt{found, result, fz};
      }
      if(!skip_key_check)
      {
        // PLR §6.10.1: dict subscript on missing key raises
        // KeyError. Set the __exception_active flag and the
        // exception-type so the value propagates as a Python
        // exception that try/except can catch. Don't emit a
        // separate property assertion: the downstream
        // uncaught_exception assertion (added per-statement)
        // fires for any path that reaches the end of a
        // function body / module scope with __exception_active
        // still true.
        const symbolt *exc_sym =
          symbol_table.lookup("python::__exception_active");
        const symbolt *exc_type_sym =
          symbol_table.lookup("python::__exception_type");
        if(exc_sym != nullptr)
        {
          exprt found_bool = found;
          if(found_bool.type() != bool_typet{})
            found_bool = safe_typecast(found_bool, bool_typet{});
          exprt missing = not_exprt{found_bool};
          // exc_active := exc_active || missing
          pending_checks.push_back(code_frontend_assignt{
            exc_sym->symbol_expr(), or_exprt{exc_sym->symbol_expr(), missing}});
          if(exc_type_sym != nullptr)
          {
            long h = exception_type_hash("KeyError");
            // exc_type := missing ? KeyError : exc_type
            pending_checks.push_back(code_frontend_assignt{
              exc_type_sym->symbol_expr(),
              if_exprt{
                missing,
                from_integer(h, exc_type_sym->type),
                exc_type_sym->symbol_expr()}});
          }
        }
      }
      // Dict-value-by-reference (Option 2, PLR §6.4/§3.1): when the value
      // type is a mutable container (list/dict) and the dict is an lvalue,
      // return the lvalue SLOT `values[found_idx]` rather than the copied
      // if-chain `result`, so an in-place mutation through the read
      // (`a[k].append(...)`) propagates. Each dict owns its `values[]`
      // storage, so there is no cross-dict aliasing. Scalar-value dicts
      // (the common case) are unaffected — they keep the value if-chain.
      {
        const typet &vet = vals_type.element_type();
        const bool mutable_val =
          is_python_list_type(vet) || is_python_dict_type(vet);
        const bool dict_is_lvalue =
          value.id() == ID_symbol || value.id() == ID_dereference ||
          value.id() == ID_member || value.id() == ID_index;
        // Restrict to direct (int/non-string) keys: found_idx is then a
        // cheap, exact equality so the lvalue index resolves precisely.
        // For string/value keys found_idx would depend on a string-solver
        // predicate (mis-resolves the slot and is costly), so keep the
        // value if-chain — string-keyed dict-value mutation is a residual.
        if(
          mutable_val && dict_is_lvalue && !keys_are_strings &&
          !keys_are_values)
          return index_exprt{vals, found_idx, vet};
      }
      return result;
    }
  }

  // List/string slicing: lst[1:4] or s[::-1]
  const jsont &slice_json = json_member(expr, "slice");

  // Tuple slicing: `t[lo:hi:step]`. A tuple is a fixed struct (`_0`..`_{n-1}`);
  // for a CONSTANT slice the result is a fixed tuple of known length, so build
  // it directly (PLR §6.3.3: a tuple slice returns a tuple). Without this a
  // tuple slice fell through to the single-index path and mis-modelled the
  // result's `len`/indexing (`(1,2,3)[1:]` had the wrong length).
  if(is_node_type(slice_json, "Slice") && is_python_tuple_type(value.type()))
  {
    const auto &st = to_struct_type(value.type());
    std::vector<typet> comp_types;
    while(st.has_component("_" + std::to_string(comp_types.size())))
      comp_types.push_back(
        st.get_component("_" + std::to_string(comp_types.size())).type());
    const long n = static_cast<long>(comp_types.size());
    const jsont &lo_j = json_member(slice_json, "lower");
    const jsont &hi_j = json_member(slice_json, "upper");
    const jsont &st_j = json_member(slice_json, "step");
    auto cst = [&](const jsont &j) -> std::optional<long>
    {
      auto d = try_eval_double(convert_expression(j));
      if(d.has_value() && *d == std::floor(*d))
        return static_cast<long>(*d);
      return std::nullopt;
    };
    long step = 1;
    bool ok = true;
    if(!st_j.is_null())
    {
      auto s = cst(st_j);
      if(s.has_value())
        step = *s;
      else
        ok = false;
    }
    std::vector<long> idxs;
    if(ok && step == 1)
    {
      long lo = 0, hi = n;
      auto clamp = [&](long v) { return v < 0 ? 0L : (v > n ? n : v); };
      if(!lo_j.is_null())
      {
        auto v = cst(lo_j);
        if(v.has_value())
          lo = clamp(*v < 0 ? *v + n : *v);
        else
          ok = false;
      }
      if(!hi_j.is_null())
      {
        auto v = cst(hi_j);
        if(v.has_value())
          hi = clamp(*v < 0 ? *v + n : *v);
        else
          ok = false;
      }
      if(ok)
        for(long i = lo; i < hi; i++)
          idxs.push_back(i);
    }
    else if(ok && step == -1 && lo_j.is_null() && hi_j.is_null())
    {
      for(long i = n - 1; i >= 0; i--)
        idxs.push_back(i); // t[::-1]
    }
    else
      ok = false;
    if(ok)
    {
      exprt::operandst elems;
      std::vector<typet> types;
      for(long i : idxs)
      {
        types.push_back(comp_types[i]);
        elems.push_back(
          member_exprt{value, "_" + std::to_string(i), comp_types[i]});
      }
      return struct_exprt{std::move(elems), python_tuple_type(types)};
    }
    // symbolic bounds / unsupported step: fall through (over-approx below)
  }

  if(
    is_node_type(slice_json, "Slice") &&
    (is_python_list_type(value.type()) || is_python_string_type(value.type())))
  {
    const jsont &lower_json = json_member(slice_json, "lower");
    const jsont &upper_json = json_member(slice_json, "upper");
    const jsont &step_json = json_member(slice_json, "step");

    // Native SMT-String back-end (Plan A): value is an SMT String, not a
    // struct, so its length comes from str.len rather than a member access.
    exprt length =
      (use_smt_string_native && is_python_string_type(value.type()))
        ? emit_string_int_function(
            ID_cprover_string_length_func, value, symbol_table, pending_checks)
        : exprt(member_exprt{value, "length", signedbv_typet{64}});

    // Check for step=-1 (reverse). Use try_eval_double to fold
    // through UnaryOp(USub, Constant(1)) — the AST shape for
    // -1 in slice steps.
    bool is_reverse = false;
    if(!step_json.is_null())
    {
      exprt step = convert_expression(step_json);
      auto step_d = try_eval_double(step);
      if(step_d.has_value() && *step_d == -1.0)
        is_reverse = true;
      // PLR §6.3.3: a slice step of 0 raises ValueError.
      else if(step_d.has_value() && *step_d == 0.0)
        add_check(
          false_exprt{},
          "exception",
          "ValueError: slice step cannot be zero",
          get_location(expr));
    }

    // Constant-string optimization for slicing
    if(is_python_string_type(value.type()))
    {
      auto sv = extract_string_value(value);
      if(!sv.has_value() && value.id() == ID_symbol)
      {
        auto it = string_constants.find(to_symbol_expr(value).get_identifier());
        if(it != string_constants.end())
          sv = it->second;
      }
      // Whether the present bounds fold to compile-time constants. A constant
      // subject with a NON-constant bound (e.g. a slice index derived from an
      // intrinsic such as the regex match-position) must NOT take the
      // character-level constant path below: that path defaults a non-foldable
      // bound to 0/len (value_or), which would silently return the wrong
      // (whole) slice. Such a case falls through to the str.substr path, which
      // folds the bound at SMT time.
      const bool lo_const =
        lower_json.is_null() ||
        try_eval_double(convert_expression(lower_json)).has_value();
      const bool hi_const =
        upper_json.is_null() ||
        try_eval_double(convert_expression(upper_json)).has_value();
      if(sv.has_value() && (is_reverse || (lo_const && hi_const)))
      {
        std::string s = sv.value();
        int len = static_cast<int>(s.size());
        if(is_reverse)
        {
          std::string rev(s.rbegin(), s.rend());
          return python_string_literal(rev);
        }
        else
        {
          int lo =
            lower_json.is_null()
              ? 0
              : static_cast<int>(
                  try_eval_double(convert_expression(lower_json)).value_or(0));
          int hi =
            upper_json.is_null()
              ? len
              : static_cast<int>(try_eval_double(convert_expression(upper_json))
                                   .value_or(len));
          // PLR §6.3.3: optional step. The is_reverse branch
          // above handles step=-1; here we handle other
          // constant steps (positive only — negative-step !=
          // -1 is rare and would need a different traversal).
          int step = 1;
          if(!step_json.is_null())
          {
            auto sv_step = try_eval_double(convert_expression(step_json));
            if(sv_step.has_value())
              step = static_cast<int>(*sv_step);
          }
          if(lo < 0)
            lo += len;
          if(hi < 0)
            hi += len;
          if(lo < 0)
            lo = 0;
          if(hi > len)
            hi = len;
          if(lo >= hi || step == 0)
            return python_string_literal("");
          if(step > 0)
          {
            std::string r;
            for(int i = lo; i < hi; i += step)
              r += s[i];
            return python_string_literal(r);
          }
          // step < 0 (and not -1): walk in reverse with stride.
          std::string r;
          int start = hi - 1;
          for(int i = start; i >= lo; i += step)
            r += s[i];
          return python_string_literal(r);
        }
      }
      // Non-constant string: emit cprover_string_substring(s, lo, hi)
      // for forward slicing so byte-level constraints from
      // `assume(s == "abc")` propagate to the slice (P1.D).
      // Reverse slicing (step=-1) and arbitrary steps fall back to
      // a nondet result — covering them would need additional axioms
      // not provided by the current refined-string solver.
      if(is_reverse)
        return side_effect_expr_nondett{
          python_string_type(), source_locationt{}};
      if(!step_json.is_null())
      {
        exprt step = convert_expression(step_json);
        auto step_d = try_eval_double(step);
        if(!step_d.has_value() || *step_d != 1.0)
          return side_effect_expr_nondett{
            python_string_type(), source_locationt{}};
      }
      exprt str_length =
        (use_smt_string_native && is_python_string_type(value.type()))
          ? emit_string_int_function(
              ID_cprover_string_length_func,
              value,
              symbol_table,
              pending_checks)
          : exprt(member_exprt{value, "length", signedbv_typet{64}});
      auto normalize_bound = [&](exprt bound) -> exprt
      {
        if(bound.type() != signedbv_typet{64})
          bound = safe_typecast(bound, signedbv_typet{64});
        // wrapped = bound < 0 ? bound + length : bound
        exprt wrapped = if_exprt{
          binary_relation_exprt{
            bound, ID_lt, from_integer(0, signedbv_typet{64})},
          plus_exprt{bound, str_length},
          bound};
        return wrapped;
      };
      exprt lo_e = lower_json.is_null()
                     ? from_integer(0, signedbv_typet{64})
                     : normalize_bound(convert_expression(lower_json));
      exprt hi_e = upper_json.is_null()
                     ? exprt{str_length}
                     : normalize_bound(convert_expression(upper_json));
      if(use_smt_string_native)
      {
        // Native SMT-String back-end (Plan A): s[a:b] = str.substr(s, a,
        // b-a). The result is a native SMT String (no res_len truncation).
        return string_substr(value, lo_e, minus_exprt{hi_e, lo_e});
      }
      exprt src_struct =
        (value.id() == ID_struct && value.operands().size() == 2)
          ? value
          : exprt(struct_exprt{
              {member_exprt{value, "length", signedbv_typet{64}},
               member_exprt{
                 value, "data", pointer_typet(unsignedbv_typet{8}, 64)}},
              value.type()});
      return emit_string_function(
        ID_cprover_string_substring_func,
        {src_struct, lo_e, hi_e},
        symbol_table,
        pending_checks,
        loop_depth > 0);
    }

    const auto &st = to_struct_type(value.type());
    const auto &data_type = to_array_type(st.components()[1].type());
    typet elem_type = data_type.element_type();
    member_exprt src_data{value, "data", data_type};

    exprt lower, upper;
    if(is_reverse)
    {
      lower = from_integer(0, signedbv_typet{64});
      upper = length;
    }
    else
    {
      lower = lower_json.is_null() ? from_integer(0, signedbv_typet{64})
                                   : convert_expression(lower_json);
      upper = upper_json.is_null() ? length : convert_expression(upper_json);
      // PLR §6.3.3: negative bounds wrap relative to length;
      // bounds beyond length clamp to length; bounds below 0
      // (after wrap) clamp to 0. Normalize both bounds via
      // if_exprt so the runtime form picks the right slot.
      auto normalize = [&](exprt bound) -> exprt
      {
        if(bound.type() != signedbv_typet{64})
          bound = safe_typecast(bound, signedbv_typet{64});
        // wrapped = bound < 0 ? bound + length : bound
        exprt wrapped = if_exprt{
          binary_relation_exprt{
            bound, ID_lt, from_integer(0, signedbv_typet{64})},
          plus_exprt{bound, length},
          bound};
        // clamped_low = max(0, wrapped)
        exprt clamped_low = if_exprt{
          binary_relation_exprt{
            wrapped, ID_lt, from_integer(0, signedbv_typet{64})},
          from_integer(0, signedbv_typet{64}),
          wrapped};
        // clamped_high = min(length, clamped_low)
        return if_exprt{
          binary_relation_exprt{clamped_low, ID_gt, length},
          length,
          clamped_low};
      };
      lower = normalize(std::move(lower));
      upper = normalize(std::move(upper));
    }

    exprt new_length =
      is_reverse ? exprt{length} : exprt{minus_exprt{upper, lower}};

    // Determine result type (same as source: list or string)
    std::size_t max_len = is_python_string_type(value.type())
                            ? PYTHON_MAX_STRING_LENGTH
                            : PYTHON_MAX_LIST_LENGTH;
    array_typet result_data_type{
      elem_type, from_integer(max_len, signedbv_typet{64})};

    exprt::operandst result_elems;
    for(std::size_t i = 0; i < max_len; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      exprt src_elem;
      if(is_reverse)
      {
        // result[i] = src[length - 1 - i]
        src_elem = index_exprt{
          src_data,
          minus_exprt{
            minus_exprt{length, from_integer(1, signedbv_typet{64})}, idx}};
      }
      else
      {
        src_elem = index_exprt{src_data, plus_exprt{lower, idx}};
      }
      // PLR §6.10.1: positions beyond new_length must be zero so
      // struct-equality with a list literal (whose trailing slots
      // are zeros) works. Without this, `[1,2,3,4,5][1:4] == [2,3,4]`
      // fails because the slice reads xs[4]=5 into result.data[3]
      // while the literal has 0 there.
      exprt in_slice = binary_relation_exprt{idx, ID_lt, new_length};
      result_elems.push_back(
        if_exprt{in_slice, src_elem, safe_zero(elem_type)});
    }

    array_exprt result_data{std::move(result_elems), result_data_type};
    return struct_exprt{{new_length, result_data}, st};
  }

  exprt slice = convert_expression(json_member(expr, "slice"));

  if(value.is_nil() || slice.is_nil())
    return nil_exprt{};

  // String indexing: s[i] → s.data[i] as a single-char string struct
  if(is_python_string_type(value.type()))
  {
    // slice should be an integer index. If it isn't, the user
    // wrote something like `s["x"]` (a runtime TypeError in
    // Python) or our type tracking lost precision elsewhere.
    // Either way we should not produce a malformed if_exprt
    // here: nondet a sound python_string and emit a TypeError
    // property if the slice's static type is concretely not
    // an integer.
    bool slice_is_int =
      slice.type().id() == ID_signedbv || slice.type().id() == ID_unsignedbv ||
      slice.type().id() == ID_integer || slice.type().id() == ID_bool;
    if(!slice_is_int)
    {
      log_overapprox(
        "string subscript with non-integer index — emitting type-error "
        "property and returning nondet python_string");
      source_locationt tloc = get_location(expr);
      tloc.set_property_class("type-error");
      tloc.set_comment("string indices must be integers");
      code_assertt te{false_exprt{}};
      te.add_source_location() = tloc;
      code_blockt te_block;
      te_block.add(std::move(te));
      pending_checks.push_back(std::move(te_block));
      return side_effect_expr_nondett{python_string_type(), get_location(expr)};
    }
    if(use_smt_string_native)
    {
      // Native SMT-String back-end (Plan A): s[i] = str.substr(s, adj, 1),
      // where adj handles a negative index via str.len. Computed before any
      // struct member access (value is an SMT String, not a struct).
      exprt len_e = emit_string_int_function(
        ID_cprover_string_length_func, value, symbol_table, pending_checks);
      exprt idx64 = slice;
      if(idx64.type() != signedbv_typet{64})
        idx64 = safe_typecast(idx64, signedbv_typet{64});
      exprt adj = if_exprt{
        binary_relation_exprt{
          idx64, ID_lt, from_integer(0, signedbv_typet{64})},
        plus_exprt{len_e, idx64},
        idx64};
      return string_substr(value, adj, from_integer(1, signedbv_typet{64}));
    }
    member_exprt length{value, "length", python_int_type()};
    // PLR §6.3.3: negative indices count from the end
    exprt adjusted_idx = if_exprt{
      binary_relation_exprt{slice, ID_lt, from_integer(0, slice.type())},
      plus_exprt{length, slice},
      slice};
    // Path-sensitive elision: if we're inside an `if s:` body
    // (string_min_lengths records `s` has at least 1 char) and
    // the index is a non-negative constant smaller than that
    // bound, the IndexError check is trivially safe.
    bool elide_str_idx_check = false;
    if(
      slice.is_constant() && value.id() == ID_symbol &&
      string_min_lengths.count(to_symbol_expr(value).get_identifier()) > 0)
    {
      mp_integer idx_val;
      if(!to_integer(to_constant_expr(slice), idx_val) && idx_val >= 0)
      {
        const mp_integer &min_len =
          string_min_lengths.at(to_symbol_expr(value).get_identifier());
        if(idx_val < min_len)
          elide_str_idx_check = true;
      }
    }
    if(!elide_str_idx_check)
    {
      // PLR §6.10.1: string subscript out-of-range raises
      // IndexError. Set the __exception_active flag and the
      // exception type so try/except IndexError catches the
      // path; the downstream uncaught_exception assertion
      // catches the uncaught case.
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
      {
        exprt in_range = and_exprt{
          binary_relation_exprt{adjusted_idx, ID_ge, safe_zero(slice.type())},
          binary_relation_exprt{adjusted_idx, ID_lt, length}};
        exprt out_of_range = not_exprt{in_range};
        // exc_active := exc_active || out_of_range
        pending_checks.push_back(code_frontend_assignt{
          exc_sym->symbol_expr(),
          or_exprt{exc_sym->symbol_expr(), out_of_range}});
        if(exc_type_sym != nullptr)
        {
          long h = exception_type_hash("IndexError");
          pending_checks.push_back(code_frontend_assignt{
            exc_type_sym->symbol_expr(),
            if_exprt{
              out_of_range,
              from_integer(h, exc_type_sym->type),
              exc_type_sym->symbol_expr()}});
        }
      }
    }
    // Constant-string optimization for indexing
    {
      auto sv = extract_string_value(value);
      if(!sv.has_value() && value.id() == ID_symbol)
      {
        auto it = string_constants.find(to_symbol_expr(value).get_identifier());
        if(it != string_constants.end())
          sv = it->second;
      }
      if(sv.has_value())
      {
        auto nv = try_eval_double(adjusted_idx);
        if(nv.has_value())
        {
          int i = static_cast<int>(nv.value());
          // PLR §6.10.1: string indexing is by code point.
          // Walk the UTF-8 bytes counting code points; once
          // we hit code point i, return all its bytes.
          const std::string &s = sv.value();
          int cp_idx = 0;
          std::size_t byte_start = 0;
          while(byte_start < s.size())
          {
            unsigned char first = static_cast<unsigned char>(s[byte_start]);
            if(first < 0x80 || first >= 0xC0)
            {
              // Leading byte for code point cp_idx.
              if(cp_idx == i)
              {
                // Determine how many continuation bytes
                // follow (1-3 for 2-, 3-, 4-byte sequences).
                std::size_t cp_len = 1;
                while(byte_start + cp_len < s.size())
                {
                  unsigned char nb =
                    static_cast<unsigned char>(s[byte_start + cp_len]);
                  if(nb >= 0x80 && nb < 0xC0)
                    ++cp_len;
                  else
                    break;
                }
                return python_string_literal(s.substr(byte_start, cp_len));
              }
              ++cp_idx;
            }
            ++byte_start;
          }
        }
      }
    }
    // Non-constant indexing.
    //
    // We have two strategies:
    //
    // (a) Direct byte read via pointer arithmetic — read
    //     `*(value.data + i)` into a fresh local 1-byte array.
    //     Exact when value's content is a known byte array
    //     (constant string literal or string_constants-tracked
    //     symbol), since the .data pointer points to actual
    //     bytes the back-end can read.
    //
    // (b) Refined-string substring intrinsic — emit
    //     `cprover_string_substring(value, i, i+1)`. The solver
    //     registers the result as a substring of `value` via
    //     the universal axiom
    //       forall k < |res|. res[k] == value[start+k]
    //     so byte-level constraints from `assume(value == "abc")`
    //     propagate to value[i]. Required when value's content
    //     is symbolic (no known byte array) but the solver has
    //     constraints linking it to a known string.
    //
    // The two strategies serve different patterns:
    // - (a) handles `s = "Livros"; for i in range(len(s)): s[i].isalpha()`.
    //   Byte-level reads of the result struct hit memory that
    //   the back-end actually has bytes for.
    // - (b) handles `s = nondet_str(3); assume(s == "abc"); s[0] == "a"`.
    //   The result needs to satisfy solver-level constraints
    //   from the equality assumption.
    //
    // We pick (a) when value has a known byte array reachable
    // (constant struct content, or tracked via string_constants),
    // (b) otherwise.
    bool value_has_known_bytes = false;
    {
      auto sv = extract_string_value(value);
      if(!sv.has_value() && value.id() == ID_symbol)
      {
        auto it = string_constants.find(to_symbol_expr(value).get_identifier());
        if(it != string_constants.end())
          sv = it->second;
      }
      if(sv.has_value())
        value_has_known_bytes = true;
    }
    if(value_has_known_bytes)
    {
      // Strategy (a): fresh-local 1-byte array backing.
      // We can't return a view {1, value.data + i} directly
      // because callers may return s[i] across stack frames
      // (e.g. `def last(a): return a[-1]`), where the source is
      // local to the callee — pointing into it would dangle.
      // The fresh-local backing means the returned char's bytes
      // are captured at the subscript point.
      member_exprt data_ptr(
        value, "data", pointer_typet(unsignedbv_typet{8}, 64));
      dereference_exprt ch(plus_exprt(data_ptr, adjusted_idx));
      exprt::operandst chars;
      chars.push_back(ch);
      array_typet at(unsignedbv_typet{8}, from_integer(1, signedbv_typet{64}));
      array_exprt arr(std::move(chars), at);
      exprt ptr = address_of_exprt(index_exprt(
        arr, from_integer(0, signedbv_typet{64}), unsignedbv_typet{8}));
      exprt len = from_integer(1, signedbv_typet{64});
      return struct_exprt({len, ptr}, python_string_type());
    }
    {
      // Strategy (b): substring intrinsic.
      exprt src_struct =
        (value.id() == ID_struct && value.operands().size() == 2)
          ? value
          : exprt(struct_exprt{
              {member_exprt{value, "length", signedbv_typet{64}},
               member_exprt{
                 value, "data", pointer_typet(unsignedbv_typet{8}, 64)}},
              value.type()});
      exprt start64 = adjusted_idx;
      if(start64.type() != signedbv_typet{64})
        start64 = safe_typecast(start64, signedbv_typet{64});
      exprt end64 = plus_exprt{start64, from_integer(1, signedbv_typet{64})};
      return emit_string_function(
        ID_cprover_string_substring_func,
        {src_struct, start64, end64},
        symbol_table,
        pending_checks,
        loop_depth > 0);
    }
    typet str_type = python_string_type();
    const auto &data_type = array_typet(
      unsignedbv_typet{8},
      from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));

    member_exprt data{value, "data", data_type};
    index_exprt char_val{data, adjusted_idx};

    // Build a single-character string struct
    exprt::operandst chars;
    chars.push_back(char_val);
    while(chars.size() < PYTHON_MAX_STRING_LENGTH)
      chars.push_back(from_integer(0, unsignedbv_typet{8}));

    array_exprt data_expr{std::move(chars), data_type};
    exprt length_expr = from_integer(1, python_int_type());

    struct_exprt result{{length_expr, data_expr}, str_type};
    return std::move(result);
  }

  // Array/list indexing
  if(is_python_list_type(value.type()))
  {
    // PLR §3.1: a python_value (tagged-union) index might be an
    // INT-tagged number that wraps a concrete int. Unwrap to
    // python_int so the subscript reads correctly. Tag-mismatch
    // would technically be a runtime error, but we
    // overapproximate by extracting __int_val and letting the
    // bounds check catch out-of-range values.
    if(is_python_value_type(slice.type()))
      slice = python_value_int(slice);
    // PLR §6.3.2: Non-integer index → TypeError
    if(
      slice.type().id() != ID_signedbv && slice.type().id() != ID_unsignedbv &&
      slice.type().id() != ID_integer && slice.type().id() != ID_bool)
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
    member_exprt length{value, "length", python_int_type()};

    // PLR §6.3.2: Subscriptions — negative indices count from the end.
    // Handle negative indices: lst[-1] → lst[len-1]
    exprt effective_idx = slice;
    if(slice.is_constant())
    {
      mp_integer idx_val;
      if(!to_integer(to_constant_expr(slice), idx_val) && idx_val < 0)
        effective_idx = plus_exprt{length, slice};
    }
    else
    {
      // Runtime: if idx < 0 then idx + length else idx
      effective_idx = if_exprt{
        binary_relation_exprt{slice, ID_lt, safe_zero(slice.type())},
        plus_exprt{length, slice},
        slice};
    }

    // Path-sensitive list-length idiom: when we're inside a
    // short-circuiting 'and' whose first operand asserts
    // len(L) >= N (or one of the equivalent forms recognised
    // in convert_bool_op), and the subscript index is a
    // non-negative constant < N, the IndexError check is
    // trivially safe and we elide it. Without this, the check
    // would fire unconditionally at conversion time, before
    // the and-test's path constraint reaches the solver.
    bool elide_idx_check = false;
    if(
      slice.is_constant() && value.id() == ID_symbol &&
      list_min_lengths.count(to_symbol_expr(value).get_identifier()) > 0)
    {
      mp_integer idx_val;
      if(!to_integer(to_constant_expr(slice), idx_val) && idx_val >= 0)
      {
        const mp_integer &min_len =
          list_min_lengths.at(to_symbol_expr(value).get_identifier());
        if(idx_val < min_len)
          elide_idx_check = true;
      }
    }

    if(!elide_idx_check)
    {
      // PLR §6.10.1: list subscript out-of-range raises
      // IndexError. Set the __exception_active flag and
      // exception type so try/except IndexError catches it.
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
      {
        exprt in_range = and_exprt{
          binary_relation_exprt{
            effective_idx, ID_ge, safe_zero(effective_idx.type())},
          binary_relation_exprt{effective_idx, ID_lt, length}};
        exprt out_of_range = not_exprt{in_range};
        pending_checks.push_back(code_frontend_assignt{
          exc_sym->symbol_expr(),
          or_exprt{exc_sym->symbol_expr(), out_of_range}});
        if(exc_type_sym != nullptr)
        {
          long h = exception_type_hash("IndexError");
          pending_checks.push_back(code_frontend_assignt{
            exc_type_sym->symbol_expr(),
            if_exprt{
              out_of_range,
              from_integer(h, exc_type_sym->type),
              exc_type_sym->symbol_expr()}});
        }
      }
    }

    const auto &st = to_struct_type(value.type());
    const auto &data_type = to_array_type(st.components()[1].type());
    member_exprt data{value, "data", data_type};
    // Access-level capacity catch-all: the (normalized) index must lie within
    // the modelled data array, whatever path produced this list. Without it,
    // an over-capacity list (e.g. from `+=`/`*=`/`list(iterable)`) reads
    // unmodelled nondet data silently. data_type.size() is the *actual* array
    // size, so a grown literal array is allowed.
    if(data_type.size().is_constant())
    {
      mp_integer cap;
      if(!to_integer(to_constant_expr(data_type.size()), cap))
        emit_index_capacity_guard(
          pending_checks,
          effective_idx,
          length,
          cap.to_long(),
          get_location(expr));
    }
    return index_exprt{data, effective_idx};
  }

  // Tuple indexing with constant index
  if(is_python_tuple_type(value.type()))
  {
    // Try to fold the index, including unary-minus over a constant.
    auto fv = try_eval_double(slice);
    if(fv.has_value() && *fv == std::floor(*fv))
    {
      mp_integer idx{(long long)*fv};
      const auto &st = to_struct_type(value.type());
      // Compute tuple length from struct components (named _0, _1...).
      std::size_t tup_len = 0;
      for(const auto &c : st.components())
      {
        std::string n = id2string(c.get_name());
        if(n.size() > 1 && n[0] == '_' && std::isdigit((unsigned char)n[1]))
          tup_len++;
      }
      if(idx < 0)
        idx += mp_integer{(long long)tup_len};
      // PLR §6.3.2: a constant tuple index outside [0, len) is a definite
      // IndexError ('tuple index out of range') -- e.g. `(1, 2)[9]`. (list/str
      // OOB are bounds-checked elsewhere; the fixed-tuple struct had no such
      // check, so an OOB constant index fell through to a nondet read.)
      if(idx < 0 || idx >= mp_integer{(long long)tup_len})
      {
        emit_conditional_exception(true_exprt{}, "IndexError");
        return side_effect_expr_nondett{
          python_value_type(), get_location(expr)};
      }
      std::string field = "_" + integer2string(idx);
      if(st.has_component(field))
        return member_exprt{value, field, st.get_component(field).type()};
    }
    if(slice.is_constant())
    {
      mp_integer idx;
      if(!to_integer(to_constant_expr(slice), idx))
      {
        const auto &st = to_struct_type(value.type());
        std::string field = "_" + integer2string(idx);
        if(st.has_component(field))
          return member_exprt{value, field, st.get_component(field).type()};
      }
    }
    log.error() << "Tuple indexing requires a constant index" << messaget::eom;
    // PLR §6.3.2: with a non-constant index, we can't pick a
    // specific tuple element statically. Returning nil_exprt
    // would propagate as a malformed argument into downstream
    // calls (CBMC SSA's equal_exprt, if_exprt invariants). Use
    // a sound over-approximation: nondet of python_value.
    log_overapprox(
      "tuple subscript with non-constant index — returning nondet "
      "python_value");
    return side_effect_expr_nondett{python_value_type(), get_location(expr)};
  }

  // Tagged union subscript: dispatch on the subscript type. An integer
  // index targets the LIST tag; a string key targets the DICT tag. (Blindly
  // taking the list path for a string key indexed a list array with a string,
  // forcing an unsound smt_string->int cast on the index.)
  if(is_python_value_type(value.type()))
  {
    // PLR §6.3.2: subscripting a value whose runtime tag is NOT a
    // subscriptable type (int/float/bool/complex/set/closure) raises
    // TypeError ("'X' object is not subscriptable"). The list-slot read
    // below assumed the LIST tag unconditionally, so e.g.
    // `def first(xs): return xs[0]` called as `first(5)` silently read
    // garbage instead of raising. Emit a tag obligation: the tag must be a
    // container (STR/LIST/DICT) or CLASS (the instance may define
    // __getitem__, dispatched on the concrete-struct path). NONE is already
    // handled (TypeError) at the top of this function.
    exprt subscriptable = or_exprt{
      or_exprt{
        python_value_is(value, python_type_tagt::STR),
        python_value_is(value, python_type_tagt::LIST)},
      or_exprt{
        python_value_is(value, python_type_tagt::DICT),
        python_value_is(value, python_type_tagt::CLASS)}};
    add_check(
      subscriptable,
      "python-type-error",
      "object is not subscriptable (TypeError)",
      get_location(expr));
    const bool int_slice =
      slice.type().id() == ID_signedbv || slice.type().id() == ID_unsignedbv ||
      slice.type().id() == ID_integer || slice.type() == python_int_type();
    if(int_slice)
    {
      exprt list_val = python_value_list(value);
      if(is_python_list_type(list_val.type()))
      {
        const auto &list_st = to_struct_type(list_val.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        member_exprt data{list_val, "data", data_type};
        return index_exprt{data, slice};
      }
    }
    // String key on a python_value (DICT tag) is not yet resolved to a
    // precise value here; fall through to the sound nondet python_value
    // over-approximation below rather than mis-indexing a list.
  }

  // PLR §6.3.2: subscripting a value of a concrete non-subscriptable scalar
  // type (int/float/bool) is a DEFINITE TypeError ("'int' object is not
  // subscriptable"). Reaching here with such a receiver (e.g. an unannotated
  // parameter inferred as int, called as `first(5); xs[0]`) previously fell
  // through to a silent nondet. The type is statically known, so assert false.
  if(
    value.type().id() == ID_signedbv || value.type().id() == ID_unsignedbv ||
    value.type().id() == ID_integer || value.type().id() == ID_floatbv ||
    value.type().id() == ID_bool || value.type() == python_int_type())
  {
    add_check(
      false_exprt{},
      "python-type-error",
      "object is not subscriptable (TypeError)",
      get_location(expr));
  }

  log_overapprox("Subscript: unsupported operand type, using nondet");
  // Returning nil_exprt would propagate as a malformed argument
  // into downstream function calls (CBMC SSA's equal_exprt,
  // if_exprt invariants). Sound over-approximation: nondet of
  // python_value (the most general Python type).
  return side_effect_expr_nondett{python_value_type(), get_location(expr)};
}

// PLR §6.2.5: List, set and tuple displays
// "A tuple display yields a new tuple object."
exprt python_convertert::convert_tuple(const jsont &expr)
{
  const jsont &elts = json_member(expr, "elts");
  if(!elts.is_array())
    return nil_exprt{};

  exprt::operandst elements;
  std::vector<typet> element_types;
  for(const auto &elt : as_array(elts))
  {
    exprt e = convert_expression(elt);
    if(e.is_nil())
      return nil_exprt{};
    element_types.push_back(e.type());
    elements.push_back(e);
  }

  struct_typet tuple_type = python_tuple_type(element_types);
  return struct_exprt{std::move(elements), tuple_type};
}

// PLR §6.2.5: List displays
// "A list display yields a new list object, the contents being specified
// by either a list of expressions or a comprehension."
exprt python_convertert::convert_list(const jsont &expr)
{
  const jsont &elts = json_member(expr, "elts");
  if(!elts.is_array())
    return nil_exprt{};

  // Collect elements and determine element type from first element
  exprt::operandst elements;
  for(const auto &elt : as_array(elts))
  {
    // PEP 448: iterable unpacking — [*a, 3, 4]. If the
    // starred expression wraps a list literal whose length
    // is known at conversion time, splice its elements
    // directly. Otherwise over-approximate by skipping the
    // starred element (sound — the resulting list is a
    // subset of what runtime would produce).
    if(is_node_type(elt, "Starred"))
    {
      const jsont &inner = json_member(elt, "value");
      exprt inner_expr = convert_expression(inner);
      if(inner_expr.is_nil())
        return nil_exprt{};

      const exprt *lit = nullptr;
      if(inner_expr.id() == ID_struct && inner_expr.operands().size() >= 2)
        lit = &inner_expr;
      else if(inner_expr.id() == ID_symbol)
      {
        auto it =
          list_literals.find(to_symbol_expr(inner_expr).get_identifier());
        if(it != list_literals.end())
          lit = &it->second;
      }
      if(
        lit != nullptr && lit->operands().size() >= 2 &&
        lit->operands()[0].is_constant())
      {
        mp_integer len_val;
        if(!to_integer(to_constant_expr(lit->operands()[0]), len_val))
        {
          const exprt &data_arr = lit->operands()[1];
          std::size_t n = len_val.to_ulong();
          for(std::size_t i = 0; i < n && i < data_arr.operands().size(); i++)
            elements.push_back(data_arr.operands()[i]);
          continue;
        }
      }
      log_overapprox(
        "PEP 448 iterable unpacking of non-literal list — skipping");
      continue;
    }

    // PLR §3.1: if the element is a Name that resolves to an
    // escaped list/dict-typed symbol (i.e. a name that we promoted
    // to `list[python_value]` / `dict[str, python_value]` storage),
    // wrap it as `make_python_value(LIST_or_DICT, &symbol)` —
    // pointing to the original symbol's storage, NOT a struct copy.
    // This makes `outer = [inner]; outer[0][0] = 99` write through
    // to inner.data[0].
    if(is_node_type(elt, "Name"))
    {
      std::string en = json_string(json_member(elt, "id"));
      irep_idt eq{qualify_name(en)};
      if(escaped_mutables.count(eq) > 0)
      {
        const symbolt *esym = symbol_table.lookup(eq);
        if(esym != nullptr)
        {
          if(is_python_list_type(esym->type))
          {
            elements.push_back(make_python_value(
              python_type_tagt::LIST, address_of_exprt{esym->symbol_expr()}));
            continue;
          }
          if(is_python_dict_type(esym->type))
          {
            elements.push_back(make_python_value(
              python_type_tagt::DICT, address_of_exprt{esym->symbol_expr()}));
            continue;
          }
        }
      }
    }

    exprt e = convert_expression(elt);
    if(e.is_nil())
      return nil_exprt{};
    // SPIKE (--python-ref-mutables): a mutable list element (a nested list
    // literal) is given a PER-INSTANCE heap object and stored as a reference
    // (make_python_value(LIST, heap_ptr)), so extraction/aliasing and
    // multi-instance construction are precise (no by-value copy). The heap
    // object is canonicalised to list[python_value] via rebuild_list_as_pv so
    // the deref-cast in python_value_list (which assumes list[python_value])
    // is type-correct -- otherwise a nested value read (g[0][0]) would
    // reinterpret the bytes of the natural element type. Named escaped
    // mutables are already handled above; this covers literals. Scope: lists
    // only (dicts/sets are phase 3, see the spike doc).
    if(ref_mutables && e.id() != ID_symbol && is_python_list_type(e.type()))
    {
      exprt canon = rebuild_list_as_pv(e);
      e = make_python_value(
        python_type_tagt::LIST, allocate_boxed_leaf(canon, canon.type()));
    }
    elements.push_back(e);
  }

  if(elements.empty())
  {
    // Empty list — default to int element type
    struct_typet list_type = python_list_type(python_int_type());
    // length and the array dimension are always signedbv[64] to match
    // python_list_type's invariant; using python_int_type() here would
    // diverge under --python-unbounded-ints (integer_typet) and break
    // the struct-assignment type check.
    exprt length = from_integer(0, signedbv_typet{64});
    array_typet data_type{
      python_int_type(),
      from_integer(PYTHON_MAX_LIST_LENGTH, signedbv_typet{64})};
    exprt::operandst zeros;
    for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
      zeros.push_back(from_integer(0, python_int_type()));
    array_exprt data{std::move(zeros), data_type};
    return struct_exprt{{length, data}, list_type};
  }

  typet elem_type = elements[0].type();
  // Check for mixed types — use tagged union for heterogeneous lists
  bool is_heterogeneous = false;
  for(const auto &e : elements)
  {
    if(e.type() != elem_type)
    {
      is_heterogeneous = true;
      elem_type = python_value_type();
      break;
    }
  }
  // The backing array has a fixed maximum size (PYTHON_MAX_LIST_LENGTH);
  // however, list literals in stdlib code can exceed it (e.g.
  // typing.py's 100+-entry '__all__'). Grow the array size to the
  // number of elements whenever that exceeds the default so the
  // resulting IR is self-consistent (operands count == array size).
  const std::size_t list_array_size =
    std::max<std::size_t>(PYTHON_MAX_LIST_LENGTH, elements.size());
  struct_typet list_type = python_list_type(elem_type);
  // Rebuild the struct's array-typed 'data' component to match the
  // actual literal size. python_list_type always returns the struct
  // with the default max length, so override the data component's
  // array type here.
  {
    auto &comps = list_type.components();
    if(comps.size() == 2)
      comps[1].type() = array_typet{
        elem_type, from_integer(list_array_size, signedbv_typet{64})};
  }
  array_typet data_type{
    elem_type, from_integer(list_array_size, signedbv_typet{64})};

  // Build data array: elements followed by zeros
  exprt::operandst data_elems;
  for(auto &e : elements)
  {
    if(is_heterogeneous)
      e = wrap_value(e);
    else if(e.type() != elem_type)
      e = typecast_exprt{e, elem_type};
    data_elems.push_back(e);
  }
  while(data_elems.size() < list_array_size)
    data_elems.push_back(safe_zero(elem_type));

  array_exprt data{std::move(data_elems), data_type};
  exprt length =
    from_integer(static_cast<long long>(elements.size()), signedbv_typet{64});

  return struct_exprt{{length, data}, list_type};
}

// §11b: resolve and call an @property getter via the MRO so that
// inherited properties (and properties accessed through a pointer
// receiver) dispatch to the getter instead of reading a field/nondet.
exprt python_convertert::emit_property_get(
  const std::string &class_name,
  const std::string &attr,
  const exprt &self_ptr,
  const source_locationt &loc)
{
  std::vector<std::string> chain;
  auto mro_it = class_mro.find(class_name);
  if(mro_it != class_mro.end())
    chain = mro_it->second;
  if(chain.empty())
    chain.push_back(class_name);

  const symbolt *msym = nullptr;
  for(const std::string &anc : chain)
  {
    auto pit = class_property_methods.find(anc);
    if(pit == class_property_methods.end() || pit->second.count(attr) == 0)
      continue;
    const symbolt *s =
      symbol_table.lookup(irep_idt{"python::" + anc + "::" + attr});
    if(s != nullptr && s->type.id() == ID_code)
    {
      msym = s;
      break;
    }
  }
  if(msym == nullptr)
    return nil_exprt{};

  const code_typet &mty = to_code_type(msym->type);
  static unsigned prop_get_ctr = 0;
  std::string tn = "__prop_" + std::to_string(prop_get_ctr++);
  irep_idt ti{qualify_name(tn)};
  if(symbol_table.lookup(ti) == nullptr)
  {
    symbolt ts{ti, mty.return_type(), "python"};
    ts.base_name = tn;
    ts.is_lvalue = true;
    ts.is_state_var = true;
    symbol_table.add(ts);
  }
  symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
  exprt self = self_ptr;
  if(!mty.parameters().empty() && self.type() != mty.parameters()[0].type())
    self = typecast_exprt{self, mty.parameters()[0].type()};
  pending_checks.push_back(code_frontend_assignt{
    tv,
    side_effect_expr_function_callt{
      msym->symbol_expr(), {self}, mty.return_type(), loc}});
  return std::move(tv);
}

// PLR §3.3.2: dispatch a @property setter on `obj.<attr> = value`.
std::optional<codet> python_convertert::emit_property_set(
  const std::string &class_name,
  const std::string &attr,
  const exprt &obj_ptr,
  const exprt &value,
  const source_locationt &loc)
{
  std::vector<std::string> chain;
  auto mro_it = class_mro.find(class_name);
  if(mro_it != class_mro.end())
    chain = mro_it->second;
  if(chain.empty())
    chain.push_back(class_name);

  std::string setter_id;
  for(const std::string &anc : chain)
  {
    auto pit = class_property_setters.find(anc);
    if(pit != class_property_setters.end())
    {
      auto ait = pit->second.find(attr);
      if(ait != pit->second.end())
      {
        setter_id = ait->second;
        break;
      }
    }
  }
  if(setter_id.empty())
  {
    // PLR §3.3.2: assigning to a READ-ONLY property (a getter with no setter
    // across the MRO) raises AttributeError ("property '...' of '...' object
    // has no setter"). Distinguish from a plain non-property attribute store,
    // which returns nullopt to take the normal (shadowing) path.
    bool is_readonly_property = false;
    for(const std::string &anc : chain)
    {
      auto pit = class_property_methods.find(anc);
      if(pit != class_property_methods.end() && pit->second.count(attr) > 0)
      {
        is_readonly_property = true;
        break;
      }
    }
    if(!is_readonly_property)
      return std::nullopt;
    code_blockt blk;
    const symbolt *ea = symbol_table.lookup("python::__exception_active");
    const symbolt *et = symbol_table.lookup("python::__exception_type");
    if(ea != nullptr)
    {
      blk.add(code_frontend_assignt{ea->symbol_expr(), true_exprt{}});
      if(et != nullptr)
        blk.add(code_frontend_assignt{
          et->symbol_expr(),
          from_integer(exception_type_hash("AttributeError"), et->type)});
    }
    return std::move(blk);
  }
  const symbolt *ssym = symbol_table.lookup(irep_idt{setter_id});
  if(ssym == nullptr || ssym->type.id() != ID_code)
    return std::nullopt;

  const code_typet &mty = to_code_type(ssym->type);
  exprt::operandst args;
  // self (the instance pointer), coerced to the setter's first parameter type.
  if(!mty.parameters().empty())
  {
    const typet &st = mty.parameters()[0].type();
    args.push_back(
      obj_ptr.type() == st ? obj_ptr : typecast_exprt{obj_ptr, st});
  }
  // value, coerced to the setter's declared value parameter type (an Any/
  // python_value param is boxed via wrap_value; otherwise a safe typecast).
  if(mty.parameters().size() >= 2)
  {
    const typet &vt = mty.parameters()[1].type();
    exprt v = value;
    if(v.type() != vt)
      v = is_python_value_type(vt) && !is_python_value_type(v.type())
            ? wrap_value(v)
            : safe_typecast(v, vt);
    args.push_back(v);
  }
  return code_expressiont{side_effect_expr_function_callt{
    ssym->symbol_expr(), args, mty.return_type(), loc}};
}

// §11b: dispatch a custom descriptor's __get__ on attribute read.
exprt python_convertert::emit_descriptor_get(
  const std::string &class_name,
  const std::string &attr,
  const exprt &obj_ptr,
  const source_locationt &loc)
{
  std::vector<std::string> chain;
  auto mro_it = class_mro.find(class_name);
  if(mro_it != class_mro.end())
    chain = mro_it->second;
  if(chain.empty())
    chain.push_back(class_name);

  std::string owner, desc_cls;
  for(const std::string &anc : chain)
  {
    auto dit = class_descriptor_attrs.find(anc);
    if(dit != class_descriptor_attrs.end())
    {
      auto ait = dit->second.find(attr);
      if(ait != dit->second.end())
      {
        owner = anc;
        desc_cls = ait->second;
        break;
      }
    }
  }
  if(desc_cls.empty())
    return nil_exprt{};
  const symbolt *msym =
    symbol_table.lookup(irep_idt{"python::" + desc_cls + "::__get__"});
  if(msym == nullptr || msym->type.id() != ID_code)
    return nil_exprt{};

  const code_typet &mty = to_code_type(msym->type);
  // descriptor instance = the owning class object's storage for attr.
  exprt self_desc;
  auto owner_obj = mro_owner_class_object(class_name, attr);
  auto dc_it = class_types.find(desc_cls);
  if(owner_obj && dc_it != class_types.end())
    self_desc = address_of_exprt{member_exprt{*owner_obj, attr, dc_it->second}};
  else if(!mty.parameters().empty())
    self_desc = side_effect_expr_nondett{mty.parameters()[0].type(), loc};
  else
    return nil_exprt{};

  static unsigned desc_ctr = 0;
  std::string tn = "__descget_" + std::to_string(desc_ctr++);
  irep_idt ti{qualify_name(tn)};
  if(symbol_table.lookup(ti) == nullptr)
  {
    symbolt ts{ti, mty.return_type(), "python"};
    ts.base_name = tn;
    ts.is_lvalue = true;
    ts.is_state_var = true;
    symbol_table.add(ts);
  }
  symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
  // Args: (descriptor self, obj, objtype=None), padded/typecast to the
  // getter's signature.
  exprt::operandst args;
  if(!mty.parameters().empty())
    args.push_back(
      self_desc.type() == mty.parameters()[0].type()
        ? self_desc
        : typecast_exprt{self_desc, mty.parameters()[0].type()});
  if(mty.parameters().size() >= 2)
  {
    const typet &ot = mty.parameters()[1].type();
    exprt obj;
    if(is_python_value_type(ot))
    {
      // Box the instance as a CLASS python_value via the CANONICAL
      // coercion (coerce_to_typed_slot also sets `__class_tag` on the
      // caller's storage), so `obj.<field>` inside the descriptor method
      // resolves to the right class and ALIASES the real instance —
      // shared between __get__ and __set__. A hand-rolled
      // make_python_value(CLASS, &obj) omits the tag and does not alias.
      exprt inst = obj_ptr.id() == ID_address_of
                     ? to_address_of_expr(obj_ptr).object()
                     : static_cast<exprt>(dereference_exprt{obj_ptr});
      obj = coerce_to_typed_slot(inst, ot);
    }
    else if(obj_ptr.type() != ot)
      obj = typecast_exprt{obj_ptr, ot};
    else
      obj = obj_ptr;
    args.push_back(obj);
  }
  if(mty.parameters().size() >= 3)
    args.push_back(
      from_integer(python_none_sentinel_int(), mty.parameters()[2].type()));
  pending_checks.push_back(code_frontend_assignt{
    tv,
    side_effect_expr_function_callt{
      msym->symbol_expr(), args, mty.return_type(), loc}});
  return std::move(tv);
}

std::optional<codet> python_convertert::emit_descriptor_set(
  const std::string &class_name,
  const std::string &attr,
  const exprt &obj_ptr,
  const exprt &value,
  const source_locationt &loc)
{
  std::vector<std::string> chain;
  auto mro_it = class_mro.find(class_name);
  if(mro_it != class_mro.end())
    chain = mro_it->second;
  if(chain.empty())
    chain.push_back(class_name);

  std::string desc_cls;
  for(const std::string &anc : chain)
  {
    auto dit = class_descriptor_attrs.find(anc);
    if(dit != class_descriptor_attrs.end())
    {
      auto ait = dit->second.find(attr);
      if(ait != dit->second.end())
      {
        desc_cls = ait->second;
        break;
      }
    }
  }
  if(desc_cls.empty())
    return std::nullopt;
  // A DATA descriptor defines __set__; without it the assignment is a
  // plain (shadowing) store, not a descriptor write.
  const symbolt *ssym =
    symbol_table.lookup(irep_idt{"python::" + desc_cls + "::__set__"});
  if(ssym == nullptr || ssym->type.id() != ID_code)
    return std::nullopt;
  const code_typet &mty = to_code_type(ssym->type);
  // descriptor instance = the owning class object's storage for attr.
  auto owner_obj = mro_owner_class_object(class_name, attr);
  auto dc_it = class_types.find(desc_cls);
  if(!owner_obj || dc_it == class_types.end())
    return std::nullopt;
  exprt self_desc =
    address_of_exprt{member_exprt{*owner_obj, attr, dc_it->second}};

  // Args: (descriptor self, obj, value), coerced to __set__'s signature.
  exprt::operandst args;
  if(!mty.parameters().empty())
    args.push_back(
      self_desc.type() == mty.parameters()[0].type()
        ? self_desc
        : typecast_exprt{self_desc, mty.parameters()[0].type()});
  if(mty.parameters().size() >= 2)
  {
    const typet &ot = mty.parameters()[1].type();
    exprt obj;
    if(is_python_value_type(ot))
    {
      // Box the instance as a CLASS python_value via the CANONICAL
      // coercion (coerce_to_typed_slot also sets `__class_tag` on the
      // caller's storage), so `obj.<field>` inside the descriptor method
      // resolves to the right class and ALIASES the real instance —
      // shared between __get__ and __set__. A hand-rolled
      // make_python_value(CLASS, &obj) omits the tag and does not alias.
      exprt inst = obj_ptr.id() == ID_address_of
                     ? to_address_of_expr(obj_ptr).object()
                     : static_cast<exprt>(dereference_exprt{obj_ptr});
      obj = coerce_to_typed_slot(inst, ot);
    }
    else if(obj_ptr.type() != ot)
      obj = typecast_exprt{obj_ptr, ot};
    else
      obj = obj_ptr;
    args.push_back(obj);
  }
  if(mty.parameters().size() >= 3)
  {
    exprt v = value;
    const typet &vt = mty.parameters()[2].type();
    if(v.type() != vt)
      v = is_python_value_type(vt) && !is_python_value_type(v.type())
            ? wrap_value(v)
            : safe_typecast(v, vt);
    args.push_back(v);
  }
  return code_expressiont{side_effect_expr_function_callt{
    ssym->symbol_expr(), args, mty.return_type(), loc}};
}

// True if `cls` or any MRO ancestor defines `method` as a code symbol.
bool python_convertert::class_mro_defines(
  const std::string &cls,
  const std::string &method)
{
  std::vector<std::string> chain;
  auto it = class_mro.find(cls);
  if(it != class_mro.end())
    chain = it->second;
  if(chain.empty())
    chain.push_back(cls);
  for(const std::string &anc : chain)
  {
    const symbolt *s =
      symbol_table.lookup(irep_idt{"python::" + anc + "::" + method});
    if(s != nullptr && s->type.id() == ID_code)
      return true;
  }
  return false;
}

bool python_convertert::slots_forbidden_attr(
  const std::string &cls,
  const std::string &attr) const
{
  if(class_slots.find(cls) == class_slots.end())
    return false; // no __slots__ on the class => __dict__ => any attr allowed
  std::vector<std::string> chain;
  auto mit = class_mro.find(cls);
  if(mit != class_mro.end())
    chain = mit->second;
  if(chain.empty())
    chain.push_back(cls);
  std::set<std::string> allowed;
  for(const std::string &b : chain)
  {
    if(b == "object")
      continue;
    auto bs = class_slots.find(b);
    if(bs == class_slots.end())
      // A base without a known __slots__ record contributes a __dict__ (or is
      // unknown/imported): not slots-enforced -> never flag (sound, no FP).
      return false;
    for(const std::string &s : bs->second)
      allowed.insert(s);
  }
  return allowed.count(attr) == 0;
}

// Whole-group helper for the dunder-protocol-missing checks (subscript /
// iteration / call / with / setitem / delitem / contains / ...). Returns true
// iff `t` is a CONCRETE user-class instance type (python_class_*) whose MRO
// defines no `dunder`. Returns false for builtins, python_value (Any), and
// non-class types, so a missing-protocol exception is emitted only when a
// violation is provable -- no false positive on Any / symbolic / builtin
// receivers, and an inherited dunder is respected via class_mro_defines.
bool python_convertert::concrete_class_lacks_dunder(
  const typet &t,
  const char *dunder)
{
  typet bt = t;
  if(bt.id() == ID_pointer)
    bt = to_pointer_type(bt).base_type();
  std::string tag;
  if(bt.id() == ID_struct)
    tag = id2string(to_struct_type(bt).get_tag());
  else if(bt.id() == ID_struct_tag)
    tag = id2string(to_struct_tag_type(bt).get_identifier());
  if(tag.compare(0, 13, "python_class_") != 0)
    return false;
  return !class_mro_defines(tag.substr(13), dunder);
}

// PLR §3.2: a dict key / set element must be hashable. The built-in mutable
// containers list/dict/set are unhashable, so using one as a key or set element
// raises TypeError ('unhashable type'). Shared predicate for the hashability
// checks at the dict-literal / dict-store / dict-comp / set sites. (A tuple is
// hashable, and a user class is hashable by default via object.__hash__, so
// neither is flagged -- only the concrete mutable builtins.)
bool python_convertert::is_unhashable_type(const typet &t)
{
  if(is_python_list_type(t) || is_python_dict_type(t) || is_python_set_type(t))
    return true;
  // PLR §3.3.1: an instance of a class whose own body defines __eq__ without
  // __hash__ (or sets `__hash__ = None`) is unhashable.
  if(!class_eq_without_hash.empty())
  {
    std::string tag;
    if(t.id() == ID_struct)
      tag = id2string(to_struct_type(t).get_tag());
    else if(t.id() == ID_struct_tag)
      tag = id2string(to_struct_tag_type(t).get_identifier());
    if(tag.rfind("python_class_", 0) == 0)
      tag = tag.substr(13);
    if(!tag.empty() && class_eq_without_hash.count(tag) > 0)
      return true;
  }
  return false;
}

std::optional<mp_integer>
python_convertert::python_numeric_key(const exprt &v) const
{
  if(!v.is_constant())
    return std::nullopt;
  const typet &t = v.type();
  if(t.id() == ID_bool)
    return v.is_true() ? mp_integer{1} : mp_integer{0};
  if(
    t.id() == ID_signedbv || t.id() == ID_unsignedbv || t.id() == ID_integer ||
    t.id() == ID_c_bool)
  {
    mp_integer iv;
    if(!to_integer(to_constant_expr(v), iv))
      return iv;
    return std::nullopt;
  }
  if(t.id() == ID_floatbv)
  {
    ieee_floatt f(
      to_constant_expr(v), ieee_floatt::rounding_modet::ROUND_TO_EVEN);
    if(f.is_NaN() || f.is_infinity())
      return std::nullopt;
    // Integral value (1.0) participates in numeric key equality with int/bool
    // (1 == 1.0, hash(1) == hash(1.0)); a non-integral float (1.5) does not.
    const mp_integer iv = f.to_integer();
    ieee_floatt round_trip(f.spec, ieee_floatt::rounding_modet::ROUND_TO_EVEN);
    round_trip.from_integer(iv);
    if(round_trip == f)
      return iv;
    return std::nullopt;
  }
  return std::nullopt;
}
// PLR §3.3.1: whether `e` is a PROVABLY non-iterable scalar -- a concrete
// numeric (int/float/bool), complex, or a constant None. Iterating or unpacking
// such a value (`for x in 5`, `a, b = None`, `[x for x in 3j]`) raises
// TypeError. Shared by the for-loop, tuple/list-unpack and comprehension sites
// (the "non-iterable operand" whole-group). PLR-correct: only PROVABLE scalars
// fire -- str/list/tuple/set/dict/range/generator are iterable, a `python_value`
// (Any) MIGHT be iterable at runtime, and a class instance is handled by the
// separate __iter__/__getitem__ protocol check (never here).
bool python_convertert::provably_non_iterable_scalar(const exprt &e) const
{
  const typet &t = e.type();
  const irep_idt tid = t.id();
  if(
    tid == ID_signedbv || tid == ID_unsignedbv || tid == ID_integer ||
    tid == ID_floatbv || tid == ID_fixedbv || tid == ID_bool ||
    tid == ID_c_bool)
    return true;
  if(tid == ID_struct && to_struct_type(t).get_tag() == "python_complex")
    return true;
  if(is_python_none_constant(e))
    return true;
  return false;
}

int python_convertert::orderable_category_of(const exprt &e)
{
  const typet &t = e.type();
  if(
    t.id() == ID_signedbv || t.id() == ID_unsignedbv || t.id() == ID_integer ||
    t.id() == ID_floatbv || t.id() == ID_bool)
    return 1; // numeric
  if(is_python_string_type(t))
    return 2; // str
  if(is_python_none_constant(e))
    return 7; // None: orderable with nothing
  if(is_python_list_type(t))
    return 3;
  if(is_python_tuple_type(t))
    return 4;
  if(is_python_set_type(t))
    return 5;
  if(is_python_dict_type(t))
    return 6;
  // A CONSTANT python_value (a make_python_value struct literal) carries a
  // statically-known __tag; recover the category from it so a boxed literal
  // element (e.g. "a" in the mixed list [1, "a"]) is seen as str. A symbolic
  // python_value is NOT a struct_exprt, so it returns 0 (never flagged).
  if(is_python_value_type(t) && e.id() == ID_struct)
  {
    static const struct_typet pv = python_value_struct_def();
    std::size_t idx = pv.components().size();
    for(std::size_t i = 0; i < pv.components().size(); ++i)
      if(pv.components()[i].get_name() == "__tag")
      {
        idx = i;
        break;
      }
    if(idx < e.operands().size() && e.operands()[idx].is_constant())
    {
      mp_integer tv;
      if(!to_integer(to_constant_expr(e.operands()[idx]), tv))
      {
        const int tg = tv.to_long();
        if(
          tg == static_cast<int>(python_type_tagt::INT) ||
          tg == static_cast<int>(python_type_tagt::FLOAT) ||
          tg == static_cast<int>(python_type_tagt::BOOL))
          return 1;
        if(tg == static_cast<int>(python_type_tagt::STR))
          return 2;
      }
    }
  }
  return 0; // unknown / not flaggable
}

int python_convertert::value_format_category(const typet &t)
{
  if(
    t.id() == ID_signedbv || t.id() == ID_unsignedbv || t.id() == ID_integer ||
    t.id() == ID_bool)
    return 1; // int
  if(t.id() == ID_floatbv)
    return 2; // float
  if(is_python_string_type(t))
    return 3; // str
  return 0;   // other / Any
}

const char *
python_convertert::format_code_violation(char code, int cat, bool percent)
{
  if(cat == 0)
    return nullptr; // unknown value type -> never flag
  switch(code)
  {
  // Integer presentation codes.
  case 'd':
  case 'i':
  case 'o':
  case 'x':
  case 'b':
  case 'n':
    if(percent)
      return cat == 3 ? "TypeError" : nullptr; // %d etc.: number ok, str fails
    return cat == 1 ? nullptr : "ValueError";  // {:d}: requires int
  // Float presentation codes.
  case 'e':
  case 'f':
  case 'g':
  case '%':
    if(percent)
      return cat == 3 ? "TypeError" : nullptr; // %f: number ok, str fails
    return cat == 3 ? "ValueError" : nullptr;  // {:f}: number ok, str fails
  // String presentation.
  case 's':
    if(percent)
      return nullptr;                         // %s accepts anything
    return cat == 3 ? nullptr : "ValueError"; // {:s}: requires str
  default:
    return nullptr; // r/a/c/none/unknown -> don't flag (conservative)
  }
}

// §11b: dispatch __getattr__ when normal attribute lookup fails.
exprt python_convertert::emit_getattr_fallback(
  const exprt &value,
  const std::string &attr,
  const source_locationt &loc)
{
  std::string cls;
  exprt self_ptr;
  if(
    value.type().id() == ID_pointer &&
    to_pointer_type(value.type()).base_type().id() == ID_struct)
  {
    cls = id2string(
      to_struct_type(to_pointer_type(value.type()).base_type()).get_tag());
    self_ptr = value;
  }
  else if(value.type().id() == ID_struct)
  {
    cls = id2string(to_struct_type(value.type()).get_tag());
    self_ptr = address_of_exprt{value};
  }
  else
    return nil_exprt{};
  if(cls.substr(0, 13) != "python_class_")
    return nil_exprt{};
  cls = cls.substr(13);

  std::vector<std::string> chain;
  auto mro_it = class_mro.find(cls);
  if(mro_it != class_mro.end())
    chain = mro_it->second;
  if(chain.empty())
    chain.push_back(cls);
  const symbolt *msym = nullptr;
  for(const std::string &anc : chain)
  {
    const symbolt *s =
      symbol_table.lookup(irep_idt{"python::" + anc + "::__getattr__"});
    if(s != nullptr && s->type.id() == ID_code)
    {
      msym = s;
      break;
    }
  }
  if(msym == nullptr)
    return nil_exprt{};

  const code_typet &mty = to_code_type(msym->type);
  static unsigned getattr_ctr = 0;
  std::string tn = "__getattr_" + std::to_string(getattr_ctr++);
  irep_idt ti{qualify_name(tn)};
  if(symbol_table.lookup(ti) == nullptr)
  {
    symbolt ts{ti, mty.return_type(), "python"};
    ts.base_name = tn;
    ts.is_lvalue = true;
    ts.is_state_var = true;
    symbol_table.add(ts);
  }
  symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
  exprt self = self_ptr;
  if(!mty.parameters().empty() && self.type() != mty.parameters()[0].type())
    self = typecast_exprt{self, mty.parameters()[0].type()};
  pending_checks.push_back(code_frontend_assignt{
    tv,
    side_effect_expr_function_callt{
      msym->symbol_expr(),
      {self, python_string_literal(attr)},
      mty.return_type(),
      loc}});
  return std::move(tv);
}

// PLR §6.3.1: Attribute references
// "An attribute reference is a primary followed by a period and a name."
exprt python_convertert::convert_attribute(const jsont &expr)
{
  std::string attr = json_string(json_member(expr, "attr"));

  // PLR §8.13: enum member `.value` / `.name`.
  // `EnumClass.MEMBER.value` → the member's value (which is what the
  // member access already resolves to); `.name` → the member-name string.
  // Member access and `==` already work via the class-object machinery;
  // this only fills in the two accessors that were returning nondet.
  if(attr == "value" || attr == "name")
  {
    const jsont &inner = json_member(expr, "value");
    if(is_node_type(inner, "Attribute"))
    {
      const jsont &inner_obj = json_member(inner, "value");
      if(is_node_type(inner_obj, "Name"))
      {
        const std::string cls = json_string(json_member(inner_obj, "id"));
        const std::string mem = json_string(json_member(inner, "attr"));
        auto eit = enum_members.find(cls);
        if(eit != enum_members.end() && eit->second.count(mem) > 0)
        {
          if(attr == "name")
            return python_string_literal(mem);
          // `.value`: the member access (EnumClass.MEMBER) resolves to
          // the assigned value via the class object.
          return convert_expression(inner);
        }
      }
    }
    // enum-member VARIABLE: `s.value` where `s = SomeEnum.MEMBER` was recorded
    // in enum_member_vars. The variable already stores the member's value (so
    // `s == SomeEnum.MEMBER` holds), but `.value` on a Name otherwise fell
    // through to a nondet attribute read. Return the variable itself -- its
    // stored value IS the member's value (with the correct runtime tag for a
    // heterogeneous enum, so `s.value` used at the wrong type is caught by the
    // operand/tag obligations). `.name` is left nondet (the member identity is
    // not recovered from a reassignable variable).
    if(attr == "value" && is_node_type(inner, "Name"))
    {
      irep_idt sid{qualify_name(json_string(json_member(inner, "id")))};
      if(enum_member_vars.count(sid) > 0)
        return convert_expression(inner);
    }
    // enum-typed FIELD: `obj.<field>.value` where field was annotated with an
    // enum class (`self.<field>: SomeEnum`). The field stores the member value
    // (retagged on each `self.<field> = E.M`), so `.value` is the field itself.
    // Closes the field analogue of the variable case, incl. the heterogeneous
    // retag-after-method-mutation false proof (enum-value-after-mutation).
    if(attr == "value" && is_node_type(inner, "Attribute"))
    {
      const std::string fld = json_string(json_member(inner, "attr"));
      exprt base = convert_expression(json_member(inner, "value"));
      typet bt = base.type();
      if(bt.id() == ID_pointer)
        bt = to_pointer_type(bt).base_type();
      std::string btag;
      if(bt.id() == ID_struct)
        btag = id2string(to_struct_type(bt).get_tag());
      else if(bt.id() == ID_struct_tag)
        btag = id2string(to_struct_tag_type(bt).get_identifier());
      const auto pos = btag.find("python_class_");
      if(pos != std::string::npos)
      {
        const std::string cls = btag.substr(pos + 13);
        auto fit = enum_typed_fields.find(cls);
        if(fit != enum_typed_fields.end() && fit->second.count(fld) > 0)
          return convert_expression(inner);
      }
    }
  }

  // icontract Phase 5: resolve OLD.<name> to the captured
  // snapshot symbol while translating an ensure lambda body.
  // active_old_snapshots is populated by convert_function_def
  // for the duration of ensure-lambda translation only;
  // outside that scope, OLD is treated as an ordinary name
  // and Python's normal resolution applies.
  if(
    !active_old_snapshots.empty() &&
    is_node_type(json_member(expr, "value"), "Name") &&
    json_string(json_member(json_member(expr, "value"), "id")) == "OLD")
  {
    auto it = active_old_snapshots.find(attr);
    if(it != active_old_snapshots.end())
      return it->second;
  }

  // Math module constants: math.pi, math.e, etc.
  // Check BEFORE converting value (which would fail for module names)
  if(
    json_member(expr, "value").is_object() &&
    is_node_type(json_member(expr, "value"), "Name"))
  {
    std::string obj_name =
      json_string(json_member(json_member(expr, "value"), "id"));
    if(obj_name == "math")
    {
      if(attr == "pi")
        return double_to_floatbv(M_PI);
      if(attr == "e")
        return double_to_floatbv(M_E);
      if(attr == "tau")
        return double_to_floatbv(2.0 * M_PI);
      if(attr == "inf")
      {
        ieee_floatt v{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        v.make_plus_infinity();
        return v.to_expr();
      }
      if(attr == "nan")
      {
        ieee_floatt v{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        v.make_NaN();
        return v.to_expr();
      }
    }
    if(obj_name == "string")
    {
      // PLR: string module constants. CPython's `string` module
      // exposes a handful of static character-class strings.
      // Map each here so `string.digits == "0123456789"` etc.
      // fold directly without going through the library lookup
      // (which currently leaves them as nondet symbols).
      static const std::map<std::string, std::string> consts = {
        {"ascii_lowercase", "abcdefghijklmnopqrstuvwxyz"},
        {"ascii_uppercase", "ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
        {"ascii_letters",
         "abcdefghijklmnopqrstuvwxyz"
         "ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
        {"digits", "0123456789"},
        {"hexdigits", "0123456789abcdefABCDEF"},
        {"octdigits", "01234567"},
        {"punctuation", "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"},
        {"whitespace", " \t\n\r\x0b\x0c"},
        {"printable",
         "0123456789"
         "abcdefghijklmnopqrstuvwxyz"
         "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
         "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"
         " \t\n\r\x0b\x0c"}};
      auto it = consts.find(attr);
      if(it != consts.end())
        return python_string_literal(it->second);
    }

    // General imported-module constant: e.g. re.IGNORECASE / re.DOTALL and
    // any module-level `NAME = <literal>` in a library stub. When the module
    // is imported and python::<attr> is a registered symbol holding a
    // constant value, fold to that value instead of a nondet attribute read.
    // (Class/function attributes have no constant value and are left to the
    // normal path.)
    if(imported_modules.count(obj_name) > 0)
    {
      const symbolt *cs = symbol_table.lookup(irep_idt{"python::" + attr});
      if(cs != nullptr && cs->value.is_not_nil() && cs->value.is_constant())
        return cs->value;
    }
  }

  exprt value = convert_expression(json_member(expr, "value"));

  if(value.is_nil())
    return nil_exprt{};

  // PLR §6.10: 'AttributeError: 'NoneType' object has no
  // attribute X'. Reading any attribute on None raises
  // AttributeError. Set __exception_active=True with
  // AttributeError tag and emit a nondet python_value result so
  // the rest of the conversion has something to bind. Mirrors
  // the iter-None / ordering-with-None / len(None) shape.
  if(is_python_none(value, symbol_table))
  {
    const symbolt *exc_sym = symbol_table.lookup("python::__exception_active");
    const symbolt *exc_type_sym =
      symbol_table.lookup("python::__exception_type");
    if(exc_sym != nullptr)
    {
      pending_checks.push_back(
        code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
      if(exc_type_sym != nullptr)
      {
        long h = exception_type_hash("AttributeError");
        pending_checks.push_back(code_frontend_assignt{
          exc_type_sym->symbol_expr(), from_integer(h, exc_type_sym->type)});
      }
    }
    return side_effect_expr_nondett{python_value_type(), get_location(expr)};
  }

  // PLR §3.3.2: a class that overrides __getattribute__ (intercepts EVERY
  // attribute access) or __setattr__ (intercepts every write, so a stored
  // field value can no longer be trusted) is not modelled by the frontend.
  // Reading the struct field directly would silently return a wrong value
  // (e.g. the `__setattr__` that stores a str into an int field -> the read
  // returns a stale int instead of raising TypeError when used). Over-
  // approximate such an instance read to a nondet python_value (Any), so a
  // subsequent use routes through the operator/subscript/call tag obligations
  // (a possible TypeError) instead of fabricating a concrete value. NOTE:
  // __getattr__ (the MISSING-attribute fallback only) is modelled precisely
  // via emit_getattr_fallback and is deliberately NOT over-approximated. The
  // class object itself (ClassName.attr) is excluded -- instance dunders do
  // not intercept class-object access.
  {
    std::string rcls;
    if(
      value.type().id() == ID_pointer &&
      to_pointer_type(value.type()).base_type().id() == ID_struct)
      rcls = id2string(
        to_struct_type(to_pointer_type(value.type()).base_type()).get_tag());
    else if(value.type().id() == ID_struct)
      rcls = id2string(to_struct_type(value.type()).get_tag());
    if(rcls.compare(0, 13, "python_class_") == 0)
    {
      const std::string bare = rcls.substr(13);
      bool is_class_object =
        value.id() == ID_symbol &&
        id2string(to_symbol_expr(value).get_identifier()) == "python::" + bare;
      if(
        !is_class_object && (class_mro_defines(bare, "__getattribute__") ||
                             class_mro_defines(bare, "__setattr__")))
        return side_effect_expr_nondett{
          python_value_type(), get_location(expr)};
    }
  }

  // PLR §6.10: AttributeError for an attribute a numeric scalar does
  // not define, e.g. (5).foo. Restricted to a *constant* numeric
  // receiver (a literal, or a variable folded to one): a non-constant
  // numeric-typed value may be a non-scalar object the frontend
  // defaulted to int (e.g. a stub return like datetime.now()), where
  // flagging would be a false positive. Dunders and known public
  // attributes are left alone.
  {
    const typet &vt = value.type();
    bool numeric = vt.id() == ID_signedbv || vt.id() == ID_unsignedbv ||
                   vt.id() == ID_floatbv || vt.id() == ID_integer ||
                   vt.id() == ID_bool;
    bool is_dunder = attr.size() >= 4 && attr.compare(0, 2, "__") == 0 &&
                     attr.compare(attr.size() - 2, 2, "__") == 0;
    static const std::set<std::string> numeric_attrs = {
      "bit_length",
      "bit_count",
      "to_bytes",
      "from_bytes",
      "conjugate",
      "numerator",
      "denominator",
      "real",
      "imag",
      "as_integer_ratio",
      "is_integer",
      "hex",
      "fromhex"};
    if(
      numeric && value.is_constant() && !is_dunder &&
      numeric_attrs.count(attr) == 0)
    {
      emit_conditional_exception(true_exprt{}, "AttributeError");
      return side_effect_expr_nondett{python_value_type(), get_location(expr)};
    }
  }

  // If value is a pointer (self in a method), dereference first
  if(value.type().id() == ID_pointer)
  {
    const auto &base = to_pointer_type(value.type()).base_type();
    if(base.id() == ID_struct)
    {
      const auto &st = to_struct_type(base);
      // §11b: @property dispatch on a pointer receiver (self, or a
      // class-typed parameter). The getter is resolved via the MRO so
      // own and inherited properties both work; without this the read
      // fell through to a field/nondet.
      {
        std::string ptag = id2string(st.get_tag());
        if(ptag.substr(0, 13) == "python_class_")
        {
          exprt pg =
            emit_property_get(ptag.substr(13), attr, value, get_location(expr));
          if(!pg.is_nil())
            return pg;
          exprt dg = emit_descriptor_get(
            ptag.substr(13), attr, value, get_location(expr));
          if(!dg.is_nil())
            return dg;
        }
      }
      if(st.has_component(attr))
      {
        // PLR §9.4: instance attribute read on a class
        // instance. If the attribute is a class-level
        // attribute (defined at class body, not via
        // self.X = in __init__), emit a shadow-fallback
        // ternary: read from instance if the synthetic
        // __shadow_<attr> flag is set, else fall back to
        // the class object's storage so updates to
        // Class.<attr> propagate to instances that have
        // not shadowed the field.
        std::string tag = id2string(st.get_tag());
        std::string cls_name =
          tag.substr(0, 13) == "python_class_" ? tag.substr(13) : tag;
        auto cla_it = class_level_attrs.find(cls_name);
        std::string shadow_name = "__shadow_" + attr;
        {
          // PLR §3.3.2: a method shadowed by an instance attribute.
          // `if(__shadow_m) instance.m else <nondet>` — the unshadowed
          // fallback is a sound nondet over-approximation.
          auto msa_it = method_shadow_attrs.find(cls_name);
          if(
            msa_it != method_shadow_attrs.end() &&
            msa_it->second.count(attr) > 0 && st.has_component(shadow_name))
          {
            dereference_exprt msd{value};
            member_exprt instance_v{msd, attr, st.get_component(attr).type()};
            member_exprt shadow_raw{msd, shadow_name, c_bool_typet{8}};
            typecast_exprt shadow_flag{shadow_raw, bool_typet{}};
            // Unshadowed fallback: the actual bound method (exact), else
            // a sound nondet if it cannot be boxed.
            exprt fb =
              try_box_bound_method_read(value, attr, get_location(expr));
            if(fb.is_nil() || fb.type() != instance_v.type())
              fb = side_effect_expr_nondett{
                st.get_component(attr).type(), get_location(expr)};
            return if_exprt{shadow_flag, std::move(instance_v), std::move(fb)};
          }
        }
        if(
          cla_it != class_level_attrs.end() && cla_it->second.count(attr) > 0 &&
          st.has_component(shadow_name))
        {
          // PLR §3.3.2: resolve fallback class storage via
          // MRO walk. If cls_name owns the attr, this returns
          // its own class object; if cls_name inherits the
          // attr, this returns the ancestor that owns it. The
          // distinction matters when the ancestor's class
          // storage is mutated at runtime (`Parent.attr = X`)
          // — without MRO walk the subclass's stale init-time
          // copy is read instead of the parent's updated
          // storage.
          auto mro_owner = mro_owner_class_object(cls_name, attr);
          if(mro_owner)
          {
            dereference_exprt deref{value};
            member_exprt instance_v{deref, attr, st.get_component(attr).type()};
            member_exprt shadow_raw{deref, shadow_name, c_bool_typet{8}};
            typecast_exprt shadow_flag{shadow_raw, bool_typet{}};
            member_exprt class_v{
              *mro_owner, attr, st.get_component(attr).type()};
            // §11: a bare-annotation field with no class default and
            // no definite __init__ assignment is unbound until the
            // instance writes it — reading it unshadowed is an
            // AttributeError, not a silent zero/class-storage read.
            auto ae_it = class_attrerror_fields.find(cls_name);
            if(
              ae_it != class_attrerror_fields.end() &&
              ae_it->second.count(attr))
              add_check(
                shadow_flag,
                "python-attribute-error",
                "'" + cls_name + "' object has no attribute '" + attr +
                  "' (AttributeError)",
                get_location(expr));
            return if_exprt{
              shadow_flag, std::move(instance_v), std::move(class_v)};
          }
        }
        return member_exprt{
          dereference_exprt{value}, attr, st.get_component(attr).type()};
      }
    }
  }

  // For struct types (classes), access the member directly.
  // PLR §3.3.2: an @property-decorated method is accessed
  // like a field — emit the call with self as sole arg.
  if(value.type().id() == ID_struct)
  {
    const auto &st = to_struct_type(value.type());
    std::string stag = id2string(st.get_tag());
    if(stag.substr(0, 13) == "python_class_")
    {
      // §11b: @property dispatch (own or inherited via MRO).
      exprt pg = emit_property_get(
        stag.substr(13), attr, address_of_exprt{value}, get_location(expr));
      if(!pg.is_nil())
        return pg;
      exprt dg = emit_descriptor_get(
        stag.substr(13), attr, address_of_exprt{value}, get_location(expr));
      if(!dg.is_nil())
        return dg;
    }
    if(st.has_component(attr))
    {
      // PLR §9.4: same shadow-fallback as the pointer-base
      // path above. `obj.x` on a struct-typed value: if x is
      // a class-level attribute and obj is not the class
      // object itself, route through the ternary so reads
      // see updated class storage when the instance hasn't
      // shadowed the attribute. The `obj is the class object`
      // case is detected by comparing the symbol identifier
      // to `python::<class_name>` — class-object Name reads
      // resolve to that symbol and want direct access.
      std::string tag = id2string(st.get_tag());
      std::string cls_name =
        tag.substr(0, 13) == "python_class_" ? tag.substr(13) : tag;
      auto cla_it = class_level_attrs.find(cls_name);
      std::string shadow_name = "__shadow_" + attr;
      bool is_class_object = false;
      if(value.id() == ID_symbol)
      {
        std::string sid = id2string(to_symbol_expr(value).get_identifier());
        if(sid == "python::" + cls_name)
          is_class_object = true;
      }
      if(
        !is_class_object && cla_it != class_level_attrs.end() &&
        cla_it->second.count(attr) > 0 && st.has_component(shadow_name))
      {
        // PLR §3.3.2: method shadowed by an instance attribute (struct
        // receiver) — see the pointer-receiver branch above. Placed
        // before the class-storage fallback because methods have no
        // class-object data field to fall back to (nondet instead).
        auto msa_it = method_shadow_attrs.find(cls_name);
        if(
          msa_it != method_shadow_attrs.end() && msa_it->second.count(attr) > 0)
        {
          member_exprt instance_v{value, attr, st.get_component(attr).type()};
          member_exprt shadow_raw{value, shadow_name, c_bool_typet{8}};
          typecast_exprt shadow_flag{shadow_raw, bool_typet{}};
          exprt fb = try_box_bound_method_read(value, attr, get_location(expr));
          if(fb.is_nil() || fb.type() != instance_v.type())
            fb = side_effect_expr_nondett{
              st.get_component(attr).type(), get_location(expr)};
          return if_exprt{shadow_flag, std::move(instance_v), std::move(fb)};
        }
      }
      if(
        !is_class_object && cla_it != class_level_attrs.end() &&
        cla_it->second.count(attr) > 0 && st.has_component(shadow_name))
      {
        // PLR §3.3.2: MRO walk for class-storage fallback.
        auto mro_owner = mro_owner_class_object(cls_name, attr);
        if(mro_owner)
        {
          member_exprt instance_v{value, attr, st.get_component(attr).type()};
          member_exprt shadow_raw{value, shadow_name, c_bool_typet{8}};
          typecast_exprt shadow_flag{shadow_raw, bool_typet{}};
          member_exprt class_v{*mro_owner, attr, st.get_component(attr).type()};
          // §11: see pointer-receiver branch above.
          auto ae_it = class_attrerror_fields.find(cls_name);
          if(ae_it != class_attrerror_fields.end() && ae_it->second.count(attr))
            add_check(
              shadow_flag,
              "python-attribute-error",
              "'" + cls_name + "' object has no attribute '" + attr +
                "' (AttributeError)",
              get_location(expr));
          return if_exprt{
            shadow_flag, std::move(instance_v), std::move(class_v)};
        }
      }
      // PLR §3.3.2: when value IS the class object itself
      // (e.g. `Subclass.attr` direct read), redirect to the
      // owning class via MRO if Subclass doesn't own the
      // attr. Without this, `Subclass.attr` returns the
      // stale init-time copy.
      if(
        is_class_object && cla_it != class_level_attrs.end() &&
        cla_it->second.count(attr) > 0)
      {
        auto owned_it = class_owned_attrs.find(cls_name);
        bool owns_here =
          owned_it != class_owned_attrs.end() && owned_it->second.count(attr);
        if(!owns_here)
        {
          if(auto mro_owner = mro_owner_class_object(cls_name, attr))
            return member_exprt{
              *mro_owner, attr, st.get_component(attr).type()};
        }
      }
      return member_exprt{value, attr, st.get_component(attr).type()};
    }
  }

  // Attribute accesses on an already-opaque tagged python_value
  // base are the common case for imported module symbols and
  // unannotated parameters. The only information we can return is
  // a fresh nondet, so log at debug level (or warning level if
  // --python-strict-warnings is set) rather than falling through
  // to the louder 'attribute ...' path.
  const bool base_is_module_value =
    (value.type().id() == ID_struct_tag &&
     id2string(to_struct_tag_type(value.type()).get_identifier()) ==
       std::string{PYTHON_VALUE_TAG});
  if(base_is_module_value)
  {
    // If any known user class has this attribute, dereference
    // __class_ptr through its struct type and read the field.
    // We pick the first matching class; when multiple classes
    // share the attribute name the result's type comes from
    // the first. Callers that need precision should narrow
    // with isinstance first.
    for(const auto &[cls_name, cls_type] : class_types)
    {
      if(!cls_type.has_component(attr))
        continue;
      const typet &field_type = cls_type.get_component(attr).type();
      exprt class_ptr = python_value_class_ptr(value);
      pointer_typet cls_ptr_type{cls_type, 64};
      dereference_exprt deref{
        typecast_exprt{class_ptr, cls_ptr_type}, cls_type};
      // PLR §9.4: shadow-fallback also through the tagged-
      // union read path so `o.x` for `o: python_value` bound
      // to a class instance sees updated class storage when
      // the instance hasn't shadowed.
      auto cla_it = class_level_attrs.find(cls_name);
      std::string shadow_name = "__shadow_" + attr;
      {
        // PLR §3.3.2: method shadowed by an instance attribute (tagged
        // python_value receiver) — nondet unshadowed fallback.
        auto msa_it = method_shadow_attrs.find(cls_name);
        if(
          msa_it != method_shadow_attrs.end() &&
          msa_it->second.count(attr) > 0 && cls_type.has_component(shadow_name))
        {
          member_exprt instance_v{deref, attr, field_type};
          member_exprt shadow_raw{deref, shadow_name, c_bool_typet{8}};
          typecast_exprt shadow_flag{shadow_raw, bool_typet{}};
          exprt fb = try_box_bound_method_read(deref, attr, get_location(expr));
          if(fb.is_nil() || fb.type() != instance_v.type())
            fb = side_effect_expr_nondett{field_type, get_location(expr)};
          return if_exprt{shadow_flag, std::move(instance_v), std::move(fb)};
        }
      }
      if(
        cla_it != class_level_attrs.end() && cla_it->second.count(attr) > 0 &&
        cls_type.has_component(shadow_name))
      {
        // PLR §3.3.2: MRO walk for class-storage fallback.
        auto mro_owner = mro_owner_class_object(cls_name, attr);
        if(mro_owner)
        {
          member_exprt instance_v{deref, attr, field_type};
          member_exprt shadow_raw{deref, shadow_name, c_bool_typet{8}};
          typecast_exprt shadow_flag{shadow_raw, bool_typet{}};
          member_exprt class_v{*mro_owner, attr, field_type};
          return if_exprt{
            shadow_flag, std::move(instance_v), std::move(class_v)};
        }
      }
      return member_exprt{std::move(deref), attr, field_type};
    }
    log_overapprox("attribute '" + attr + "': using nondet over-approximation");
    return side_effect_expr_nondett{python_int_type(), source_locationt{}};
  }

  // §11b: CPython calls __getattr__ when normal lookup fails. Try it
  // before over-approximating an unresolved attribute as nondet.
  {
    exprt ga = emit_getattr_fallback(value, attr, get_location(expr));
    if(!ga.is_nil())
      return ga;
  }

  // PLR §3.3.2: a bare read of a method name (not called here, not an
  // alias target) — box it as a runtime bound-method value so it can be
  // stored/passed/returned and dispatched later.
  {
    exprt bm = try_box_bound_method_read(value, attr, get_location(expr));
    if(!bm.is_nil())
      return bm;
  }

  log_overapprox("attribute '" + attr + "': using nondet over-approximation");
  return side_effect_expr_nondett{python_int_type(), source_locationt{}};
}

exprt python_convertert::try_box_bound_method_read(
  const exprt &value,
  const std::string &attr,
  const source_locationt &loc)
{
  // Normalise the receiver to a struct-typed expr + extract its class.
  exprt recv = value;
  typet rt = recv.type();
  if(rt.id() == ID_pointer)
  {
    recv = dereference_exprt{recv};
    rt = recv.type();
  }
  std::string cls_name;
  if(rt.id() == ID_struct)
    cls_name = id2string(to_struct_type(rt).get_tag());
  else if(rt.id() == ID_struct_tag)
    cls_name = id2string(to_struct_tag_type(rt).get_identifier());
  else
    return nil_exprt{};
  if(cls_name.rfind("tag-", 0) == 0)
    cls_name = cls_name.substr(4);
  if(cls_name.rfind("python_class_", 0) == 0)
    cls_name = cls_name.substr(13);

  // Resolve `attr` as a method across the MRO (own class first).
  std::vector<std::string> chain{cls_name};
  auto mro_it = class_mro.find(cls_name);
  if(mro_it != class_mro.end())
    chain = mro_it->second;
  for(const std::string &anc : chain)
  {
    // @property reads are values, not bound methods — leave them.
    auto pit = class_property_methods.find(anc);
    if(pit != class_property_methods.end() && pit->second.count(attr) > 0)
      return nil_exprt{};
    const symbolt *ms =
      symbol_table.lookup(irep_idt{"python::" + anc + "::" + attr});
    if(ms != nullptr && ms->type.id() == ID_code)
      return box_bound_method(ms->name, recv, loc);
  }
  return nil_exprt{};
}

// PLR §6.2.7: Dictionary displays
// Build a python_dict value from (key, value) pairs. Determines the
// key/value element types (promoting to the tagged union when
// heterogeneous), de-duplicates equal *constant* keys (PLR §6.4: a dict has
// at most one entry per key; the LAST value wins, matching Python's
// overwrite semantics — symbolic keys cannot be compared at conversion time
// and are kept as-is), pads to PYTHON_MAX_DICT_SIZE, and guards over-capacity.
// Shared by convert_dict (dict literals) and dict.fromkeys.
exprt python_convertert::allocate_boxed_leaf(
  const exprt &value,
  const typet &leaf_type)
{
  // Per-instance heap object (mirrors the closure capture-record allocation):
  // each execution of this construction site allocates a DISTINCT object, and
  // the container copies the pointer VALUE at construction, so values boxed in
  // a container built more than once (function return / loop) do not alias.
  pointer_typet ptr_type{leaf_type, 64};
  static unsigned box_ctr = 0;
  std::string pn = "__box_ptr_" + std::to_string(box_ctr++);
  irep_idt pid{qualify_name(pn)};
  if(symbol_table.lookup(pid) == nullptr)
  {
    symbolt ps{pid, ptr_type, "python"};
    ps.base_name = pn;
    ps.is_lvalue = true;
    ps.is_state_var = true;
    ps.is_static_lifetime = current_function.empty();
    symbol_table.add(ps);
  }
  symbol_exprt ptr = symbol_table.lookup_ref(pid).symbol_expr();
  namespacet ns{symbol_table};
  // A non-fixed-width leaf (smt_string / integer_typet) has no byte size; use
  // a fixed nonzero size so each ID_allocate yields a DISTINCT dynamic object
  // (a zero size collapses them to one, re-introducing aliasing).
  auto computed = pointer_offset_size(leaf_type, ns);
  std::size_t bytes = (computed.has_value() && *computed > 0)
                        ? numeric_cast_v<std::size_t>(*computed)
                        : 16;
  exprt size = from_integer(bytes, size_type());
  side_effect_exprt alloc{
    ID_allocate, {size, false_exprt{}}, ptr_type, source_locationt{}};
  pending_checks.push_back(code_frontend_assignt{ptr, alloc});
  pending_checks.push_back(
    code_frontend_assignt{dereference_exprt{ptr}, value});
  return std::move(ptr);
}

exprt python_convertert::box_string_for_storage(const exprt &str_value)
{
  if(
    !python_smt_string_native_flag() ||
    !is_python_string_type(str_value.type()))
    return str_value;
  return allocate_boxed_leaf(str_value, python_string_type());
}

exprt python_convertert::box_int_for_storage(const exprt &int_value)
{
  if(!unbounded_ints)
    return int_value;
  // Box the int behind a per-instance heap integer (allocate_boxed_leaf gives
  // a fresh object per execution; the dict-subscript const-fold is guarded
  // against re-reading the boxed pointer, see contains_boxed_leaf_pointer), so
  // a wrapped unbounded int keeps FULL PRECISION and does not alias across
  // instances of the same construction site.
  exprt v = int_value;
  if(v.type().id() != ID_integer)
    v = typecast_exprt{v, integer_typet{}};
  return allocate_boxed_leaf(v, integer_typet{});
}

exprt python_convertert::build_dict_value(
  std::vector<std::pair<exprt, exprt>> pairs,
  const source_locationt &loc)
{
  // De-dup equal constant keys (keep last value).
  {
    std::vector<std::pair<exprt, exprt>> deduped;
    for(const auto &p : pairs)
    {
      bool merged = false;
      // PLR §3: dedup keys by Python equality — numeric equality
      // (1 == 1.0 == True) for numeric-key constants, string VALUE equality for
      // string-literal keys (a python_string struct is not a constant_exprt, so
      // exact-expr equality below never merges them), else exact constant
      // equality. A symbolic string key has no known value -> not deduped
      // (sound: cannot prove two symbolic keys equal).
      std::optional<mp_integer> pk = python_numeric_key(p.first);
      std::optional<std::string> ps;
      if(!pk.has_value() && is_python_string_type(p.first.type()))
        ps = extract_string_value(p.first);
      if(pk.has_value())
      {
        for(auto &d : deduped)
        {
          std::optional<mp_integer> dk = python_numeric_key(d.first);
          if(dk.has_value() && *dk == *pk)
          {
            d.second = p.second; // later value overwrites
            merged = true;
            break;
          }
        }
      }
      else if(ps.has_value())
      {
        for(auto &d : deduped)
          if(
            is_python_string_type(d.first.type()) &&
            extract_string_value(d.first) == ps)
          {
            d.second = p.second; // later value overwrites
            merged = true;
            break;
          }
      }
      else if(is_python_none_constant(p.first))
      {
        // PLR §3: None is a singleton — `{None: 1, None: 2}` has one key.
        for(auto &d : deduped)
          if(is_python_none_constant(d.first))
          {
            d.second = p.second; // later value overwrites
            merged = true;
            break;
          }
      }
      else if(is_python_tuple_type(p.first.type()))
      {
        // PLR §3: structurally-identical tuple keys are equal (`(1,2)` twice).
        // Structural expr equality is sound (identical trees denote the same
        // value); a cross-type-numeric element difference (`(1,2)` vs `(1,2.0)`)
        // is not merged here (a rare residual).
        for(auto &d : deduped)
          if(is_python_tuple_type(d.first.type()) && d.first == p.first)
          {
            d.second = p.second; // later value overwrites
            merged = true;
            break;
          }
      }
      else if(p.first.is_constant())
      {
        for(auto &d : deduped)
          if(d.first.is_constant() && d.first == p.first)
          {
            d.second = p.second; // later value overwrites
            merged = true;
            break;
          }
      }
      if(!merged)
        deduped.push_back(p);
    }
    pairs = std::move(deduped);
  }

  // Determine key/value types from first pair
  typet key_type = pairs.empty() ? python_string_type() : pairs[0].first.type();
  typet val_type = pairs.empty() ? python_int_type() : pairs[0].second.type();

  // Heterogeneous-value detection: if any later value's type
  // disagrees with val_type, promote val_type to the tagged
  // union (python_value_type) so each value can be wrapped via
  // wrap_value rather than typecast through a smaller struct.
  for(std::size_t i = 1; i < pairs.size(); i++)
  {
    if(pairs[i].second.type() != val_type)
    {
      val_type = python_value_type();
      break;
    }
  }
  // Same for keys.
  for(std::size_t i = 1; i < pairs.size(); i++)
  {
    if(pairs[i].first.type() != key_type)
    {
      key_type = python_value_type();
      break;
    }
  }

  struct_typet dict_type = python_dict_type(key_type, val_type);
  const auto &keys_arr_type = to_array_type(dict_type.components()[1].type());
  const auto &vals_arr_type = to_array_type(dict_type.components()[2].type());
  // On native, string keys are boxed: the array element is a string*, so each
  // key must be materialised behind a pointer (and padding is a null string*).
  const typet &keys_elem_type = keys_arr_type.element_type();

  // Build keys array
  exprt::operandst key_elems;
  for(const auto &p : pairs)
  {
    exprt k = p.first;
    if(k.type() != key_type)
      k = is_python_value_type(key_type) ? wrap_value(k)
                                         : safe_typecast(k, key_type);
    key_elems.push_back(box_string_for_storage(k));
  }
  while(key_elems.size() < PYTHON_MAX_DICT_SIZE)
    key_elems.push_back(safe_zero(keys_elem_type));

  // Build values array
  exprt::operandst val_elems;
  for(const auto &p : pairs)
  {
    exprt v = p.second;
    if(v.type() != val_type)
      v = is_python_value_type(val_type) ? wrap_value(v)
                                         : safe_typecast(v, val_type);
    val_elems.push_back(v);
  }
  while(val_elems.size() < PYTHON_MAX_DICT_SIZE)
    val_elems.push_back(safe_zero(val_type));

  exprt length =
    from_integer(static_cast<long long>(pairs.size()), signedbv_typet{64});

  // Over-capacity dict: bounded scans miss entries beyond the cap, so
  // report python-model-bound + cut at construction.
  if(pairs.size() > static_cast<std::size_t>(PYTHON_MAX_DICT_SIZE))
  {
    emit_count_capacity_guard(
      pending_checks, length, PYTHON_MAX_DICT_SIZE, loc);
    key_elems.resize(PYTHON_MAX_DICT_SIZE);
    val_elems.resize(PYTHON_MAX_DICT_SIZE);
    length = from_integer(PYTHON_MAX_DICT_SIZE, signedbv_typet{64});
  }

  return struct_exprt{
    {length,
     array_exprt{std::move(key_elems), keys_arr_type},
     array_exprt{std::move(val_elems), vals_arr_type}},
    dict_type};
}

// "A dictionary display yields a new dictionary object."
exprt python_convertert::convert_dict(const jsont &expr)
{
  const jsont &keys = json_member(expr, "keys");
  const jsont &values = json_member(expr, "values");

  if(!keys.is_array() || !values.is_array())
    return nil_exprt{};

  // Collect key-value pairs
  std::vector<std::pair<exprt, exprt>> pairs;
  auto key_it = as_array(keys).begin();
  auto val_it = as_array(values).begin();
  for(; key_it != as_array(keys).end(); ++key_it, ++val_it)
  {
    exprt k = convert_expression(*key_it);
    // PLR §3.2: a dict key must be hashable; a list/dict/set key raises
    // TypeError ('unhashable type').
    if(!k.is_nil() && is_unhashable_type(k.type()))
      emit_conditional_exception(true_exprt{}, "TypeError");
    exprt v;
    // PLR §3.1: if the value is a Name resolving to an escaped
    // list/dict-typed symbol, wrap as `make_python_value(LIST/DICT,
    // &symbol)` — pointing to the original storage. Subsequent
    // mutations through this dict's value propagate to the symbol.
    bool wrapped_as_ref = false;
    if(is_node_type(*val_it, "Name"))
    {
      std::string vn = json_string(json_member(*val_it, "id"));
      irep_idt vq{qualify_name(vn)};
      if(escaped_mutables.count(vq) > 0)
      {
        const symbolt *vsym = symbol_table.lookup(vq);
        if(vsym != nullptr)
        {
          if(is_python_list_type(vsym->type))
          {
            v = make_python_value(
              python_type_tagt::LIST, address_of_exprt{vsym->symbol_expr()});
            wrapped_as_ref = true;
          }
          else if(is_python_dict_type(vsym->type))
          {
            v = make_python_value(
              python_type_tagt::DICT, address_of_exprt{vsym->symbol_expr()});
            wrapped_as_ref = true;
          }
        }
      }
    }
    if(!wrapped_as_ref)
      v = convert_expression(*val_it);
    if(k.is_nil() || v.is_nil())
      continue;
    pairs.emplace_back(k, v);
  }

  // Determine key/value types, dedup equal constant keys, build the struct.
  return build_dict_value(std::move(pairs), get_location(expr));
}
