/// Python to GOTO converter — FunctionDef (PLR §8.7),
/// ClassDef (§9.1), and Expr-as-statement handlers.
///
/// Extracted from python_converter.cpp to reduce the main file size.
/// All logic and class-member state remains unchanged — this file is
/// a pure source-split.

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/config.h>
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

#include <set>

// PLR §8.7: Function definitions
// Shared, un-annotated return-type inference for both free functions
// (convert_function_def) and methods (convert_class_def). Computes the
// union of detections: class-instance construction (`return ClassName()`,
// `return self`, `return varname` bound to a constructor), tuple returns,
// generator yields, `return param` for tagged-union params, and dict /
// list literal shapes. PLR §3.2 / §6.10.5 / §7.6.
python_convertert::inferred_returnt
python_convertert::infer_return_type_from_body(
  const jsont &body,
  const code_typet::parameterst &parameters,
  const std::string &qualified_name,
  const std::string &enclosing_class)
{
  inferred_returnt result;
  typet &return_type = result.type; // empty_typet{} until a type is found
  result.yield_element_type = python_int_type();
  bool has_none_return = false;
  // Eager dict / list shape tracking (every non-None value return must
  // share the shape to commit to it).
  bool has_dict = false, all_dict = true;
  bool has_list = false, all_list = true;
  typet first_dict_key, first_dict_val, first_list_elem;

  // Container-shape of a return-value expression, for inferring dict/list
  // return types beyond bare {...}/[...] literals. Closing the
  // function-return type-propagation gap: a function that returns a dict/list
  // via a CALL (dict()/list()/nondet_dict()/nondet_list()), a comprehension,
  // or a local variable bound to one was previously typed as the int default,
  // so the type-punned call result false-proved e.g. `f() == {}`. 0 = neither,
  // 1 = dict, 2 = list. `direct_kind` is non-recursive; `rv_container_kind`
  // additionally resolves a returned Name to its in-body assignment (one hop,
  // no recursion -> no infinite loop).
  auto direct_kind = [&](const jsont &e) -> int
  {
    if(is_node_type(e, "Dict") || is_node_type(e, "DictComp"))
      return 1;
    if(is_node_type(e, "List") || is_node_type(e, "ListComp"))
      return 2;
    if(is_node_type(e, "Call") && is_node_type(json_member(e, "func"), "Name"))
    {
      const std::string cn =
        json_string(json_member(json_member(e, "func"), "id"));
      if(cn == "dict" || cn == "nondet_dict")
        return 1;
      if(cn == "list" || cn == "nondet_list")
        return 2;
    }
    return 0;
  };
  auto rv_container_kind = [&](const jsont &rv) -> int
  {
    int k = direct_kind(rv);
    if(k != 0)
      return k;
    if(is_node_type(rv, "Name"))
    {
      const std::string rn = json_string(json_member(rv, "id"));
      if(body.is_array())
        for(const auto &bs : as_array(body))
        {
          if(!is_node_type(bs, "Assign"))
            continue;
          const jsont &tgts = json_member(bs, "targets");
          if(!tgts.is_array() || as_array(tgts).empty())
            continue;
          const jsont &t0 = *as_array(tgts).begin();
          if(
            is_node_type(t0, "Name") &&
            json_string(json_member(t0, "id")) == rn)
          {
            int vk = direct_kind(json_member(bs, "value"));
            if(vk != 0)
              return vk;
          }
        }
    }
    return 0;
  };

  std::function<void(const jsont &)> scan = [&](const jsont &body_node)
  {
    if(!body_node.is_array())
      return;
    for(const auto &s : as_array(body_node))
    {
      if(is_node_type(s, "Return"))
      {
        const jsont &rv_top = json_member(s, "value");
        if(!rv_top.is_null())
        {
          result.has_value_return = true;
          // A conditional-expression return (`a if cond else b`) returns `a`
          // on one path and `b` on another; flatten nested IfExp into leaf
          // return-values so each arm contributes to the inferred type. PLR
          // §3.2: otherwise `C() if c else None` is an unrecognised form that
          // defaults to int, erasing None so the None path is never explored
          // (a false-proof vector). A non-IfExp return is a single leaf.
          std::vector<const jsont *> rv_leaves;
          std::function<void(const jsont &)> collect_rv = [&](const jsont &e)
          {
            if(is_node_type(e, "IfExp"))
            {
              collect_rv(json_member(e, "body"));
              collect_rv(json_member(e, "orelse"));
            }
            else
              rv_leaves.push_back(&e);
          };
          collect_rv(rv_top);
          for(const jsont *rv_ptr : rv_leaves)
          {
            const jsont &rv = *rv_ptr;
            bool this_is_none = false;
            if(
              (is_node_type(rv, "Constant") &&
               json_member(rv, "value").is_null()) ||
              (is_node_type(rv, "Name") &&
               json_string(json_member(rv, "id")) == "None"))
            {
              has_none_return = true;
              this_is_none = true;
            }
            // `return ClassName(...)`, or `return cls(...)` in a
            // classmethod (cls constructs the enclosing class).
            if(
              is_node_type(rv, "Call") &&
              is_node_type(json_member(rv, "func"), "Name"))
            {
              std::string call_name =
                json_string(json_member(json_member(rv, "func"), "id"));
              const typet *this_type = nullptr;
              if(class_types.count(call_name))
                this_type = &class_types[call_name];
              else if(
                call_name == "cls" && !enclosing_class.empty() &&
                class_types.count(enclosing_class))
                this_type = &class_types[enclosing_class];
              if(this_type != nullptr)
              {
                if(return_type.id() == ID_empty)
                  return_type = *this_type;
                else if(return_type != *this_type)
                  return_type = python_value_type();
              }
              // `return h()` where h is another user FUNCTION → propagate
              // h's return type (PLR §3.2). Forward references are resolved
              // by the sub-pass 1b.4 fixpoint that calls this routine; here
              // we read h's CURRENT symbol return type. This prevents a
              // tail-calling function from falling to the int fallback
              // below (which mis-types e.g. a forward `return g()` whose g
              // returns a string/dict/list).
              else if(call_name != "cls")
              {
                const symbolt *cs = symbol_table.lookup("python::" + call_name);
                if(cs != nullptr && cs->type.id() == ID_code)
                {
                  const typet &rt = to_code_type(cs->type).return_type();
                  if(rt.id() != ID_empty)
                  {
                    if(return_type.id() == ID_empty)
                      return_type = rt;
                    else if(return_type != rt)
                      return_type = python_value_type();
                  }
                }
              }
            }
            // `return <constant>` — infer the literal's type directly.
            // Without this a bare constant return falls to the int
            // fallback at the end (wrong for str/float), which only the
            // later body conversion repairs — too late for a forward
            // reference reading this signature.
            if(is_node_type(rv, "Constant"))
            {
              const jsont &cv = json_member(rv, "value");
              typet ct{ID_empty};
              if(cv.is_string())
                ct = python_string_type();
              else if(cv.is_true() || cv.is_false())
                ct = python_int_type(); // bool ⊂ int
              else if(cv.is_number())
              {
                const std::string vs = cv.value;
                ct = (vs.find('.') != std::string::npos ||
                      vs.find('e') != std::string::npos ||
                      vs.find('E') != std::string::npos)
                       ? double_type()
                       : python_int_type();
              }
              if(ct.id() != ID_empty)
              {
                if(return_type.id() == ID_empty)
                  return_type = ct;
                else if(return_type != ct)
                  return_type = python_value_type();
              }
            }
            // `return self` → the enclosing class (builder pattern).
            if(
              !enclosing_class.empty() && is_node_type(rv, "Name") &&
              json_string(json_member(rv, "id")) == "self" &&
              class_types.count(enclosing_class))
            {
              typet this_type = class_types[enclosing_class];
              if(return_type.id() == ID_empty)
                return_type = this_type;
              else if(return_type != this_type)
                return_type = python_value_type();
            }
            if(is_node_type(rv, "Name"))
            {
              std::string rname = json_string(json_member(rv, "id"));
              // `return param` where param is a tagged-union (Any / no
              // annotation) parameter → python_value.
              for(const auto &p : parameters)
              {
                if(p.get_base_name() == rname)
                {
                  if(is_python_value_type(p.type()))
                  {
                    return_type = python_value_type();
                    result.saw_python_value_return = true;
                  }
                  break;
                }
              }
              // `return varname` where varname = ClassName(...) earlier
              // in the body.
              if(body.is_array())
              {
                for(const auto &bs : as_array(body))
                {
                  if(!is_node_type(bs, "Assign"))
                    continue;
                  const jsont &targets = json_member(bs, "targets");
                  if(!targets.is_array() || as_array(targets).empty())
                    continue;
                  const jsont &t0 = *as_array(targets).begin();
                  if(
                    !is_node_type(t0, "Name") ||
                    json_string(json_member(t0, "id")) != rname)
                    continue;
                  const jsont &av = json_member(bs, "value");
                  if(
                    !is_node_type(av, "Call") ||
                    !is_node_type(json_member(av, "func"), "Name"))
                    continue;
                  std::string cn =
                    json_string(json_member(json_member(av, "func"), "id"));
                  if(class_types.count(cn))
                  {
                    typet this_type = class_types[cn];
                    if(return_type.id() == ID_empty)
                      return_type = this_type;
                    else if(return_type != this_type)
                      return_type = python_value_type();
                  }
                  break;
                }
              }
            }
            // `return a, b` — infer the tuple type from the shape.
            if(is_node_type(rv, "Tuple") && return_type.id() == ID_empty)
            {
              const jsont &telts = json_member(rv, "elts");
              if(telts.is_array() && !as_array(telts).empty())
              {
                std::vector<typet> elem_types;
                for(const auto &e : as_array(telts))
                {
                  typet et = python_int_type();
                  if(is_node_type(e, "Constant"))
                  {
                    const jsont &cv = json_member(e, "value");
                    if(cv.is_string())
                      et = python_string_type();
                    else if(cv.is_number())
                    {
                      std::string vs = cv.value;
                      if(
                        vs.find('.') != std::string::npos ||
                        vs.find('e') != std::string::npos)
                        et = double_type();
                    }
                  }
                  else if(is_node_type(e, "Name"))
                  {
                    std::string nm = json_string(json_member(e, "id"));
                    irep_idt nid{"python::" + qualified_name + "::" + nm};
                    const symbolt *ns = symbol_table.lookup(nid);
                    if(ns != nullptr && ns->type.id() != ID_empty)
                      et = ns->type;
                    else
                    {
                      if(body.is_array())
                      {
                        for(const auto &bs : as_array(body))
                        {
                          if(!is_node_type(bs, "AnnAssign"))
                            continue;
                          const jsont &target = json_member(bs, "target");
                          if(
                            !is_node_type(target, "Name") ||
                            json_string(json_member(target, "id")) != nm)
                            continue;
                          const jsont &ann = json_member(bs, "annotation");
                          if(!ann.is_null())
                            et = convert_type_annotation(ann);
                          break;
                        }
                      }
                      if(et == python_int_type())
                        et = double_type();
                    }
                  }
                  elem_types.push_back(et);
                }
                return_type = python_tuple_type(elem_types);
              }
            }
            // Eager dict / list shapes (used only if no class/tuple type
            // committed above).
            const int ck = rv_container_kind(rv);
            if(ck == 1)
            {
              has_dict = true;
              if(first_dict_key.id_string().empty())
              {
                // Safe defaults for a non-literal dict return (call /
                // comprehension / local var); refined below for a literal
                // whose first entry is constant.
                first_dict_key = python_string_type();
                first_dict_val = python_int_type();
                if(is_node_type(rv, "Dict"))
                {
                  const jsont &keys = json_member(rv, "keys");
                  const jsont &values = json_member(rv, "values");
                  first_dict_val = python_value_type();
                  if(
                    keys.is_array() && values.is_array() &&
                    !as_array(keys).empty())
                  {
                    const jsont &k0 = *as_array(keys).begin();
                    const jsont &v0 = *as_array(values).begin();
                    if(is_node_type(k0, "Constant"))
                    {
                      const jsont &kcv = json_member(k0, "value");
                      if(kcv.is_number())
                        first_dict_key = python_int_type();
                      else if(kcv.is_string())
                        first_dict_key = python_string_type();
                    }
                    if(is_node_type(v0, "Constant"))
                    {
                      const jsont &vcv = json_member(v0, "value");
                      if(vcv.is_number())
                        first_dict_val =
                          vcv.value.find('.') != std::string::npos
                            ? double_type()
                            : python_int_type();
                      else if(vcv.is_string())
                        first_dict_val = python_string_type();
                      else if(vcv.is_true() || vcv.is_false())
                        first_dict_val = bool_typet{};
                    }
                  }
                }
              }
            }
            else if(!this_is_none)
              all_dict = false;
            if(ck == 2)
            {
              has_list = true;
              if(first_list_elem.id_string().empty())
              {
                first_list_elem = python_value_type();
                if(is_node_type(rv, "List"))
                {
                  const jsont &elts = json_member(rv, "elts");
                  if(elts.is_array() && !as_array(elts).empty())
                  {
                    const jsont &e0 = *as_array(elts).begin();
                    if(is_node_type(e0, "Constant"))
                    {
                      const jsont &cv = json_member(e0, "value");
                      if(cv.is_string())
                        first_list_elem = python_string_type();
                      else if(cv.is_number())
                        first_list_elem =
                          cv.value.find('.') != std::string::npos
                            ? double_type()
                            : python_int_type();
                      else if(cv.is_true() || cv.is_false())
                        first_list_elem = bool_typet{};
                    }
                  }
                }
              }
            }
            else if(!this_is_none)
              all_list = false;
          }
        }
      }
      // Generator detection.
      if(is_node_type(s, "Expr"))
      {
        const jsont &val = json_member(s, "value");
        if(is_node_type(val, "Yield") || is_node_type(val, "YieldFrom"))
        {
          result.has_yield = true;
          const jsont &yield_val = json_member(val, "value");
          if(is_node_type(yield_val, "Constant"))
          {
            const jsont &cv = json_member(yield_val, "value");
            if(cv.is_number())
            {
              if(cv.value.find('.') != std::string::npos)
                result.yield_element_type = double_type();
            }
            else if(cv.is_string())
              result.yield_element_type = python_string_type();
            else if(cv.is_true() || cv.is_false())
              result.yield_element_type = bool_typet{};
          }
        }
      }
      if(json_member(s, "body").is_array())
        scan(json_member(s, "body"));
      if(json_member(s, "orelse").is_array())
        scan(json_member(s, "orelse"));
      if(json_member(s, "handlers").is_array())
      {
        for(const auto &h : as_array(json_member(s, "handlers")))
          if(json_member(h, "body").is_array())
            scan(json_member(h, "body"));
      }
    }
  };
  scan(body);

  auto is_safe = [](const typet &t)
  {
    return t.id() == ID_signedbv || t.id() == ID_floatbv || t.id() == ID_bool ||
           is_python_string_type(t);
  };

  // Resolution (class/tuple already committed in return_type during the
  // walk; dict/list are the post-walk fall-backs).
  if(result.has_value_return && has_none_return)
    // A value return on one path and None on another → Optional[...] →
    // tagged union (python_value) so `is not None` dispatches. Covers
    // class types, scalars (e.g. `return 0` / `return None`), and
    // containers alike — must take precedence over any concrete type
    // committed during the walk (PLR §3.2).
    return_type = python_value_type();
  else if(
    return_type.id() == ID_empty && has_dict && all_dict &&
    !first_dict_key.id_string().empty() && is_safe(first_dict_key) &&
    is_safe(first_dict_val))
    return_type = python_dict_type(first_dict_key, first_dict_val);
  else if(
    return_type.id() == ID_empty && has_list && all_list &&
    !first_list_elem.id_string().empty())
    return_type = python_list_type(first_list_elem);
  else if(result.has_value_return && return_type.id() == ID_empty)
  {
    bool has_float_param = false;
    for(const auto &p : parameters)
      if(p.type().id() == ID_floatbv)
        has_float_param = true;
    return_type = has_float_param ? double_type() : python_int_type();
  }
  // !has_value_return → return_type stays empty (caller's fall-through).
  return result;
}

// PLR §6.2.9: see header. Build `__gen_result_<current_function>.data[length]
// = v; length += 1`, or an empty block if not in a generator / no result sym.
code_blockt python_convertert::build_gen_result_append(const exprt &v)
{
  code_blockt block;
  if(current_function.empty() || !generator_functions.count(current_function))
    return block;
  irep_idt gri{qualify_name("__gen_result_" + current_function)};
  const symbolt *grs = symbol_table.lookup(gri);
  if(grs == nullptr)
    return block;
  const auto &list_st = to_struct_type(grs->type);
  const auto &data_type = to_array_type(list_st.components()[1].type());
  member_exprt data{grs->symbol_expr(), "data", data_type};
  member_exprt length{grs->symbol_expr(), "length", signedbv_typet{64}};
  exprt typed_val = v;
  if(typed_val.type() != data_type.element_type())
    typed_val = coerce_element(typed_val, data_type.element_type());
  block.add(code_frontend_assignt{index_exprt{data, length}, typed_val});
  block.add(code_frontend_assignt{
    length, plus_exprt{length, from_integer(1, signedbv_typet{64})}});
  return block;
}

// PLR §6.2.9: create the eager-result list symbol `__gen_result_<name>`
// for a generator function/method and emit its initialisation
// (length = 0 + zeroed data) into \p body_block. Shared by free
// functions (convert_function_def) and methods (convert_class_def);
// \p qualified_name must equal current_function during body conversion
// so the yield-append / return rewrites resolve the same symbol.
irep_idt python_convertert::setup_generator_result(
  const std::string &qualified_name,
  const typet &list_type,
  code_blockt &body_block)
{
  std::string grn = "__gen_result_" + qualified_name;
  irep_idt gen_result_id{qualify_name(grn)};
  if(symbol_table.lookup(gen_result_id) == nullptr)
  {
    symbolt grs{gen_result_id, list_type, "python"};
    grs.base_name = grn;
    grs.is_lvalue = true;
    grs.is_state_var = true;
    symbol_table.add(grs);
  }
  body_block.add(code_frontend_assignt{
    member_exprt{
      symbol_table.lookup_ref(gen_result_id).symbol_expr(),
      "length",
      signedbv_typet{64}},
    from_integer(0, signedbv_typet{64})});
  if(list_type.id() == ID_struct)
  {
    const auto &rt = to_struct_type(list_type);
    if(rt.components().size() >= 2)
    {
      const auto &data_t = to_array_type(rt.components()[1].type());
      exprt::operandst zero_elems;
      while(zero_elems.size() < PYTHON_MAX_LIST_LENGTH)
        zero_elems.push_back(safe_zero(data_t.element_type()));
      body_block.add(code_frontend_assignt{
        member_exprt{
          symbol_table.lookup_ref(gen_result_id).symbol_expr(), "data", data_t},
        array_exprt{std::move(zero_elems), data_t}});
    }
  }
  return gen_result_id;
}

// "A function definition defines a user-defined function object."
void python_convertert::collect_def_time_default_checks(
  const jsont &args_node,
  const source_locationt &loc,
  code_blockt &out)
{
  bool raised = false;
  for(const char *grp : {"defaults", "kw_defaults"})
  {
    const jsont &lst = json_member(args_node, grp);
    if(!lst.is_array())
      continue;
    for(const auto &d : as_array(lst))
    {
      if(!d.is_object() || d.is_null())
        continue;
      // Evaluate for exception side effects only; discard the value. Save /
      // clear / restore so the caller's pending_checks is undisturbed and the
      // newly-emitted checks are captured into `out`.
      auto saved = pending_checks;
      pending_checks.clear();
      (void)convert_expression(d);
      if(!pending_checks.empty())
        raised = true;
      for(auto &chk : pending_checks)
        out.add(chk);
      pending_checks = saved;
    }
  }
  const symbolt *ea = symbol_table.lookup("python::__exception_active");
  if(raised && !python_no_exception_checks && ea != nullptr)
  {
    source_locationt eloc = loc;
    eloc.set_property_class("exception");
    eloc.set_comment("uncaught exception");
    code_assertt exc_check{not_exprt{ea->symbol_expr()}};
    exc_check.add_source_location() = eloc;
    out.add(std::move(exc_check));
  }
}

codet python_convertert::convert_function_def(const jsont &stmt)
{
  // PLR §8.7: skip @overload decorated functions (type hints only)
  const jsont &decorators = json_member(stmt, "decorator_list");
  bool is_c_intrinsic = false;
  std::string c_intrinsic_name;
  std::string c_intrinsic_fold;
  std::string c_intrinsic_domain;
  std::string c_intrinsic_range;
  int c_intrinsic_int_width = 0;
  if(decorators.is_array())
  {
    for(const auto &dec : as_array(decorators))
    {
      if(
        is_node_type(dec, "Name") &&
        json_string(json_member(dec, "id")) == "overload")
        return code_skipt{};
      // @c_intrinsic('NAME', fold='OP', domain='KIND', range='KIND',
      //              int_width=N) — route calls to the named C
      // function. Optional ``fold`` enables parse-time constant
      // folding; optional ``domain`` raises Python ValueError for
      // constant arguments that fail the named domain predicate;
      // optional ``range`` constrains the nondet return for
      // symbolic arguments; optional ``int_width`` overrides the
      // default Python-int→signedbv64 projection for C functions
      // that take/return 32-bit int.
      if(is_node_type(dec, "Call"))
      {
        const jsont &dec_func = json_member(dec, "func");
        if(
          is_node_type(dec_func, "Name") &&
          json_string(json_member(dec_func, "id")) == "c_intrinsic")
        {
          const jsont &dec_args = json_member(dec, "args");
          if(dec_args.is_array() && !as_array(dec_args).empty())
          {
            const jsont &first = *as_array(dec_args).begin();
            if(is_node_type(first, "Constant"))
            {
              c_intrinsic_name = json_string(json_member(first, "value"));
              is_c_intrinsic = !c_intrinsic_name.empty();
            }
          }
          const jsont &dec_kwargs = json_member(dec, "keywords");
          if(dec_kwargs.is_array())
          {
            for(const auto &kw : as_array(dec_kwargs))
            {
              const jsont &val = json_member(kw, "value");
              if(!is_node_type(val, "Constant"))
                continue;
              std::string arg_name = json_string(json_member(kw, "arg"));
              if(arg_name == "int_width")
              {
                // Integer literal: its stringified form lives in
                // .value as a decimal string, not in the json_string()
                // wrapper (which expects a string-typed value).
                const jsont &iv_node = json_member(val, "value");
                try
                {
                  c_intrinsic_int_width = std::stoi(iv_node.value);
                }
                catch(...)
                {
                  c_intrinsic_int_width = 0;
                }
                continue;
              }
              std::string arg_val = json_string(json_member(val, "value"));
              if(arg_name == "fold")
                c_intrinsic_fold = arg_val;
              else if(arg_name == "domain")
                c_intrinsic_domain = arg_val;
              else if(arg_name == "range")
                c_intrinsic_range = arg_val;
            }
          }
        }
      }
    }
  }

  // Phase 2 of the icontract integration plan
  // (see the Contracts section of doc/python-frontend-architecture.md): collect the
  // @icontract.require / bare @require decorators so we can
  // emit the corresponding precondition assumes at function
  // entry, and (for DFCC) attach __CPROVER_requires clauses to
  // the function type. Each entry is a pointer into the
  // decorator AST owned by `stmt`, so it remains valid for the
  // duration of convert_function_def.
  std::vector<const jsont *> icontract_require_lambdas;
  // Phase 3 (companion): collect @icontract.ensure / bare
  // @ensure decorator lambdas. Lambdas that reference `result`
  // are deferred until Phase 4 wires up the result-binding
  // mechanism — they're collected here but skipped during
  // emission below.
  std::vector<const jsont *> icontract_ensure_lambdas;
  // Phase 5: collect @icontract.snapshot decorators. Each
  // entry pairs a name (the keyword argument `name="..."`) with
  // a pointer to the capture lambda's AST. The capture
  // expression is evaluated at function entry (body
  // prologue), stored in a synthesised per-snapshot symbol,
  // and exposed to ensure lambdas via OLD.<name>.
  std::vector<std::pair<std::string, const jsont *>> icontract_snapshots;
  if(decorators.is_array())
  {
    for(const auto &dec : as_array(decorators))
    {
      if(!is_node_type(dec, "Call"))
        continue;
      const jsont &dec_func = json_member(dec, "func");
      bool is_require = false;
      bool is_ensure = false;
      bool is_snapshot = false;
      // @icontract.require / @icontract.ensure / @icontract.snapshot:
      //   Attribute(Name("icontract"), "require"|"ensure"|"snapshot")
      if(is_node_type(dec_func, "Attribute"))
      {
        std::string attr = json_string(json_member(dec_func, "attr"));
        const jsont &v = json_member(dec_func, "value");
        if(
          is_node_type(v, "Name") &&
          json_string(json_member(v, "id")) == "icontract")
        {
          if(attr == "require")
            is_require = true;
          else if(attr == "ensure")
            is_ensure = true;
          else if(attr == "snapshot")
            is_snapshot = true;
        }
      }
      // @require / @ensure / @snapshot (when imported as
      // `from icontract import require, ensure, snapshot`).
      else if(is_node_type(dec_func, "Name"))
      {
        std::string nm = json_string(json_member(dec_func, "id"));
        if(nm == "require")
          is_require = true;
        else if(nm == "ensure")
          is_ensure = true;
        else if(nm == "snapshot")
          is_snapshot = true;
      }
      if(!is_require && !is_ensure && !is_snapshot)
        continue;
      const jsont &dec_args = json_member(dec, "args");
      if(!dec_args.is_array() || as_array(dec_args).empty())
        continue;
      const jsont &first = *as_array(dec_args).begin();
      if(!is_node_type(first, "Lambda"))
        continue;
      if(is_require)
        icontract_require_lambdas.push_back(&first);
      else if(is_ensure)
        icontract_ensure_lambdas.push_back(&first);
      else
      {
        // @snapshot needs a name= keyword argument. Default
        // to empty (skip) if not provided.
        std::string snap_name;
        const jsont &dec_kws = json_member(dec, "keywords");
        if(dec_kws.is_array())
        {
          for(const auto &kw : as_array(dec_kws))
          {
            if(json_string(json_member(kw, "arg")) != "name")
              continue;
            const jsont &val = json_member(kw, "value");
            if(is_node_type(val, "Constant"))
            {
              const jsont &v = json_member(val, "value");
              if(v.is_string())
                snap_name = v.value;
            }
          }
        }
        if(snap_name.empty())
          continue;
        icontract_snapshots.emplace_back(std::move(snap_name), &first);
      }
    }
  }

  std::string func_name = json_string(json_member(stmt, "name"));
  source_locationt loc = get_location(stmt);

  // PLR §4.2.1: nested function definitions live in their
  // enclosing function's scope, not the module scope. Two
  // sibling functions can each contain `def f(...)` with
  // different bodies — they must not collide. We qualify
  // nested function names with the enclosing scope chain so
  // the symbol identifier is unique. Module-scope functions
  // keep their bare name for backwards-compat with module
  // imports and call resolution.
  std::string qualified_func_name = func_name;
  if(!current_function.empty())
    qualified_func_name = current_function + "::" + func_name;

  // Constant-directed dispatcher folding: record the pure-dispatcher /
  // forwarder shape (see dispatcher_summaryt) for literal-keyed call-site
  // folding. Module/nested functions: no self param.
  register_dispatcher_summary(
    "python::" + qualified_func_name, stmt, /*first_param_index=*/0);

  // Build parameter list
  const jsont &args_node = json_member(stmt, "args");
  const jsont &params = json_member(args_node, "args");

  code_typet::parameterst parameters;

  // PLR §8.7: positional-only parameters (before the '/' marker).
  // These are ordinary parameters from a call-site perspective; we
  // must still bind them so the function body can reference them.
  const jsont &posonlyargs = json_member(args_node, "posonlyargs");
  auto add_positional = [&](
                          const jsont &param,
                          std::size_t param_idx_in_args,
                          bool is_in_args_section)
  {
    std::string param_name = json_string(json_member(param, "arg"));
    const jsont &annotation = json_member(param, "annotation");
    if(annotation.is_null())
    {
      log.warning() << "parameter '" << param_name << "' of function '"
                    << func_name << "' has no type annotation" << messaget::eom;
    }
    typet param_type = annotation.is_null()
                         ? python_value_type()
                         : convert_type_annotation(annotation);
    // PLR §3.1: when the parameter is unannotated, look up
    // the type inferred from call-site arguments (Pass 0.28).
    // A unique inferred type across all callers is treated as
    // the effective annotation; otherwise the default
    // python_value_type stays.
    if(annotation.is_null() && is_in_args_section)
    {
      auto fn_it = inferred_param_types.find(func_name);
      if(fn_it != inferred_param_types.end())
      {
        auto p_it = fn_it->second.find(param_idx_in_args);
        if(p_it != fn_it->second.end())
          param_type = p_it->second;
      }
    }
    // Mark Optional[T] / Union[..., None] / T | None parameters
    // as nullable so the Is/IsNot fast-path doesn't lie.
    if(!annotation.is_null() && annotation_includes_none(annotation))
    {
      std::string param_id =
        "python::" + qualified_func_name + "::" + param_name;
      optional_params.insert(irep_idt{param_id});
    }
    // Phase 7 type-annotation check: if the annotation is a
    // Union[X, Y, ...], extract its component types and
    // record under the parameter's symbol id so call-site
    // checks can detect arguments that don't match any
    // component.
    if(!annotation.is_null())
    {
      // Annotation provenance: this parameter's type is a genuine source
      // annotation (sound basis for a call-boundary tag obligation), unlike
      // the default Any / a call-site-inferred type.
      explicitly_annotated_params.insert(
        irep_idt{"python::" + qualified_func_name + "::" + param_name});
      auto components = extract_union_components(annotation);
      if(!components.empty())
      {
        std::string param_id =
          "python::" + qualified_func_name + "::" + param_name;
        union_annotation_components[irep_idt{param_id}] = std::move(components);
      }
    }

    // PLR §4.2.1: Class instances are passed by reference.
    if(
      param_type.id() == ID_struct &&
      id2string(to_struct_type(param_type).get_tag()).find("python_class_") !=
        std::string::npos &&
      param_name != "self")
    {
      param_type = pointer_type(param_type);
    }

    // PLR §3.1: Mutable containers (list, dict) are also passed by
    // reference. Without this, mutations inside the function (e.g.
    // `xs.append(v)`) target a local copy and the caller's
    // container is silently unaffected, which is unsound w.r.t.
    // Python's reference semantics. The corresponding call-site
    // wraps the argument with address_of (the existing
    // 'struct arg -> pointer param' typecast path); inside the
    // body, convert_name auto-dereferences these pointer-typed
    // parameter symbols so existing member_exprt-based access
    // continues to work transparently.
    //
    // Self parameters are excluded above; varargs (*args) and
    // **kwargs are intentionally NOT wrapped here — they are
    // packed/freshly-built at the call site, so by-value vs
    // by-reference is moot, and pointer-wrapping them would
    // break the existing pack/unpack logic.
    if(
      param_name != "self" &&
      (is_python_list_type(param_type) || is_python_dict_type(param_type)))
    {
      param_type = pointer_type(param_type);
    }

    code_typet::parametert p{param_type};
    p.set_identifier("python::" + qualified_func_name + "::" + param_name);
    p.set_base_name(param_name);
    parameters.push_back(p);
  };

  if(posonlyargs.is_array())
  {
    for(const auto &param : as_array(posonlyargs))
      add_positional(param, 0, false);
  }
  if(params.is_array())
  {
    std::size_t idx = 0;
    for(const auto &param : as_array(params))
      add_positional(param, idx++, true);
  }

  // Call-site signature validation metadata. Only recorded for an
  // exact signature: an undecorated function (a decorator may wrap
  // the callable with *args/**kwargs, which would make arity/kwarg
  // checks unsound). At this point `parameters` holds exactly the
  // positional-or-keyword params (posonly + regular, incl. self),
  // before *args / kwonly / **kwargs / closure captures are added.
  bool exact_signature =
    !(decorators.is_array() && !as_array(decorators).empty());
  if(exact_signature)
  {
    irep_idt fkey{"python::" + qualified_func_name};
    function_signature_checkable.insert(fkey);
    function_max_positional[fkey] = parameters.size();
    // Required positional-or-keyword params = all of them minus the
    // trailing ones that have a default value. `defaults` lists the
    // default expressions for the last N positional params.
    const jsont &sig_defaults = json_member(args_node, "defaults");
    std::size_t n_def =
      sig_defaults.is_array() ? as_array(sig_defaults).size() : 0;
    function_required_positional[fkey] =
      parameters.size() > n_def ? parameters.size() - n_def : 0;
    // PLR §8.7: record positional-only param base names (before `/`) so a
    // call passing one by keyword can be flagged as a TypeError.
    if(posonlyargs.is_array())
      for(const auto &param : as_array(posonlyargs))
        function_posonly_params[fkey].insert(
          json_string(json_member(param, "arg")));
  }

  // PLR §8.7: keyword-only arguments are appended later (after *args),
  // since per Python's parameter ordering, kwonlyargs follow the
  // bare * or *args separator. See block below after the vararg
  // handling for the actual append; this comment documents the
  // ordering decision.

  // PLR §8.7: *args — catch-all positional argument tuple.
  // We model it as a list (our tuple model is effectively a list here).
  const jsont &vararg = json_member(args_node, "vararg");
  std::string varargs_name;
  if(!vararg.is_null())
  {
    varargs_name = json_string(json_member(vararg, "arg"));
    typet va_type = python_list_type(python_value_type());
    code_typet::parametert p{va_type};
    p.set_identifier("python::" + qualified_func_name + "::" + varargs_name);
    p.set_base_name(varargs_name);
    // PLR §8.7: record the index of the *args param so the call
    // site can locate it for packing/unpacking even when closure
    // captures are appended later.
    function_vararg_index[irep_idt{"python::" + qualified_func_name}] =
      parameters.size();
    parameters.push_back(p);
  }

  // PLR §8.7: keyword-only arguments (anything after *args or a bare *).
  {
    const jsont &kwonlyargs = json_member(args_node, "kwonlyargs");
    if(kwonlyargs.is_array())
    {
      // kw_defaults is parallel to kwonlyargs; a null entry means the
      // keyword-only arg has NO default and is therefore required.
      const jsont &kw_defaults = json_member(args_node, "kw_defaults");
      std::size_t kw_idx = 0;
      for(const auto &arg : as_array(kwonlyargs))
      {
        std::string param_name = json_string(json_member(arg, "arg"));
        if(exact_signature)
        {
          bool has_default = false;
          if(kw_defaults.is_array())
          {
            std::size_t j = 0;
            for(const auto &d : as_array(kw_defaults))
            {
              if(j == kw_idx)
              {
                has_default = !d.is_null();
                break;
              }
              ++j;
            }
          }
          if(!has_default)
            function_required_kwonly[irep_idt{"python::" + qualified_func_name}]
              .insert(param_name);
        }
        ++kw_idx;
        typet ptype;
        const jsont &annot = json_member(arg, "annotation");
        if(!annot.is_null())
          ptype = convert_type_annotation(annot);
        else
          ptype = python_value_type();
        code_typet::parametert p{ptype};
        p.set_identifier("python::" + qualified_func_name + "::" + param_name);
        p.set_base_name(param_name);
        parameters.push_back(p);
      }
    }
  }

  // PLR §8.7: **kwargs — catch-all keyword argument dict
  const jsont &kwarg = json_member(args_node, "kwarg");
  std::string kwargs_name;
  if(!kwarg.is_null())
  {
    kwargs_name = json_string(json_member(kwarg, "arg"));
    function_has_kwargs.insert(irep_idt{"python::" + qualified_func_name});
    typet kw_type = python_dict_type(python_string_type(), python_value_type());
    code_typet::parametert p{kw_type};
    p.set_identifier("python::" + qualified_func_name + "::" + kwargs_name);
    p.set_base_name(kwargs_name);
    parameters.push_back(p);
  }

  // Evaluate default parameter values at definition time (PLR §8.7)
  // Only for simple types — class instances use call-time evaluation
  {
    const jsont &defaults = json_member(args_node, "defaults");
    if(defaults.is_array() && !as_array(defaults).empty())
    {
      std::size_t n_defaults = as_array(defaults).size();
      std::size_t first_default = parameters.size() - n_defaults;
      auto def_it = as_array(defaults).begin();
      for(std::size_t i = first_default; i < parameters.size(); i++, ++def_it)
      {
        exprt val = convert_expression(*def_it);
        if(val.is_nil())
          continue;
        if(val.type().id() == ID_pointer)
          continue;
        // Skip raw struct types — those are class-instance
        // defaults whose value we don't have a stable
        // snapshot for at definition time. Built-in container
        // types (string, tuple, list, set, value, complex) are
        // returned as struct_tag values by their respective
        // constructors and ARE safe to record here.
        if(val.type().id() == ID_struct)
        {
          std::string tag = id2string(to_struct_type(val.type()).get_tag());
          if(
            tag != "python_string" && tag != "python_tuple" &&
            tag != "python_value" && tag != "python_set" &&
            tag != "python_complex" && tag != "python_list")
            continue;
        }
        default_values[{func_name, i}] = val;
      }
    }
  }
  // PLR §8.7: keyword-only parameter defaults are stored separately
  // in `kw_defaults` (positional defaults are in `defaults`). Each
  // entry is either an expression (default value) or null
  // (parameter is required). Match each entry to the kwonlyargs
  // parameter at the same index — kwonlyargs are appended to
  // `parameters` after the regular params and any *args slot, in
  // the order they appear.
  {
    const jsont &kwonly = json_member(args_node, "kwonlyargs");
    const jsont &kw_defaults = json_member(args_node, "kw_defaults");
    if(
      kwonly.is_array() && kw_defaults.is_array() &&
      as_array(kwonly).size() == as_array(kw_defaults).size())
    {
      // Find the first index of kwonlyargs in `parameters`. The
      // kwonlyargs were appended just after *args (or after the
      // regular params if no *args). We can scan parameters by
      // identifier-prefix to locate them.
      std::size_t kwonly_count = as_array(kwonly).size();
      std::size_t kwonly_start = parameters.size() - kwonly_count;
      auto kw_it = as_array(kwonly).begin();
      auto def_it = as_array(kw_defaults).begin();
      for(std::size_t k = 0; k < kwonly_count; k++, ++kw_it, ++def_it)
      {
        if(def_it->is_null())
          continue; // required kwonly param, no default
        exprt val = convert_expression(*def_it);
        if(
          !val.is_nil() && val.type().id() != ID_struct &&
          val.type().id() != ID_pointer)
          default_values[{func_name, kwonly_start + k}] = val;
      }
    }
  }

  // Return type
  bool has_yield = false;
  const jsont &returns = json_member(stmt, "returns");
  typet return_type = empty_typet{};
  if(!returns.is_null())
  {
    return_type = convert_type_annotation(returns);
    // Only register for missing-return checks when the
    // declared return type is NOT None. A function annotated
    // '-> None' legitimately falls through without returning
    // a value, and the implicit None return matches the
    // declared type. Detect None by inspecting the AST node
    // (Constant with value=None) directly so we don't have
    // to rely on the post-conversion typet which is now
    // python_int_type for None.
    bool is_none_annotation = false;
    if(is_node_type(returns, "Constant"))
    {
      const jsont &v = json_member(returns, "value");
      if(v.is_null())
        is_none_annotation = true;
    }
    if(!is_none_annotation)
      annotated_return_functions.insert(qualified_func_name);
    // If the annotation is a concrete (non-python_value) type but some return
    // path GENUINELY yields a python_value VALUE (`return <union/Any param>`),
    // WIDEN the return slot to python_value. Otherwise that path's python_value
    // is punned into the concrete slot, corrupting the value read on *other*
    // paths too (`def g(x:"int|str")->int: if isinstance(x,str): return len(x);
    // return x` mis-evaluated g("abc")). Gated on `saw_python_value_return`, NOT
    // merely `is_python_value_type(inf.type)`: the latter is also set when the
    // inferer is UNCERTAIN about an unresolved forward/recursive call (a
    // mutually-recursive `-> bool` must NOT widen). The annotation stays a hint;
    // soundness is preserved (a returned value keeps its tag, so use-site tag
    // obligations still fire on a misuse).
    if(!is_python_value_type(return_type))
    {
      inferred_returnt inf = infer_return_type_from_body(
        json_member(stmt, "body"), parameters, qualified_func_name, "");
      if(inf.saw_python_value_return)
        return_type = python_value_type();
    }
  }
  else
  {
    // No return annotation — infer from the body via the shared
    // scanner (also used for methods in convert_class_def).
    inferred_returnt inf = infer_return_type_from_body(
      json_member(stmt, "body"), parameters, qualified_func_name, "");
    has_yield = inf.has_yield;
    if(inf.has_yield)
      return_type = python_list_type(inf.yield_element_type);
    else if(inf.type.id() != ID_empty)
      return_type = inf.type;
    else
      // PLR §7.6: no value-returning return falls off the end and
      // returns None; model the implicit None as python_value{NONE}.
      return_type = python_value_type();
  }

  code_typet func_type{parameters, return_type};

  // Add closure captures as extra parameters
  irep_idt func_qid{"python::" + qualified_func_name};
  auto cap_it = closure_captures.find(id2string(func_qid));
  if(cap_it != closure_captures.end())
  {
    for(const auto &[outer_id, name, type] : cap_it->second)
    {
      code_typet::parametert p{type};
      std::string cap_param_id = "python::" + qualified_func_name + "::" + name;
      p.set_identifier(cap_param_id);
      p.set_base_name(name);
      parameters.push_back(p);
    }
    func_type = code_typet{parameters, return_type};
  }

  if(has_yield)
    generator_functions.insert(qualified_func_name);

  // Create function symbol BEFORE converting the body
  // (so recursive calls can find it)
  irep_idt symbol_id{"python::" + qualified_func_name};

  if(symbol_table.lookup(symbol_id) == nullptr)
  {
    symbolt func_symbol{symbol_id, func_type, "python"};
    func_symbol.base_name = func_name;
    func_symbol.location = loc;
    func_symbol.is_lvalue = true;
    symbol_table.add(func_symbol);
  }
  else
  {
    // Update pre-registered placeholder with real signature
    symbol_table.get_writeable_ref(symbol_id).type = func_type;
  }

  // Record @c_intrinsic mapping so convert_call can redirect to
  // the named C function instead of executing the Python body.
  if(is_c_intrinsic)
  {
    c_intrinsic_map[symbol_id] = c_intrinsic_name;
    if(!c_intrinsic_fold.empty())
      c_intrinsic_fold_map[symbol_id] = c_intrinsic_fold;
    if(!c_intrinsic_domain.empty())
      c_intrinsic_domain_map[symbol_id] = c_intrinsic_domain;
    if(!c_intrinsic_range.empty())
      c_intrinsic_range_map[symbol_id] = c_intrinsic_range;
    if(c_intrinsic_int_width == 32 || c_intrinsic_int_width == 64)
      c_intrinsic_int_width_map[symbol_id] = c_intrinsic_int_width;
  }

  // Create parameter symbols
  for(const auto &p : parameters)
  {
    symbolt param_symbol{p.get_identifier(), p.type(), "python"};
    param_symbol.base_name = p.get_base_name();
    param_symbol.location = loc;
    param_symbol.is_lvalue = true;
    param_symbol.is_state_var = true;
    param_symbol.is_parameter = true;
    if(symbol_table.lookup(param_symbol.name) == nullptr)
      symbol_table.add(param_symbol);
    else
    {
      // The module pre-scan may have created this symbol with a
      // different type (e.g. struct list instead of pointer-to-list).
      // Update it to match the function's actual parameter type.
      symbolt &existing = symbol_table.get_writeable_ref(param_symbol.name);
      if(existing.type != param_symbol.type)
        existing.type = param_symbol.type;
    }
  }

  // Convert function body
  std::string saved_function = current_function;
  auto saved_globals = global_names;
  if(!current_function.empty())
    enclosing_functions.push_back(current_function);
  current_function = qualified_func_name;
  global_names.clear();
  nonlocal_names.clear();

  // §12b: pre-scan the body for locally-assigned names so a read
  // before the binding can be reported as UnboundLocalError. Drop
  // parameters (always bound on entry) and global/nonlocal names.
  auto saved_locals = current_function_locals;
  auto saved_bit_locals = current_function_bit_locals;
  {
    std::set<std::string> assigned, excluded, non_plain;
    collect_assigned_locals(
      json_member(stmt, "body"), assigned, excluded, non_plain);
    for(const std::string &p : collect_param_names(stmt))
      excluded.insert(p);
    current_function_locals.clear();
    current_function_bit_locals.clear();
    for(const std::string &a : assigned)
      if(excluded.count(a) == 0)
      {
        current_function_locals.insert(a);
        if(non_plain.count(a) == 0)
          current_function_bit_locals.insert(a);
      }
  }

  // Phase 4 of the icontract integration plan: create the
  // `result` symbol that lambda bodies can reference to mean
  // the function's return value. Adding the symbol to the
  // function's scope lets convert_expression resolve
  // Name("result") to it during ensure-lambda translation
  // below. The post-process walk on body_block (further
  // down) assigns the actual return value to this symbol
  // before each assertion fires, so the postcondition sees
  // the right value. For void-like return types we still
  // create the symbol with python_int_type as a placeholder
  // — those ensures shouldn't reference result anyway.
  irep_idt result_symbol_id;
  if(!icontract_ensure_lambdas.empty())
  {
    typet rsym_type =
      return_type.id() == ID_empty ? python_int_type() : return_type;
    std::string rsym_name = "python::" + qualified_func_name + "::result";
    result_symbol_id = irep_idt{rsym_name};
    if(!symbol_table.has_symbol(result_symbol_id))
    {
      symbolt rsym{result_symbol_id, rsym_type, "python"};
      rsym.base_name = "result";
      rsym.is_lvalue = true;
      rsym.is_state_var = true;
      symbol_table.add(rsym);
    }
  }

  // Phase 5 of the icontract integration plan: translate
  // @snapshot decorators. For each @snapshot(lambda x:
  // expr_x, name="N") we:
  //   1. Translate expr_x to an exprt in the function's
  //      scope (so it can reference function parameters).
  //   2. Create a per-snapshot symbol
  //      python::FUNC::__icontract_old_N of the captured
  //      expression's type.
  //   3. Save the snapshot symbol's expression in
  //      active_old_snapshots[N] so that ensure-lambda
  //      translation below sees Name("OLD"), Attribute "N"
  //      and substitutes the snapshot symbol expression
  //      (handled in convert_attribute).
  //   4. Build the "capture at entry" assignments — these
  //      are pushed onto active_snapshot_assigns and
  //      injected into body_block as the first executable
  //      statements (after the require assumes) further
  //      below.
  std::map<std::string, exprt> saved_old_snapshots;
  saved_old_snapshots.swap(active_old_snapshots);
  std::vector<code_frontend_assignt> snapshot_assigns;
  for(const auto &snap : icontract_snapshots)
  {
    const std::string &name = snap.first;
    const jsont &lam = *snap.second;
    const jsont &lam_body = json_member(lam, "body");
    exprt cap;
    try
    {
      cap = convert_expression(lam_body);
    }
    catch(...)
    {
      cap = nil_exprt{};
    }
    if(cap.is_nil() || cap.type().id() == ID_empty)
      continue;
    std::string old_id_str =
      "python::" + qualified_func_name + "::__icontract_old_" + name;
    irep_idt old_id{old_id_str};
    if(!symbol_table.has_symbol(old_id))
    {
      symbolt old_sym{old_id, cap.type(), "python"};
      old_sym.base_name = "__icontract_old_" + name;
      old_sym.is_lvalue = true;
      old_sym.is_state_var = true;
      symbol_table.add(old_sym);
    }
    const symbolt &old_sym = symbol_table.lookup_ref(old_id);
    snapshot_assigns.push_back(
      code_frontend_assignt{old_sym.symbol_expr(), cap});
    active_old_snapshots[name] = old_sym.symbol_expr();
  }

  // Phase 3 of the icontract integration plan: translate the
  // @ensure lambdas now that current_function is set so Name
  // lookups in the lambda body resolve to function parameters
  // (Phase 3) and to the `result` symbol just registered above
  // (Phase 4). Successfully translated postconditions get
  // pushed onto active_ensures so the post-process walk can
  // emit the assertion before each return statement, and onto
  // the ID_C_spec_ensures slot on the function type for DFCC.
  std::vector<exprt> saved_ensures;
  saved_ensures.swap(active_ensures);
  for(const jsont *lam : icontract_ensure_lambdas)
  {
    const jsont &lam_body = json_member(*lam, "body");
    exprt cond;
    try
    {
      cond = convert_expression(lam_body);
    }
    catch(...)
    {
      cond = nil_exprt{};
    }
    if(cond.is_nil() || cond.type().id() == ID_empty)
      continue;
    if(cond.type().id() != ID_bool)
      cond = typecast_exprt{cond, bool_typet{}};
    active_ensures.push_back(cond);
    if(symbol_table.has_symbol(symbol_id))
    {
      typet &t = symbol_table.get_writeable_ref(symbol_id).type;
      static_cast<exprt &>(t.add(ID_C_spec_ensures)).operands().push_back(cond);
    }
  }

  // Pre-scan the body to collect parameter attribute uses.
  // Used by --python-check-any-arg-attrs at call sites to detect
  // Any-erasure bugs (cross-function flow where the caller has a
  // concrete class type for an arg whose corresponding parameter
  // is `Any`-typed).
  if(python_check_any_arg_attrs)
  {
    std::set<std::string> param_names;
    for(const auto &p : parameters)
    {
      // Only Any-typed parameters benefit from this analysis;
      // typed parameters already get dispatched correctly.
      if(is_python_value_type(p.type()))
        param_names.insert(std::string{id2string(p.get_base_name())});
    }
    if(!param_names.empty())
    {
      std::map<std::string, std::set<std::string>> per_param;
      std::map<std::string, std::map<std::string, std::set<std::string>>>
        per_param_gates;
      const jsont &body_for_scan = json_member(stmt, "body");
      collect_param_attribute_uses(
        body_for_scan, param_names, per_param, &per_param_gates);
      for(const auto &kv : per_param)
      {
        irep_idt key{"python::" + qualified_func_name + "::" + kv.first};
        function_param_attr_uses[key] = kv.second;
      }
      for(const auto &kv : per_param_gates)
      {
        irep_idt key{"python::" + qualified_func_name + "::" + kv.first};
        function_param_attr_gates[key] = kv.second;
      }
    }
  }

  // For generator functions, create __gen_result list
  code_blockt body_block;

  // PLR §6.2.9: generators return an eager result list.
  bool is_generator = generator_functions.count(qualified_func_name) > 0;
  irep_idt gen_result_id;
  if(is_generator)
    gen_result_id =
      setup_generator_result(qualified_func_name, return_type, body_block);

  // Phase 2 of the icontract integration plan: lower each
  // @require lambda to (a) a __CPROVER_assume at the start of
  // the function body so the body is verified under the
  // precondition, and (b) a __CPROVER_requires clause on the
  // function type so DFCC can use it for caller-side checking.
  // We invoke this AFTER param symbols are added (so Name
  // lookups in the lambda body resolve to function params) and
  // AFTER current_function is set to qualified_func_name, but
  // BEFORE the body conversion that consumes them. The
  // translation gracefully degrades to a skip when the lambda
  // body can't be converted (e.g. references a free variable
  // not in the function's signature).
  for(const jsont *lam : icontract_require_lambdas)
  {
    const jsont &lam_body = json_member(*lam, "body");
    exprt cond;
    try
    {
      cond = convert_expression(lam_body);
    }
    catch(...)
    {
      cond = nil_exprt{};
    }
    if(cond.is_nil() || cond.type().id() == ID_empty)
      continue;
    if(cond.type().id() != ID_bool)
      cond = typecast_exprt{cond, bool_typet{}};
    body_block.add(code_assumet{cond});
    // Attach to the function type for DFCC compatibility.
    if(symbol_table.has_symbol(symbol_id))
    {
      typet &t = symbol_table.get_writeable_ref(symbol_id).type;
      static_cast<exprt &>(t.add(ID_C_spec_requires))
        .operands()
        .push_back(cond);
    }
  }

  // Phase 5 of the icontract integration plan: emit the
  // @snapshot capture assignments at function entry, after
  // the precondition assumes (so the captures see the
  // post-assume state — which equals the pre-state under
  // the precondition).
  for(auto &assign : snapshot_assigns)
    body_block.add(std::move(assign));

  const jsont &body = json_member(stmt, "body");

  // Detect closures: scan for nested FunctionDefs that reference
  // our parameters or local variables (free variables).
  if(body.is_array())
  {
    // Collect our parameters
    std::set<std::string> our_params;
    for(const auto &p : parameters)
      our_params.insert(std::string{id2string(p.get_base_name())});

    // Also collect local variable names (assigned in our body)
    // These are Name targets in Assign/AnnAssign statements
    for(const auto &s : as_array(body))
    {
      if(is_node_type(s, "AnnAssign"))
      {
        const jsont &tgt = json_member(s, "target");
        if(is_node_type(tgt, "Name"))
          our_params.insert(json_string(json_member(tgt, "id")));
      }
      else if(is_node_type(s, "Assign"))
      {
        const jsont &tgts = json_member(s, "targets");
        if(tgts.is_array())
        {
          for(const auto &t : as_array(tgts))
          {
            if(is_node_type(t, "Name"))
              our_params.insert(json_string(json_member(t, "id")));
          }
        }
      }
    }

    for(const auto &s : as_array(body))
    {
      if(is_node_type(s, "FunctionDef") || is_node_type(s, "AsyncFunctionDef"))
      {
        std::string nested_bare = json_string(json_member(s, "name"));
        // PLR §4.2.1: nested function symbol ids are qualified
        // by the enclosing scope chain (see top of
        // convert_function_def). Mirror that when keying the
        // closure_captures map so the inner conversion can find
        // its captures.
        std::string nested_name = current_function + "::" + nested_bare;
        std::set<std::string> nested_params = collect_param_names(s);
        // Collect names declared 'nonlocal' or 'global' inside the
        // nested function — these must not be captured by value;
        // they resolve through qualify_name's nonlocal/global
        // redirect directly to the enclosing/module scope.
        std::set<std::string> nested_nonlocal_global;
        const jsont &nbody = json_member(s, "body");
        if(nbody.is_array())
        {
          for(const auto &ns : as_array(nbody))
          {
            if(is_node_type(ns, "Nonlocal") || is_node_type(ns, "Global"))
            {
              const jsont &nnames = json_member(ns, "names");
              if(nnames.is_array())
              {
                for(const auto &n : as_array(nnames))
                  if(n.is_string())
                    nested_nonlocal_global.insert(n.value);
              }
            }
          }
        }
        std::set<std::string> refs;
        collect_name_refs(json_member(s, "body"), refs);
        std::vector<std::tuple<std::string, std::string, typet>> captures;
        for(const auto &ref : refs)
        {
          if(nested_params.count(ref) || !our_params.count(ref))
            continue;
          if(nested_nonlocal_global.count(ref))
            continue; // handled by qualify_name redirect at use site
          // Find the variable's symbol and type
          std::string var_id = "python::" + qualified_func_name + "::" + ref;
          const symbolt *var_sym = symbol_table.lookup(irep_idt{var_id});
          if(var_sym == nullptr)
          {
            // Try as a parameter
            bool found = false;
            for(const auto &p : parameters)
            {
              if(id2string(p.get_base_name()) == ref)
              {
                captures.push_back(
                  {id2string(p.get_identifier()), ref, p.type()});
                found = true;
                break;
              }
            }
            // Local variable not yet in symbol table — create it
            if(!found)
            {
              typet var_type = python_int_type();
              // Try to infer type from AnnAssign annotation
              for(const auto &bs : as_array(body))
              {
                if(is_node_type(bs, "AnnAssign"))
                {
                  const jsont &tgt = json_member(bs, "target");
                  if(
                    is_node_type(tgt, "Name") &&
                    json_string(json_member(tgt, "id")) == ref)
                  {
                    var_type =
                      convert_type_annotation(json_member(bs, "annotation"));
                    break;
                  }
                }
              }
              // Create the symbol so it exists for the nested function
              if(symbol_table.lookup(irep_idt{var_id}) == nullptr)
              {
                symbolt new_sym{irep_idt{var_id}, var_type, "python"};
                new_sym.base_name = ref;
                new_sym.is_lvalue = true;
                new_sym.is_state_var = true;
                symbol_table.add(new_sym);
              }
              captures.push_back({var_id, ref, var_type});
            }
          }
          else
          {
            captures.push_back({var_id, ref, var_sym->type});
          }
        }
        // Closure cell substrate (PLR §4.2.2), mutating slice. A
        // NONLOCAL variable the nested function mutates, when the nested
        // function ESCAPES (is returned), cannot use the qualify_name
        // redirect to the shared enclosing symbol: across multiple
        // factory invocations that single symbol would alias, falsely
        // proving independent closures equal. Instead box such a
        // variable in a per-invocation HEAP CELL — the nested function
        // captures the cell POINTER, and the cell is allocated and
        // initialised from the enclosing value at the def site.
        //
        // Sound gating: apply only when the cell-var is assigned in the
        // enclosing body BEFORE the nested def and NOT reassigned at or
        // after it (so the def-site value equals the late-binding
        // value). Everything else falls back to the existing sound
        // nonlocal path (which over-approximates to nondet).
        if(!nested_nonlocal_global.empty())
        {
          std::function<bool(const jsont &)> returns_name =
            [&](const jsont &n) -> bool
          {
            if(n.is_array())
            {
              for(const auto &e : as_array(n))
                if(returns_name(e))
                  return true;
              return false;
            }
            if(!n.is_object())
              return false;
            if(is_node_type(n, "Return"))
            {
              const jsont &rv = json_member(n, "value");
              if(
                is_node_type(rv, "Name") &&
                json_string(json_member(rv, "id")) == nested_bare)
                return true;
            }
            static const char *fs[] = {
              "body", "orelse", "finalbody", "handlers", nullptr};
            for(const char **f = fs; *f; ++f)
            {
              const jsont &c = n[*f];
              if(!c.is_null() && returns_name(c))
                return true;
            }
            return false;
          };
          if(returns_name(body))
          {
            const auto &barr = as_array(body);
            std::size_t def_idx = barr.size();
            {
              std::size_t k = 0;
              for(const auto &bs : barr)
              {
                if(
                  (is_node_type(bs, "FunctionDef") ||
                   is_node_type(bs, "AsyncFunctionDef")) &&
                  json_string(json_member(bs, "name")) == nested_bare)
                {
                  def_idx = k;
                  break;
                }
                ++k;
              }
            }
            auto assigns_name =
              [&](const jsont &st, const std::string &nm) -> bool
            {
              std::set<std::string> a, ex, np;
              collect_assigned_locals(st, a, ex, np);
              return a.count(nm) > 0;
            };
            for(const auto &cv : nested_nonlocal_global)
            {
              if(our_params.count(cv) == 0)
                continue;
              bool before = false, after = false;
              std::size_t k = 0;
              for(const auto &bs : barr)
              {
                if(assigns_name(bs, cv))
                {
                  if(k < def_idx)
                    before = true;
                  else
                    after = true;
                }
                ++k;
              }
              if(!before || after)
                continue;
              typet valtype = python_int_type();
              const symbolt *cvsym = symbol_table.lookup(
                irep_idt{"python::" + current_function + "::" + cv});
              if(cvsym != nullptr && cvsym->type.id() != ID_code)
                valtype = cvsym->type;
              pointer_typet cellptr{valtype, 64};
              std::string cell_src =
                "python::" + current_function + "::__cell_" + cv;
              if(symbol_table.lookup(irep_idt{cell_src}) == nullptr)
              {
                symbolt cs{irep_idt{cell_src}, cellptr, "python"};
                cs.base_name = "__cell_" + cv;
                cs.is_lvalue = true;
                cs.is_state_var = true;
                symbol_table.add(cs);
              }
              captures.push_back({cell_src, cv, cellptr});
              function_cell_capture_names["python::" + nested_name].insert(cv);
              nested_cell_allocs["python::" + nested_name].push_back(
                {cv, valtype});
            }
          }
        }
        if(!captures.empty())
          closure_captures["python::" + nested_name] = captures;
      }
    }
  }

  if(body.is_array())
  {
    // PLR §3.1: pre-scan to identify names that escape into a
    // container literal (or list-mutating method). This must run
    // BEFORE convert_statement(s) so that the literal-construction
    // path knows to wrap the escaped Names with python_value
    // pointers rather than struct-copying them.
    collect_escaped_mutables(body);
    collect_empty_list_inferred_types(body);
    // §12b: declare each plain-Assign local's runtime is-bound flag and
    // initialise it to false at function entry. It is set true after
    // the binding statement (convert_statement) and asserted at each
    // read (convert_name), so a read on a path that didn't assign it is
    // detected as UnboundLocalError.
    for(const std::string &nm : current_function_bit_locals)
    {
      irep_idt bid{qualify_name(nm) + "$bound"};
      if(symbol_table.lookup(bid) == nullptr)
      {
        symbolt bs{bid, bool_typet{}, "python"};
        bs.base_name = nm + "$bound";
        bs.is_lvalue = true;
        bs.is_state_var = true;
        symbol_table.add(bs);
      }
      body_block.add(code_frontend_assignt{
        symbol_table.lookup_ref(bid).symbol_expr(), false_exprt{}});
    }
    for(const auto &s : as_array(body))
      body_block.add(convert_statement(s));
  }

  // Phase 3 + 4 of the icontract integration plan: walk the
  // body recursively and replace each code_frontend_returnt
  // with:
  //   { result = X; assert(ensures); return X; }
  // so the postcondition assertions see the return value
  // bound to the `result` symbol. For bare returns (no
  // value) we just prepend the assertions. This catches all
  // return statements no matter how deeply nested (inside
  // if / for / while / try blocks). The implicit fall-through
  // return below is handled separately.
  if(!active_ensures.empty())
  {
    std::function<void(codet &)> inject_at_returns = [&](codet &c) -> void
    {
      // Walk operands; if an operand is a code_frontend_returnt,
      // replace it with a code_blockt(assign-result + asserts + return).
      // Otherwise recurse into operands that are codet.
      for(auto &op : c.operands())
      {
        if(op.id() != ID_code)
          continue;
        codet &inner = static_cast<codet &>(op);
        if(inner.get_statement() == ID_return)
        {
          const code_frontend_returnt &ret =
            static_cast<const code_frontend_returnt &>(inner);
          code_blockt blk;
          // Assign return value to `result` symbol so postcondition
          // assertions can reference it. Skip for bare returns
          // (no return value) and when the result symbol wasn't
          // registered (no ensure decorator collected one).
          if(
            ret.has_return_value() && !result_symbol_id.empty() &&
            symbol_table.has_symbol(result_symbol_id))
          {
            const symbolt &rsym = symbol_table.lookup_ref(result_symbol_id);
            exprt rv = ret.return_value();
            if(rv.type() != rsym.type)
              rv = safe_typecast(rv, rsym.type);
            blk.add(code_frontend_assignt{rsym.symbol_expr(), rv});
          }
          for(const exprt &cond : active_ensures)
            blk.add(code_assertt{cond});
          blk.add(static_cast<const codet &>(inner));
          op = std::move(blk);
        }
        else
        {
          inject_at_returns(inner);
        }
      }
    };
    inject_at_returns(body_block);
  }

  current_function = saved_function;
  global_names = saved_globals;
  current_function_locals = saved_locals;
  current_function_bit_locals = saved_bit_locals;
  if(
    !enclosing_functions.empty() &&
    enclosing_functions.back() == saved_function)
    enclosing_functions.pop_back();

  // PLR §7.6: if function doesn't end with return, append return
  // For generators: return __gen_result list
  if(is_generator)
  {
    // Phase 3 icontract: assert ensures before the implicit
    // generator-result return.
    for(const exprt &cond : active_ensures)
      body_block.add(code_assertt{cond});
    body_block.add(code_frontend_returnt{
      symbol_table.lookup_ref(gen_result_id).symbol_expr()});
  }
  else if(return_type.id() != ID_empty)
  {
    // Re-read the function's return type from the symbol
    // table; convert_return may have widened it during body
    // conversion (e.g. annotated `-> int` widened to
    // python_value when the body returns a tagged-union
    // value). Without this, the implicit fall-through
    // None-encoded as a python_int sentinel gets silently
    // typecast to python_value at goto-conversion, emitting
    // a "warning: ignoring typecast" and leaving the
    // exception-active early-exit path with a wrong-typed
    // return value.
    const symbolt *cur_func_sym =
      symbol_table.lookup(irep_idt{"python::" + qualified_func_name});
    if(cur_func_sym != nullptr && cur_func_sym->type.id() == ID_code)
    {
      const typet &updated_rt = to_code_type(cur_func_sym->type).return_type();
      if(updated_rt.id() != ID_empty)
        return_type = updated_rt;
    }
    exprt none_expr;
    if(is_python_value_type(return_type))
      none_expr = python_none_value();
    else
    {
      mp_integer none_val = python_none_sentinel_int();
      none_expr =
        safe_typecast(from_integer(none_val, python_int_type()), return_type);
    }
    // Phase 3 icontract: assert ensures before the implicit
    // None return.
    for(const exprt &cond : active_ensures)
      body_block.add(code_assertt{cond});
    // --python-missing-return-check: when this function has
    // an explicit non-None return-type annotation but a
    // control-flow path reaches the implicit fall-through,
    // emit a missing-return property. Reachability of this
    // assertion implies the path didn't execute a `return X`
    // — which is a Python bug since the implicit None return
    // violates the declared return type.
    //
    // The check is gated on annotated_return_functions to
    // avoid firing for functions with no annotation (where
    // the inferred-int-return is just our default).
    if(
      python_missing_return_check &&
      annotated_return_functions.count(qualified_func_name) > 0)
    {
      source_locationt mr_loc = loc;
      mr_loc.set_property_class("missing-return");
      mr_loc.set_comment(
        "function '" + qualified_func_name +
        "' has annotated return type but a path reaches the "
        "implicit fall-through without returning a value");
      code_assertt mr{false_exprt{}};
      mr.add_source_location() = mr_loc;
      body_block.add(std::move(mr));
    }
    body_block.add(code_frontend_returnt{none_expr});
  }
  else
  {
    // Empty return type (void-like). Still emit ensures
    // assertions before the implicit fall-through.
    for(const exprt &cond : active_ensures)
      body_block.add(code_assertt{cond});
  }

  // Update the symbol with the body
  symbolt *sym_ptr = symbol_table.get_writeable(symbol_id);
  if(sym_ptr != nullptr)
    sym_ptr->value = body_block;

  // Restore the parent function's icontract ensures (Phase 3 of
  // the integration plan). Done AFTER body assembly because the
  // implicit-return injection above reads from active_ensures.
  active_ensures.swap(saved_ensures);
  active_old_snapshots.swap(saved_old_snapshots);

  // PLR §8.7: if the AST body is a single \`return <constant>\`, record
  // the constant for downstream propagation. Only the simplest shape
  // qualifies — single statement, Return AST node, value is a Constant
  // node (or UnaryOp(USub, Constant) for negative literals). This
  // covers the \`def f() -> int: return 97\` pattern where every call
  // returns the same value, and lets \`c = f()\` look up f's constant
  // through try_eval_double for further folding (chr(c), len(...) on
  // a constant-length list, math.X on a constant double, etc.).
  if(body.is_array() && as_array(body).size() == 1)
  {
    const jsont &only_stmt = *as_array(body).begin();
    if(is_node_type(only_stmt, "Return"))
    {
      const jsont &val = json_member(only_stmt, "value");
      if(!val.is_null())
      {
        // convert_expression would normally honour the surrounding
        // function scope; we're past current_function = saved here so
        // any reference to a parameter would mis-resolve. Restrict to
        // pure-constant shapes.
        bool is_const = is_node_type(val, "Constant") ||
                        (is_node_type(val, "UnaryOp") &&
                         is_node_type(json_member(val, "operand"), "Constant"));
        if(is_const)
        {
          exprt val_expr = convert_expression(val);
          auto evd = try_eval_double(val_expr);
          if(evd.has_value())
            function_return_constants[symbol_id] = evd.value();
        }
      }
    }
  }

  // Function definitions don't produce executable code at the call site
  // UNLESS the function has decorators (PLR §8.7).
  //
  // PLR §8.7: "A function definition may be wrapped by one or more
  // decorator expressions. (...) The result is then bound to the
  // function name instead of the function object."
  //
  // @dec
  // def f(x): ...
  //
  // is equivalent to:
  //
  // def f(x): ...
  // f = dec(f)
  //
  // For multiple decorators they are applied bottom-up:
  // @dec1 @dec2 def f(): ... → f = dec1(dec2(f))
  //
  // We emit the decorator call(s) as pending_checks so they fire
  // at the statement site where the function definition appears.
  // The function_aliases map is updated to point at the wrapper
  // so subsequent calls to `f` dispatch through the decorator.
  if(decorators.is_array() && !is_c_intrinsic)
  {
    // Collect non-overload, non-c_intrinsic decorators (bottom-up order
    // = reverse of the list in the AST, which is top-down).
    std::vector<const jsont *> user_decorators;
    for(const auto &dec : as_array(decorators))
    {
      if(
        is_node_type(dec, "Name") &&
        json_string(json_member(dec, "id")) == "overload")
        continue;
      if(
        is_node_type(dec, "Name") &&
        (json_string(json_member(dec, "id")) == "staticmethod" ||
         json_string(json_member(dec, "id")) == "classmethod" ||
         json_string(json_member(dec, "id")) == "property"))
        continue;
      if(is_node_type(dec, "Call"))
      {
        const jsont &df = json_member(dec, "func");
        if(
          is_node_type(df, "Name") &&
          json_string(json_member(df, "id")) == "c_intrinsic")
          continue;
      }
      user_decorators.push_back(&dec);
    }
    if(!user_decorators.empty())
    {
      // Build the chain: start with the raw function, apply each
      // decorator from bottom (last in list) to top (first).
      // For each decorator, look up its symbol and emit a call.
      // The final result is aliased to the function name.
      code_blockt dec_block;
      for(auto rit = user_decorators.rbegin(); rit != user_decorators.rend();
          ++rit)
      {
        const jsont &dec = **rit;
        exprt dec_expr = convert_expression(dec);
        if(dec_expr.is_nil() || dec_expr.type().id() != ID_code)
        {
          // PLR §8.7: `@dec def f` applies `f = dec(f)` at def-time, so the
          // decorator value MUST be callable. A provably non-callable concrete
          // value (int/float/str/list/...) raises TypeError here. Gated to a
          // PROVABLE violation: a nil decorator (unresolved import / forward
          // reference) and a python_value (Any) decorator cannot be proven
          // non-callable, so they are NOT flagged (no false positive). A
          // callable instance (a class with __call__) is ID_code-typed via its
          // dispatch and falls through. The exception is emitted DIRECTLY into
          // dec_block (which convert_function_def returns) -- not via
          // pending_checks, which are discarded at the def site.
          // Only a bare `@name` decorator is checked here (a Call/Attribute
          // factory or library decorator cannot be proven non-callable -> no
          // false positive).
          if(
            is_node_type(dec, "Name") && !dec_expr.is_nil() &&
            !is_python_value_type(dec_expr.type()))
          {
            // Callable iff a user-class instance whose MRO defines __call__;
            // any other concrete value, or a user-class lacking __call__, is
            // non-callable.
            std::string dtag;
            if(dec_expr.type().id() == ID_struct)
              dtag = id2string(to_struct_type(dec_expr.type()).get_tag());
            else if(dec_expr.type().id() == ID_struct_tag)
              dtag =
                id2string(to_struct_tag_type(dec_expr.type()).get_identifier());
            const bool is_user_class =
              dtag.compare(0, 13, "python_class_") == 0;
            const bool noncallable =
              !is_user_class ||
              concrete_class_lacks_dunder(dec_expr.type(), "__call__");
            if(noncallable)
            {
              const symbolt *ea =
                symbol_table.lookup("python::__exception_active");
              const symbolt *et =
                symbol_table.lookup("python::__exception_type");
              if(ea != nullptr)
              {
                dec_block.add(
                  code_frontend_assignt{ea->symbol_expr(), true_exprt{}});
                if(et != nullptr)
                  dec_block.add(code_frontend_assignt{
                    et->symbol_expr(),
                    from_integer(exception_type_hash("TypeError"), et->type)});
              }
            }
          }
          continue;
        }
        // The decorator is a function. Call it with the current
        // function as argument. The result becomes the new binding.
        // We model this by recording the decorator's inner function
        // (if it returns one) as the alias target. For the common
        // pattern where the decorator defines a `wrapper` inside and
        // returns it, the wrapper is already registered as a symbol
        // by convert_function_def (since it's a nested def inside
        // the decorator body). We look for it by convention:
        // decorator_name::wrapper.
        std::string dec_name =
          id2string(to_symbol_expr(dec_expr).get_identifier());
        // Look for a nested function named "wrapper" inside the decorator.
        // The frontend registers nested functions with various naming
        // conventions; try the most common ones.
        irep_idt wrapper_id;
        for(const std::string &candidate :
            {dec_name + "::wrapper",
             std::string{"python::wrapper"},
             std::string{"python::" + func_name + "::wrapper"}})
        {
          if(symbol_table.lookup(irep_idt{candidate}) != nullptr)
          {
            wrapper_id = irep_idt{candidate};
            break;
          }
        }
        // Also scan the decorator's body AST for a nested FunctionDef
        // named "wrapper" and use whatever symbol name it got.
        // PLR §8.7: decorators may be defined inside another
        // function (e.g. a test harness that defines `my_dec`
        // and then `@my_dec def f(): ...` all inside one
        // outer function). We therefore search the parse tree
        // RECURSIVELY for the decorator's FunctionDef node,
        // not just the module body.
        std::function<const jsont *(const jsont &, const std::string &)>
          find_func_def =
            [&](
              const jsont &scope_body, const std::string &name) -> const jsont *
        {
          if(!scope_body.is_array())
            return nullptr;
          for(const auto &s : as_array(scope_body))
          {
            if(
              is_node_type(s, "FunctionDef") &&
              json_string(json_member(s, "name")) == name)
              return &s;
            // Recurse into nested function bodies to find
            // decorators defined inside other functions.
            if(is_node_type(s, "FunctionDef") || is_node_type(s, "ClassDef"))
            {
              const jsont &b = json_member(s, "body");
              if(const jsont *r = find_func_def(b, name))
                return r;
            }
            // Recurse into if/for/while/try bodies too, in case
            // the decorator is defined inside a control-flow block.
            for(const std::string &fld : std::vector<std::string>{
                  "body", "orelse", "finalbody", "handlers"})
            {
              const jsont &fb = json_member(s, fld);
              if(fb.is_array())
                if(const jsont *r = find_func_def(fb, name))
                  return r;
            }
          }
          return nullptr;
        };
        if(wrapper_id.empty())
        {
          std::string dec_short =
            id2string(to_symbol_expr(dec_expr).get_identifier()).substr(8);
          const jsont *dec_ast =
            find_func_def(json_member(parse_tree.ast_json, "body"), dec_short);
          if(dec_ast != nullptr)
          {
            const jsont &dec_body = json_member(*dec_ast, "body");
            if(dec_body.is_array())
            {
              for(const auto &inner : as_array(dec_body))
              {
                if(is_node_type(inner, "FunctionDef"))
                {
                  std::string inner_name =
                    json_string(json_member(inner, "name"));
                  // PLR §8.7: the wrapper is a nested function of the
                  // decorator, so its symbol is QUALIFIED by the decorator's
                  // name (python::<dec>::<inner>). Look there first; fall back
                  // to the bare name for top-level / older shapes. Without the
                  // qualifier the lookup missed, the alias was never registered,
                  // and the call bypassed the wrapper (so its arity was never
                  // enforced -- the dec_wrong_arity false proof).
                  irep_idt cand{"python::" + dec_short + "::" + inner_name};
                  if(symbol_table.lookup(cand) == nullptr)
                    cand = irep_idt{"python::" + inner_name};
                  if(symbol_table.lookup(cand) != nullptr)
                  {
                    wrapper_id = cand;
                    break;
                  }
                }
              }
            }
          }
        }
        const symbolt *wrapper_sym =
          wrapper_id.empty() ? nullptr : symbol_table.lookup(wrapper_id);
        if(wrapper_sym != nullptr && wrapper_sym->type.id() == ID_code)
        {
          // Redirect calls to f to go through wrapper instead.
          // The qualified symbol_id covers callers using the
          // qualified id directly. We additionally register an
          // alias keyed by the BARE name `python::<func_name>`
          // to support callers that haven't switched to qualified
          // ids (e.g. module-level dispatch through a closure
          // alias). For nested functions, callers inside the same
          // enclosing function use `python::<enclosing>::<func>`,
          // which equals symbol_id and is already covered.
          function_aliases[id2string(symbol_id)] = wrapper_id;
          if(!saved_function.empty() && saved_function != current_function)
            function_aliases["python::" + func_name] = wrapper_id;
          // The wrapper's body calls `fn(*args)` where `fn` is a
          // closure-captured reference to the original function.
          // Bind `fn` (the decorator's parameter name) to the
          // original function, then RE-CONVERT the wrapper's body
          // so the call resolves correctly. The first conversion
          // happened when the decorator was defined (before we knew
          // what fn would be), so it emitted "no body for callee fn".
          const code_typet &dec_type =
            to_code_type(symbol_table.lookup_ref(dec_name).type);
          if(!dec_type.parameters().empty())
          {
            std::string param_base =
              id2string(dec_type.parameters()[0].get_base_name());
            // Bind fn → original f in all scopes the wrapper might
            // look it up from. Guard the global bare-name binding when the
            // decorator's parameter shares the decorated function's name
            // (`def dec(f): ... @dec def f`): writing `python::f -> f` there
            // would clobber the decorated-function -> wrapper alias above and
            // mask a wrong-arity call. The wrapper-scoped and param-qualified
            // bindings below still resolve `fn` inside the wrapper body.
            if(irep_idt{"python::" + param_base} != symbol_id)
              function_aliases["python::" + param_base] = symbol_id;
            function_aliases[id2string(wrapper_id) + "::" + param_base] =
              symbol_id;
            function_aliases[id2string(
              dec_type.parameters()[0].get_identifier())] = symbol_id;
          }
          // Re-convert the wrapper's body now that fn is bound.
          // Find the wrapper's AST in the decorator's body — search
          // RECURSIVELY through the parse tree so decorators
          // defined inside another function are also handled.
          std::function<const jsont *(const jsont &, const std::string &)>
            find_decorator_ast = [&](
                                   const jsont &scope_body,
                                   const std::string &name) -> const jsont *
          {
            if(!scope_body.is_array())
              return nullptr;
            for(const auto &s : as_array(scope_body))
            {
              if(
                is_node_type(s, "FunctionDef") &&
                json_string(json_member(s, "name")) == name)
                return &s;
              if(is_node_type(s, "FunctionDef") || is_node_type(s, "ClassDef"))
              {
                const jsont &b = json_member(s, "body");
                if(const jsont *r = find_decorator_ast(b, name))
                  return r;
              }
              for(const std::string &fld : std::vector<std::string>{
                    "body", "orelse", "finalbody", "handlers"})
              {
                const jsont &fb = json_member(s, fld);
                if(fb.is_array())
                  if(const jsont *r = find_decorator_ast(fb, name))
                    return r;
              }
            }
            return nullptr;
          };
          std::string dec_short =
            id2string(to_symbol_expr(dec_expr).get_identifier()).substr(8);
          // PLR §4.2.1: when the decorator itself is nested
          // (e.g. `def block_0(): def my_dec(...): ...`), its
          // qualified id is `python::block_0::my_dec`. The AST
          // node has the bare name "my_dec" — extract the last
          // `::` component to match against the AST.
          std::string dec_short_bare = dec_short;
          if(auto pos = dec_short.rfind("::"); pos != std::string::npos)
            dec_short_bare = dec_short.substr(pos + 2);
          const jsont *dec_ast = find_decorator_ast(
            json_member(parse_tree.ast_json, "body"), dec_short_bare);
          if(dec_ast != nullptr)
          {
            const jsont &dec_body_ast = json_member(*dec_ast, "body");
            if(dec_body_ast.is_array())
            {
              for(const auto &inner : as_array(dec_body_ast))
              {
                if(!is_node_type(inner, "FunctionDef"))
                  continue;
                std::string inner_name =
                  json_string(json_member(inner, "name"));
                // Build the qualified id of the inner function:
                // it's a nested def inside the decorator, so its
                // symbol id is `python::<dec_short>::<inner_name>`.
                irep_idt inner_qid{"python::" + dec_short + "::" + inner_name};
                if(inner_qid != wrapper_id)
                  continue;
                // Re-convert the wrapper function body
                std::string saved_fn = current_function;
                // Use the wrapper's qualified id (without the
                // "python::" prefix) so qualify_name() and the
                // closure-capture mechanism resolve names against
                // the right scope.
                current_function = id2string(wrapper_id).substr(8);
                const jsont &wrapper_body_ast = json_member(inner, "body");
                code_blockt new_body;
                if(wrapper_body_ast.is_array())
                {
                  for(const auto &ws : as_array(wrapper_body_ast))
                    new_body.add(convert_statement(ws));
                }
                // Append default return
                const code_typet &wt = to_code_type(wrapper_sym->type);
                if(wt.return_type().id() != ID_empty)
                  new_body.add(
                    code_frontend_returnt{safe_zero(wt.return_type())});
                current_function = saved_fn;
                symbol_table.get_writeable_ref(wrapper_id).value = new_body;
                break;
              }
            }
          }
        }
      }
      if(!dec_block.statements().empty())
        return std::move(dec_block);
    }
  }

  // Closure cell substrate (PLR §4.2.2), mutating slice: if this nested
  // function captures nonlocal cell-variables by HEAP CELL, emit the
  // per-invocation cell allocation + initialisation into the ENCLOSING
  // body at this def's site. current_function has been restored to the
  // parent scope here, so python::<parent>::<cv> reads the enclosing
  // value and python::<parent>::__cell_<cv> is the cell pointer the
  // escaping closure captures (snapshotted at the factory call site).
  {
    auto nca = nested_cell_allocs.find("python::" + qualified_func_name);
    if(nca != nested_cell_allocs.end())
    {
      namespacet ns{symbol_table};
      code_blockt cell_block;
      for(const auto &[cv, valtype] : nca->second)
      {
        const symbolt *cs = symbol_table.lookup(
          irep_idt{"python::" + current_function + "::__cell_" + cv});
        const symbolt *cvs = symbol_table.lookup(
          irep_idt{"python::" + current_function + "::" + cv});
        if(cs == nullptr || cvs == nullptr)
          continue;
        exprt size = from_integer(
          pointer_offset_size(valtype, ns).value_or(8), size_type());
        side_effect_exprt alloc{
          ID_allocate, {size, false_exprt{}}, cs->type, loc};
        cell_block.add(code_frontend_assignt{cs->symbol_expr(), alloc});
        cell_block.add(code_frontend_assignt{
          dereference_exprt{cs->symbol_expr()}, cvs->symbol_expr()});
      }
      if(!cell_block.statements().empty())
        return std::move(cell_block);
    }
  }

  return code_skipt{};
}

// PLR §8.9: Class definitions
// PLR §3.2: "Class instances have a namespace implemented as a dictionary."
// We model classes as structs with __class_tag for dispatch.
// "A class definition defines a class object."
codet python_convertert::convert_class_def(const jsont &stmt)
{
  std::string class_name = json_string(json_member(stmt, "name"));
  source_locationt loc = get_location(stmt);

  // Phase 6 of the icontract integration plan
  // (see the Contracts section of doc/python-frontend-architecture.md): collect class-level
  // @icontract.invariant decorators. Each invariant lambda is
  // asserted at the entry and exit of every public method (with
  // a special exception for __init__ which only gets the exit
  // assertion since the object doesn't exist at entry). We store
  // the lambda body pointers here; the actual translation
  // happens inside each method's conversion scope further down,
  // where `self` resolves to the method's first parameter.
  //
  // Phase 7 (Liskov inheritance composition): walk the bases
  // and prepend the parent classes' invariants. Per Liskov
  // substitution, child invariants merge with parents' via
  // AND — the child must satisfy every invariant the parent
  // declared. We achieve this by including the parent's
  // lambdas in the child's effective set; each method's body
  // then asserts all of them. The walk uses
  // class_invariant_lambdas which is populated as each class
  // is processed (parent before child by Python's source-order
  // requirement).
  std::vector<const jsont *> icontract_invariant_lambdas;
  {
    const jsont &cls_decorators = json_member(stmt, "decorator_list");
    if(cls_decorators.is_array())
    {
      for(const auto &dec : as_array(cls_decorators))
      {
        if(!is_node_type(dec, "Call"))
          continue;
        const jsont &dec_func = json_member(dec, "func");
        bool is_inv = false;
        if(is_node_type(dec_func, "Attribute"))
        {
          const jsont &v = json_member(dec_func, "value");
          if(
            is_node_type(v, "Name") &&
            json_string(json_member(v, "id")) == "icontract" &&
            json_string(json_member(dec_func, "attr")) == "invariant")
            is_inv = true;
        }
        else if(
          is_node_type(dec_func, "Name") &&
          json_string(json_member(dec_func, "id")) == "invariant")
          is_inv = true;
        if(!is_inv)
          continue;
        const jsont &dec_args = json_member(dec, "args");
        if(!dec_args.is_array() || as_array(dec_args).empty())
          continue;
        const jsont &first = *as_array(dec_args).begin();
        if(is_node_type(first, "Lambda"))
          icontract_invariant_lambdas.push_back(&first);
      }
    }
  }
  // Phase 7: prepend parent classes' invariants. We walk the
  // explicit bases in source order; each base's effective
  // invariants are already composed (recursive merge happened
  // when the base was processed) so we don't need to re-walk
  // grandparents.
  {
    const jsont &cls_bases = json_member(stmt, "bases");
    if(cls_bases.is_array())
    {
      std::vector<const jsont *> inherited;
      for(const auto &base : as_array(cls_bases))
      {
        if(!is_node_type(base, "Name"))
          continue;
        std::string base_name = json_string(json_member(base, "id"));
        auto it = class_invariant_lambdas.find(base_name);
        if(it == class_invariant_lambdas.end())
          continue;
        for(const jsont *lam : it->second)
          inherited.push_back(lam);
      }
      // Inherited invariants come first so they appear in a
      // predictable order in the generated assertions.
      if(!inherited.empty())
      {
        inherited.insert(
          inherited.end(),
          icontract_invariant_lambdas.begin(),
          icontract_invariant_lambdas.end());
        icontract_invariant_lambdas = std::move(inherited);
      }
    }
  }
  // Register this class's effective invariant lambdas (own +
  // inherited) so future subclasses can inherit them.
  if(!icontract_invariant_lambdas.empty())
    class_invariant_lambdas[class_name] = icontract_invariant_lambdas;

  // Analyze methods to determine instance attributes (PLR §6.3.1
  // assignment to attribute reference creates a new attribute).
  struct_typet::componentst components;
  const jsont &body = json_member(stmt, "body");

  // PLR §8.9: Inherit fields from base classes (supports multiple inheritance)
  const jsont &bases = json_member(stmt, "bases");
  std::set<std::string> inherited_fields;
  // All field names already added to `components` (regardless of
  // origin). Used by the class-level / __init__ scanners below to
  // avoid emitting duplicate field declarations — the SMT-LIB
  // back-end (smt2_conv) lowers each component as a datatype
  // selector, and CVC5 rejects datatypes whose constructor has
  // two selectors with the same name. Pre-populated with the
  // names already inherited so subclass scans skip them too.
  std::set<std::string> declared_fields;
  // PLR §8.13: is this an enum class (derives from enum.Enum or an
  // alias / a known enum base)? If so, record its member names so
  // `Member.value` / `Member.name` resolve.
  if(bases.is_array())
  {
    for(const auto &base : as_array(bases))
    {
      if(
        is_node_type(base, "Name") &&
        enum_base_aliases.count(json_string(json_member(base, "id"))))
      {
        auto &members = enum_members[class_name];
        typet &vtype = enum_value_type[class_name];
        vtype = python_int_type(); // default for int-valued enums
        // PLR §8.13: a HETEROGENEOUS enum (members with different value types,
        // e.g. `A = 1; B = "x"`) has a union value type. Track whether any
        // member value is a string vs non-string; if BOTH appear, type `.value`
        // as python_value (Any) so a member-typed variable reassigned across
        // members carries the correct RUNTIME tag, and a use at the wrong type
        // (`S.B.value - 1`) is caught by the operand/tag obligations -- the same
        // mechanism that already makes plain `x: "int|str"` retags sound.
        // Without this, a mixed enum defaulted to the first member's type, so a
        // later member's `.value` was read at the stale type (a false proof:
        // enum-value-after-mutation).
        bool enum_saw_str = false, enum_saw_nonstr = false;
        if(body.is_array())
          for(const auto &item : as_array(body))
          {
            const jsont *tgt = nullptr;
            const jsont *valnode = nullptr;
            if(is_node_type(item, "Assign"))
            {
              const jsont &tgts = json_member(item, "targets");
              if(tgts.is_array() && !as_array(tgts).empty())
                tgt = &(*as_array(tgts).begin());
              valnode = &json_member(item, "value");
            }
            else if(is_node_type(item, "AnnAssign"))
            {
              tgt = &json_member(item, "target");
              valnode = &json_member(item, "value");
            }
            if(tgt != nullptr && is_node_type(*tgt, "Name"))
            {
              members.insert(json_string(json_member(*tgt, "id")));
              const bool is_str_val =
                valnode != nullptr && is_node_type(*valnode, "Constant") &&
                json_member(*valnode, "value").is_string();
              if(is_str_val)
                enum_saw_str = true;
              else
                enum_saw_nonstr = true;
            }
          }
        if(enum_saw_str && enum_saw_nonstr)
          vtype = python_value_type(); // heterogeneous -> union (Any)
        else if(enum_saw_str)
          vtype = python_string_type();
        // else: all-non-string -> python_int_type() (the default above)
        break;
      }
    }
  }
  if(bases.is_array())
  {
    for(const auto &base : as_array(bases))
    {
      if(is_node_type(base, "Name"))
      {
        std::string base_name = json_string(json_member(base, "id"));
        if(class_types.count(base_name))
        {
          const auto &base_type = class_types[base_name];
          for(const auto &comp : base_type.components())
          {
            std::string fname = id2string(comp.get_name());
            // Skip __class_tag and already-inherited fields (diamond)
            if(fname == "__class_tag" || inherited_fields.count(fname))
              continue;
            inherited_fields.insert(fname);
            declared_fields.insert(fname);
            components.push_back(comp);
          }
          // Inherit class-level-attribute tracking from base —
          // a class-level attr in the parent stays class-level
          // in the child, even if the child overrides its
          // value. The child can still shadow it on instances.
          auto base_it = class_level_attrs.find(base_name);
          if(base_it != class_level_attrs.end())
          {
            for(const auto &a : base_it->second)
              class_level_attrs[class_name].insert(a);
          }
        }
      }
    }
  }

  // Scan class body for class-level attributes (AnnAssign outside methods)
  if(body.is_array())
  {
    for(const auto &item : as_array(body))
    {
      if(is_node_type(item, "AnnAssign"))
      {
        const jsont &target = json_member(item, "target");
        if(is_node_type(target, "Name"))
        {
          std::string attr_name = json_string(json_member(target, "id"));
          // PLR §3.3.2: track ownership independently of the
          // struct-component dedup. A subclass that
          // re-declares an inherited attr in its own body
          // OWNS the attr (overrides the parent's value), so
          // class_owned_attrs gets the entry even when the
          // struct already has the field via inheritance.
          class_owned_attrs[class_name].insert(attr_name);
          if(declared_fields.insert(attr_name).second)
          {
            typet attr_type =
              convert_type_annotation(json_member(item, "annotation"));
            components.push_back(
              struct_typet::componentt{attr_name, attr_type});
            // PLR §9.4: track this as a class-level attribute
            // so attribute reads can dispatch via the
            // shadow-fallback ternary.
            class_level_attrs[class_name].insert(attr_name);
            // Add the synthetic shadow flag immediately after
            // the value field so the struct layout is
            // deterministic. The flag is False by default
            // (zero-initialised); instance writes set it to
            // True so subsequent reads return the instance
            // value rather than falling back to class storage.
            components.push_back(struct_typet::componentt{
              "__shadow_" + attr_name, c_bool_typet{8}});
          }
        }
      }
      else if(is_node_type(item, "Assign"))
      {
        const jsont &targets = json_member(item, "targets");
        if(targets.is_array())
        {
          for(const auto &t : as_array(targets))
          {
            if(is_node_type(t, "Name"))
            {
              std::string attr_name = json_string(json_member(t, "id"));
              // PLR §3.3.2: track ownership independently of
              // the struct-component dedup so subclass overrides
              // are recorded even when the struct already has
              // the inherited field.
              class_owned_attrs[class_name].insert(attr_name);
              if(!declared_fields.insert(attr_name).second)
                continue;
              // Infer type from value
              const jsont &val = json_member(item, "value");
              typet attr_type = python_int_type();
              if(is_node_type(val, "Constant"))
              {
                const jsont &v = json_member(val, "value");
                if(v.is_string())
                  attr_type = python_string_type();
                else if(v.is_true() || v.is_false())
                  attr_type = python_int_type();
              }
              // §11b: a class attribute bound to an instance of a class
              // defining __get__ is a (custom) descriptor. Type the
              // field as that class and record it so attribute reads
              // dispatch __get__ rather than returning the instance.
              if(
                is_node_type(val, "Call") &&
                is_node_type(json_member(val, "func"), "Name"))
              {
                std::string cn =
                  json_string(json_member(json_member(val, "func"), "id"));
                if(
                  class_types.count(cn) &&
                  symbol_table.lookup(
                    irep_idt{"python::" + cn + "::__get__"}) != nullptr)
                {
                  attr_type = class_types[cn];
                  class_descriptor_attrs[class_name][attr_name] = cn;
                }
              }
              components.push_back(
                struct_typet::componentt{attr_name, attr_type});
              // PLR §9.4: same shadow tracking as AnnAssign above.
              class_level_attrs[class_name].insert(attr_name);
              components.push_back(struct_typet::componentt{
                "__shadow_" + attr_name, c_bool_typet{8}});
            }
          }
        }
      }
    }
  }

  // Scan all method bodies for self.attr = ... assignments to
  // determine fields. Python allows attributes to be created
  // dynamically by any method (PLR §6.3.1: "An attribute reference
  // is a primary followed by a period and a name"; assigning to
  // such a target creates a new attribute when not present). The
  // converter cannot do this lazily because the struct type must
  // be fixed at class-definition time, so we collect from every
  // method body up front.
  std::vector<const jsont *> methods_to_scan;
  if(body.is_array())
  {
    for(const auto &item : as_array(body))
    {
      if(
        is_node_type(item, "FunctionDef") ||
        is_node_type(item, "AsyncFunctionDef"))
        methods_to_scan.push_back(&item);
    }
  }
  // Attribute-protocol (del + __getattr__): a field that is `del`-ed ANYWHERE
  // (via `del self.x` in a method, OR a direct `del obj.x` in module/function
  // code on an external instance) on a class that defines __getattr__ must,
  // after the del, read back __getattr__'s (possibly differently-typed) result
  // -- not the stale field. Model such fields as `python_value` so the slot can
  // hold both the normal value and the __getattr__ fallback, and so a later
  // cross-type use routes through the operand/tag obligations. `del obj.x` then
  // assigns the __getattr__ result into the slot (see the Delete handler). The
  // scan below is MODULE-WIDE and keys on the attribute NAME (a sound
  // over-approximation -- an extra python_value field just routes through the
  // tag obligations, never a false proof; only __getattr__-classes are
  // affected). Closes c4_getattr_fallback / laurel-006 + the external-del
  // residual. (Residual: `del o.x` through an Any-boxed function parameter does
  // not propagate -- the structural-mutation-through-a-call family.)
  bool class_defines_getattr = false;
  std::set<std::string> getattr_deletable_fields;
  for(const jsont *mn : methods_to_scan)
    if(json_string(json_member(*mn, "name")) == "__getattr__")
      class_defines_getattr = true;
  if(class_defines_getattr)
  {
    std::function<void(const jsont &)> walk = [&](const jsont &n)
    {
      if(n.is_array())
      {
        for(const auto &e : as_array(n))
          walk(e);
        return;
      }
      if(!n.is_object())
        return;
      if(is_node_type(n, "Delete"))
      {
        const jsont &tgts = json_member(n, "targets");
        if(tgts.is_array())
          for(const auto &t : as_array(tgts))
            if(is_node_type(t, "Attribute")) // any `<expr>.<attr>` deletion
              getattr_deletable_fields.insert(
                json_string(json_member(t, "attr")));
      }
      for(const char *k : {"body", "orelse", "finalbody", "handlers"})
      {
        const jsont &c = json_member(n, k);
        if(!c.is_null())
          walk(c);
      }
    };
    // Whole-module AST: covers `del self.x` in this class's methods AND a
    // `del obj.x` in module-level / function code on an external instance.
    walk(json_member(parse_tree.ast_json, "body"));
  }
  // External-store slot-pun (PLR §3.1) -- the LAST slot-pun. A concrete-typed
  // field (`self.x: int`) puns a mismatched value stored EXTERNALLY (`o.x = "s"`
  // / `o.x = src()`), which the per-method self-store scan does not see. Storing
  // is legal Python (the error arises on a later USE), so the fix is to WIDEN
  // the field to python_value at class-definition time. Module-wide scan keying
  // on the attribute NAME (a sound over-approximation, exactly like the del +
  // __getattr__ scan above): record the scalar CATEGORY of each externally
  // stored value, then widen a concrete scalar field whose category differs.
  // Scoped to Constant literals + resolvable Call return types (a bare Name RHS
  // is left alone to avoid over-widening); bool is treated as int (Python
  // subtype). Not under --python-check-annotations (that mode reports the
  // mismatch as a property instead).
  std::map<std::string, std::set<std::string>> ext_store_scalar_cats;
  const auto type_cat = [this](const typet &t) -> std::string
  {
    if(t == python_string_type())
      return "str";
    if(t == double_type())
      return "float";
    if(t == python_int_type() || t == bool_typet{})
      return "int"; // bool is a subtype of int
    return "";
  };
  if(!python_check_annotations)
  {
    // Map function name -> return scalar category, resolved from the AST (the
    // symbol table may not yet hold a callee defined AFTER this class). From the
    // `-> T` annotation, else inferred from the first `return <Constant>`.
    std::map<std::string, std::string> func_return_cat;
    const auto const_cat = [](const jsont &cv) -> std::string
    {
      if(cv.is_string())
        return "str";
      if(cv.is_boolean())
        return "int";
      if(cv.is_number())
        return cv.value.find('.') != std::string::npos ? "float" : "int";
      return "";
    };
    std::function<void(const jsont &)> fscan = [&](const jsont &n)
    {
      if(n.is_array())
      {
        for(const auto &e : as_array(n))
          fscan(e);
        return;
      }
      if(!n.is_object())
        return;
      if(is_node_type(n, "FunctionDef") || is_node_type(n, "AsyncFunctionDef"))
      {
        std::string fn = json_string(json_member(n, "name"));
        std::string cat;
        const jsont &ret = json_member(n, "returns");
        if(is_node_type(ret, "Name"))
        {
          const std::string tn = json_string(json_member(ret, "id"));
          if(tn == "str")
            cat = "str";
          else if(tn == "float")
            cat = "float";
          else if(tn == "int" || tn == "bool")
            cat = "int";
        }
        if(cat.empty())
        {
          // infer from the first `return <Constant>` in the body
          const jsont &fb = json_member(n, "body");
          if(fb.is_array())
            for(const auto &s : as_array(fb))
              if(
                is_node_type(s, "Return") &&
                is_node_type(json_member(s, "value"), "Constant"))
              {
                cat = const_cat(json_member(json_member(s, "value"), "value"));
                break;
              }
        }
        if(!cat.empty())
          func_return_cat[fn] = cat;
      }
      for(const char *k : {"body", "orelse", "finalbody", "handlers"})
      {
        const jsont &c = json_member(n, k);
        if(!c.is_null())
          fscan(c);
      }
    };
    fscan(json_member(parse_tree.ast_json, "body"));

    const auto cat_of_value = [&](const jsont &v) -> std::string
    {
      if(is_node_type(v, "Constant"))
        return const_cat(json_member(v, "value"));
      if(
        is_node_type(v, "Call") && is_node_type(json_member(v, "func"), "Name"))
      {
        auto it = func_return_cat.find(
          json_string(json_member(json_member(v, "func"), "id")));
        if(it != func_return_cat.end())
          return it->second;
      }
      return "";
    };
    std::function<void(const jsont &)> escan = [&](const jsont &n)
    {
      if(n.is_array())
      {
        for(const auto &e : as_array(n))
          escan(e);
        return;
      }
      if(!n.is_object())
        return;
      if(is_node_type(n, "Assign"))
      {
        const jsont &tgts = json_member(n, "targets");
        if(tgts.is_array())
          for(const auto &t : as_array(tgts))
            if(is_node_type(t, "Attribute"))
            {
              std::string cat = cat_of_value(json_member(n, "value"));
              if(!cat.empty())
                ext_store_scalar_cats[json_string(json_member(t, "attr"))]
                  .insert(cat);
            }
      }
      for(const char *k : {"body", "orelse", "finalbody", "handlers"})
      {
        const jsont &c = json_member(n, k);
        if(!c.is_null())
          escan(c);
      }
    };
    escan(json_member(parse_tree.ast_json, "body"));
  }
  // Widen a concrete scalar field to python_value when an external store of a
  // DIFFERENT scalar category was seen for a field of that name.
  const auto ext_store_punned = [&](const std::string &attr, const typet &d)
  {
    auto it = ext_store_scalar_cats.find(attr);
    if(it == ext_store_scalar_cats.end())
      return false;
    const std::string dcat = type_cat(d);
    if(dcat.empty())
      return false;
    for(const auto &c : it->second)
      if(c != dcat)
        return true;
    return false;
  };

  auto getattr_deletable_override =
    [&](const std::string &attr_name, typet &attr_type)
  {
    if(class_defines_getattr && getattr_deletable_fields.count(attr_name))
      attr_type = python_value_type();
  };
  for(const jsont *method_node : methods_to_scan)
  {
    const jsont &init_body = json_member(*method_node, "body");
    if(init_body.is_array())
    {
      for(const auto &s : as_array(init_body))
      {
        // Handle AnnAssign: self.attr: Type = value
        if(is_node_type(s, "AnnAssign"))
        {
          const jsont &target = json_member(s, "target");
          if(!is_node_type(target, "Attribute"))
            continue;
          const jsont &target_value = json_member(target, "value");
          if(
            !is_node_type(target_value, "Name") ||
            json_string(json_member(target_value, "id")) != "self")
            continue;

          std::string attr_name = json_string(json_member(target, "attr"));
          const jsont &annotation = json_member(s, "annotation");
          typet attr_type = annotation.is_null()
                              ? python_value_type()
                              : convert_type_annotation(annotation);
          // §field slot-pun (default mode): a `self.x: T = v` init where T is a
          // concrete SCALAR but the init value's type is uninferable (e.g. an
          // UNANNOTATED param -> Any) or a different scalar category WIDENS the
          // field to python_value, so a mismatched store preserves its runtime
          // tag instead of being PUNNED into T (a false proof). Mirrors the
          // list-element inference; also fixes later external `o.x = <bad>`
          // stores since they coerce to the (now widened) field type. Under
          // --python-check-annotations the concrete T is kept so the mismatch
          // is reported as a property.
          if(!python_check_annotations)
          {
            const auto is_scalar = [this](const typet &t)
            {
              return t == python_int_type() || t == double_type() ||
                     t == bool_typet{} || t == python_string_type();
            };
            if(is_scalar(attr_type))
            {
              const jsont &av = json_member(s, "value");
              std::optional<typet> vt;
              bool considered = false;
              if(is_node_type(av, "Constant"))
              {
                considered = true;
                const jsont &cv = json_member(av, "value");
                if(cv.is_string())
                  vt = python_string_type();
                else if(cv.is_boolean())
                  vt = bool_typet{};
                else if(cv.is_number())
                {
                  std::string vs = cv.value;
                  vt = (vs.find('.') != std::string::npos) ? double_type()
                                                           : python_int_type();
                }
              }
              else if(is_node_type(av, "Name"))
              {
                considered = true;
                // resolve a param's declared type; unannotated -> Any (nullopt)
                std::string vn = json_string(json_member(av, "id"));
                const jsont &margs = json_member(*method_node, "args");
                const jsont &params = json_member(margs, "args");
                if(params.is_array())
                  for(const auto &p : as_array(params))
                    if(json_string(json_member(p, "arg")) == vn)
                    {
                      const jsont &pa = json_member(p, "annotation");
                      if(!pa.is_null())
                        vt = convert_type_annotation(pa);
                      break;
                    }
              }
              // Only widen for a store form we actually resolved (Constant /
              // Name param). Other RHS forms keep the annotation (conservative
              // -- avoids over-widening a `self.x: int = compute()`).
              if(considered)
              {
                const bool uninferable =
                  !vt.has_value() || vt->id().empty() || vt->id() == ID_empty;
                if(uninferable || (is_scalar(*vt) && *vt != attr_type))
                  attr_type = python_value_type();
              }
            }
          }
          // reference-semantics-for-instances (Phase 3): an instance-typed
          // field assigned a by-REFERENCE value (`self.x: C = <Name>` -- a
          // param/alias) is held by reference -> type it pointer-to-instance so
          // it aliases (not value-copies). A FRESH construction
          // (`self.x: C = C()`) is an OWNED object -> keep the struct type
          // (distinct per instance; pointer-ifying it would make all instances
          // share one constructed temp -- over-aliasing).
          const jsont &av = json_member(s, "value");
          const bool field_is_instance =
            (attr_type.id() == ID_struct &&
             id2string(to_struct_type(attr_type).get_tag())
                 .find("python_class_") != std::string::npos) ||
            (attr_type.id() == ID_struct_tag &&
             id2string(to_struct_tag_type(attr_type).get_identifier())
                 .find("python_class_") != std::string::npos);
          if(field_is_instance && is_node_type(av, "Name"))
            attr_type = pointer_type(attr_type);
          // External-store slot-pun: widen when a mismatched external store to
          // a field of this name was seen module-wide.
          if(ext_store_punned(attr_name, attr_type))
            attr_type = python_value_type();
          getattr_deletable_override(attr_name, attr_type);
          if(declared_fields.insert(attr_name).second)
            components.push_back(
              struct_typet::componentt{attr_name, attr_type});
          continue;
        }

        if(!is_node_type(s, "Assign"))
          continue;
        const jsont &targets = json_member(s, "targets");
        if(!targets.is_array() || as_array(targets).empty())
          continue;
        const jsont &target = *as_array(targets).begin();
        if(!is_node_type(target, "Attribute"))
          continue;
        const jsont &target_value = json_member(target, "value");
        if(
          !is_node_type(target_value, "Name") ||
          json_string(json_member(target_value, "id")) != "self")
          continue;

        std::string attr_name = json_string(json_member(target, "attr"));

        // Determine type from the method's parameter annotation.
        const jsont &rhs = json_member(s, "value");
        std::string rhs_name;
        if(is_node_type(rhs, "Name"))
          rhs_name = json_string(json_member(rhs, "id"));

        typet attr_type = python_value_type(); // tagged union for untyped

        // Check if RHS is a constructor call: self.inner = Inner(v)
        if(
          is_node_type(rhs, "Call") &&
          is_node_type(json_member(rhs, "func"), "Name"))
        {
          std::string ctor_name =
            json_string(json_member(json_member(rhs, "func"), "id"));
          if(class_types.count(ctor_name))
            attr_type = class_types[ctor_name];
        }
        else if(!rhs_name.empty())
        {
          // Find the parameter annotation in the current method.
          const jsont &init_args = json_member(*method_node, "args");
          const jsont &params = json_member(init_args, "args");
          if(params.is_array())
          {
            for(const auto &p : as_array(params))
            {
              if(json_string(json_member(p, "arg")) == rhs_name)
              {
                const jsont &ann = json_member(p, "annotation");
                if(!ann.is_null())
                {
                  typet at = convert_type_annotation(ann);
                  // reference-semantics-for-instances (Phase 3): a field
                  // assigned a by-reference instance PARAMETER (which is itself
                  // a pointer-to-instance) must PRESERVE identity. Type the
                  // field as a pointer-to-instance (matching the param) so the
                  // store binds the reference (not a value copy) and
                  // `obj.field.attr` dereferences to the SAME object. A concrete
                  // struct type value-copies the parameter -> a mutation through
                  // the field is invisible to the caller's object (the
                  // composition false proof).
                  const bool at_is_instance =
                    (at.id() == ID_struct &&
                     id2string(to_struct_type(at).get_tag())
                         .find("python_class_") != std::string::npos) ||
                    (at.id() == ID_struct_tag &&
                     id2string(to_struct_tag_type(at).get_identifier())
                         .find("python_class_") != std::string::npos);
                  attr_type = at_is_instance ? typet{pointer_type(at)} : at;
                }
                // else: stays as python_value_type (tagged union)
                break;
              }
            }
          }
        }
        else if(is_node_type(rhs, "BoolOp"))
        {
          // PLR §6.11: 'A and B' / 'A or B' yield the type of
          // their operands when both are the same type. Walk
          // the operands looking for a Name that resolves to
          // a typed parameter, or a Constant whose type we
          // can identify directly. This is a syntactic best-
          // effort: when the operands' types disagree the
          // attr stays python_value_type as the safe default.
          auto resolve_op_type =
            [&](const jsont &op_node) -> std::optional<typet>
          {
            if(is_node_type(op_node, "Name"))
            {
              std::string nm = json_string(json_member(op_node, "id"));
              const jsont &init_args = json_member(*method_node, "args");
              const jsont &params = json_member(init_args, "args");
              if(params.is_array())
              {
                for(const auto &p : as_array(params))
                {
                  if(json_string(json_member(p, "arg")) == nm)
                  {
                    const jsont &ann = json_member(p, "annotation");
                    if(!ann.is_null())
                      return convert_type_annotation(ann);
                    return std::nullopt;
                  }
                }
              }
            }
            if(is_node_type(op_node, "Constant"))
            {
              const jsont &cv = json_member(op_node, "value");
              if(cv.is_string())
                return python_string_type();
              if(cv.is_number())
              {
                std::string vs = cv.value;
                if(
                  vs.find('.') != std::string::npos ||
                  vs.find('e') != std::string::npos)
                  return double_type();
                return python_int_type();
              }
              if(cv.is_true() || cv.is_false())
                return bool_typet{};
            }
            return std::nullopt;
          };
          const jsont &bool_values = json_member(rhs, "values");
          if(bool_values.is_array() && as_array(bool_values).size() >= 2)
          {
            std::optional<typet> agreed;
            bool conflict = false;
            for(const auto &op_node : as_array(bool_values))
            {
              auto t = resolve_op_type(op_node);
              if(!t.has_value())
              {
                conflict = true;
                break;
              }
              if(!agreed.has_value())
                agreed = t;
              else if(*agreed != *t)
              {
                conflict = true;
                break;
              }
            }
            if(!conflict && agreed.has_value())
              attr_type = *agreed;
          }
        }

        if(ext_store_punned(attr_name, attr_type))
          attr_type = python_value_type();
        getattr_deletable_override(attr_name, attr_type);
        if(declared_fields.insert(attr_name).second)
          components.push_back(struct_typet::componentt{attr_name, attr_type});
      }
    }
  }

  // PLR §6.3.1: free-function dynamic attribute writes that
  // were collected during pass 0.27. Add as python_value-typed
  // fields (the safe default since the writer's RHS type isn't
  // available without deeper type inference).
  {
    auto dyn_it = dynamic_class_attrs.find(class_name);
    if(dyn_it != dynamic_class_attrs.end())
    {
      auto msa_it = method_shadow_attrs.find(class_name);
      for(const auto &attr_name : dyn_it->second)
      {
        if(declared_fields.insert(attr_name).second)
        {
          components.push_back(
            struct_typet::componentt{attr_name, python_value_type()});
          // PLR §3.3.2: a method-shadow attr needs the runtime shadow
          // flag + class_level_attrs membership so `c.m = v` sets the
          // flag (maybe_shadow_assign) and `c.m` reads dispatch via the
          // shadow ternary (with a nondet unshadowed fallback).
          if(
            msa_it != method_shadow_attrs.end() &&
            msa_it->second.count(attr_name))
          {
            class_level_attrs[class_name].insert(attr_name);
            components.push_back(struct_typet::componentt{
              "__shadow_" + attr_name, c_bool_typet{8}});
          }
        }
      }
    }
  }

  // Add __class_tag as first field for dynamic dispatch
  struct_typet::componentst tagged_components;
  tagged_components.push_back(
    struct_typet::componentt{"__class_tag", signedbv_typet{32}});
  // Inherit parent class attributes
  if(bases.is_array())
  {
    for(const auto &base : as_array(bases))
    {
      if(is_node_type(base, "Name"))
      {
        std::string base_name = json_string(json_member(base, "id"));
        if(class_types.count(base_name))
        {
          const auto &parent = to_struct_type(class_types[base_name]);
          for(const auto &pc : parent.components())
          {
            if(pc.get_name() == "__class_tag")
              continue; // already added
            // Only add if not already defined in child
            bool found = false;
            for(const auto &c : components)
            {
              if(c.get_name() == pc.get_name())
              {
                found = true;
                break;
              }
            }
            if(!found)
              tagged_components.push_back(pc);
          }
        }
      }
    }
  }
  for(auto &c : components)
    tagged_components.push_back(std::move(c));

  // Native SMT-String backend: str fields become STRING-ID HANDLES
  // (fixed-width; see python_string_handle_type). An inline smt_string
  // member made the struct variable-width and aborted smt2 SSA conversion
  // on every byte-granular identity read (unpack_struct); POINTER boxing
  // merely moved the byte-extraction to the pointed string (bv_to_expr,
  // reverted 2026-07-20). Reads map h -> strtab(h) at the
  // convert_attribute choke point; writes allocate handles in
  // coerce_assign_rhs.
  // EXEMPT frontend-library classes (src/python/library/...): their str
  // fields carry a BACKEND CONTRACT -- e.g. re.Pattern.pattern must be a
  // conversion-time-recoverable String constant for smt2's regex lowering
  // (constant propagation cannot see through the strtab UF). Library
  // structs are converter-controlled and never flow through the
  // byte-imaged identity reads that motivated handles (their receivers
  // are typed, not Any) -- the corpus crashes were all USER classes.
  const bool is_library_class =
    filename.find("/src/python/library/") != std::string::npos;
  if(python_smt_string_native_flag() && !is_library_class)
  {
    for(auto &c : tagged_components)
    {
      if(c.type().id() == ID_smt_string)
        c.type() = python_string_handle_type();
    }
  }

  // PLR §7.5/§6.10: for each attribute `del`-eted somewhere in the program AND
  // declared on this class, add a per-instance `__present_<attr>` bool flag
  // (mirrors the ripple-safe `__shadow_` bool field). A store sets it true, a
  // `del c.a` sets it false, and a read raises AttributeError when false.
  if(!deleted_attr_targets.empty())
  {
    std::set<std::string> existing_names;
    for(const auto &c : tagged_components)
      existing_names.insert(id2string(c.get_name()));
    std::vector<std::string> present_to_add;
    const auto cla_it = class_level_attrs.find(class_name);
    for(const auto &c : tagged_components)
    {
      const std::string n = id2string(c.get_name());
      // Only INSTANCE-ONLY attrs get a present flag. A class-level attr (with a
      // class default) uses the existing __shadow_ mechanism: `del c.a` there
      // removes the instance override and reads FALL BACK to the class value --
      // not AttributeError. Adding a present flag/read-guard for such attrs
      // would wrongly raise on the fallback read (class10 regression).
      const bool is_class_level =
        cla_it != class_level_attrs.end() && cla_it->second.count(n) > 0;
      if(
        deleted_attr_targets.count(n) > 0 && !is_class_level &&
        n.rfind("__", 0) != 0 && existing_names.count("__present_" + n) == 0)
        present_to_add.push_back(n);
    }
    for(const auto &n : present_to_add)
      tagged_components.push_back(
        struct_typet::componentt{"__present_" + n, c_bool_typet{8}});
  }

  struct_typet class_type{tagged_components};
  class_type.set_tag("python_class_" + class_name);
  class_types[class_name] = class_type;

  // §11: a read of a declared instance field before the instance has
  // assigned it is an AttributeError. Track, across the inheritance
  // chain: all bare class-body annotations (`x: T`, no value), and the
  // fields that constructing this class definitely assigns (own
  // __init__ top-level self-stores, plus the base chain when
  // super().__init__() is called -- so a subclass that omits super
  // leaves inherited fields unassigned). attrerror = all_bare minus
  // ctor_assigned.
  {
    std::set<std::string> own_bare, own_def;
    bool has_init = false, calls_super = false;
    if(body.is_array())
      for(const auto &item : as_array(body))
      {
        if(
          is_node_type(item, "AnnAssign") &&
          is_node_type(json_member(item, "target"), "Name") &&
          json_member(item, "value").is_null())
          own_bare.insert(
            json_string(json_member(json_member(item, "target"), "id")));
        else if(
          is_node_type(item, "FunctionDef") &&
          json_string(json_member(item, "name")) == "__init__")
        {
          has_init = true;
          const jsont &ib = json_member(item, "body");
          if(ib.is_array())
            for(const auto &s : as_array(ib))
            {
              const jsont *tgt = nullptr;
              if(
                is_node_type(s, "Assign") &&
                json_member(s, "targets").is_array() &&
                !as_array(json_member(s, "targets")).empty())
                tgt = &*as_array(json_member(s, "targets")).begin();
              else if(is_node_type(s, "AnnAssign"))
                tgt = &json_member(s, "target");
              if(
                tgt != nullptr && is_node_type(*tgt, "Attribute") &&
                is_node_type(json_member(*tgt, "value"), "Name") &&
                json_string(json_member(json_member(*tgt, "value"), "id")) ==
                  "self")
                own_def.insert(json_string(json_member(*tgt, "attr")));
              // super().__init__(...) call as an expression statement.
              const jsont &sv =
                is_node_type(s, "Expr") ? json_member(s, "value") : s;
              if(
                is_node_type(sv, "Call") &&
                is_node_type(json_member(sv, "func"), "Attribute") &&
                json_string(json_member(json_member(sv, "func"), "attr")) ==
                  "__init__")
              {
                const jsont &base_of =
                  json_member(json_member(sv, "func"), "value");
                if(
                  is_node_type(base_of, "Call") &&
                  is_node_type(json_member(base_of, "func"), "Name") &&
                  json_string(
                    json_member(json_member(base_of, "func"), "id")) == "super")
                  calls_super = true;
              }
            }
        }
      }
    // A @dataclass synthesizes an __init__ that assigns every annotated field,
    // so those bare class-body annotations ARE constructor-assigned and must
    // NOT be flagged as unassigned-field AttributeErrors (a frequent precision
    // false alarm: reading `cfg.host` on a @dataclass instance). Detect the
    // decorator in any of its spellings (@dataclass, @dataclass(...),
    // @dataclasses.dataclass[(...)]) and fold the bare annotations into the
    // constructor-assigned set.
    {
      const jsont &dl = json_member(stmt, "decorator_list");
      bool is_dataclass = false;
      if(dl.is_array())
        for(const auto &d : as_array(dl))
        {
          const jsont *node = &d;
          if(is_node_type(d, "Call"))
            node = &json_member(d, "func");
          std::string dn;
          if(is_node_type(*node, "Name"))
            dn = json_string(json_member(*node, "id"));
          else if(is_node_type(*node, "Attribute"))
            dn = json_string(json_member(*node, "attr"));
          if(dn == "dataclass")
            is_dataclass = true;
        }
      if(is_dataclass)
      {
        own_def.insert(own_bare.begin(), own_bare.end());
        // Record the fields in ANNOTATION ORDER so construction can bind
        // positional args to them (the synthesized __init__ signature). A
        // field with a default value (`x: int = 5` or `= field(...)`) is noted
        // so an omitted positional arg is left at its class-level default.
        // Only meaningful for a @dataclass WITHOUT an explicit __init__.
        if(!has_init)
        {
          std::vector<std::string> ordered;
          std::set<std::string> defaulted;
          if(body.is_array())
            for(const auto &item : as_array(body))
              if(
                is_node_type(item, "AnnAssign") &&
                is_node_type(json_member(item, "target"), "Name"))
              {
                std::string fn =
                  json_string(json_member(json_member(item, "target"), "id"));
                ordered.push_back(fn);
                if(!json_member(item, "value").is_null())
                  defaulted.insert(fn);
              }
          dataclass_init_fields[class_name] = ordered;
          dataclass_defaulted_fields[class_name] = defaulted;
        }
      }
    }
    std::set<std::string> all_bare = own_bare, ctor = own_def;
    bool inherit_ctor = calls_super || !has_init;
    if(bases.is_array())
      for(const auto &base : as_array(bases))
        if(is_node_type(base, "Name"))
        {
          std::string bn = json_string(json_member(base, "id"));
          auto bb = class_all_bare.find(bn);
          if(bb != class_all_bare.end())
            all_bare.insert(bb->second.begin(), bb->second.end());
          if(inherit_ctor)
          {
            auto bc = class_ctor_assigned.find(bn);
            if(bc != class_ctor_assigned.end())
              ctor.insert(bc->second.begin(), bc->second.end());
          }
        }
    class_all_bare[class_name] = all_bare;
    class_ctor_assigned[class_name] = ctor;
    for(const std::string &f : all_bare)
      if(ctor.count(f) == 0)
        class_attrerror_fields[class_name].insert(f);
  }

  // PLR §3.3.1: detect a __len__ that PROVABLY returns a negative integer
  // constant (body is a single `return <neg const>`); len() on such an instance
  // raises ValueError. Constant-only -> no false positive on a symbolic or
  // non-negative __len__.
  // PLR §4.4: classify instance truthiness from __bool__/__len__ (see
  // truthiness_kindt). __bool__ wins over __len__ (CPython). A single
  // constant return classifies definitively; anything else UNKNOWN.
  // Idempotent per class.
  if(body.is_array() && class_truthiness.count(class_name) == 0)
  {
    const jsont *bool_def = nullptr;
    const jsont *len_def = nullptr;
    for(const auto &item : as_array(body))
    {
      if(!is_node_type(item, "FunctionDef"))
        continue;
      const std::string mn = json_string(json_member(item, "name"));
      if(mn == "__bool__")
        bool_def = &item;
      else if(mn == "__len__")
        len_def = &item;
    }
    const jsont *decider = bool_def != nullptr ? bool_def : len_def;
    if(decider != nullptr)
    {
      truthiness_kindt k = truthiness_kindt::UNKNOWN;
      const jsont &db = json_member(*decider, "body");
      // Single-statement `return <constant>` body (docstrings allowed
      // before it) -- the common stub shape.
      if(db.is_array())
      {
        const jsont *ret = nullptr;
        bool other_stmt = false;
        for(const auto &st : as_array(db))
        {
          if(is_node_type(st, "Return"))
          {
            if(ret != nullptr)
              other_stmt = true;
            ret = &st;
          }
          else if(!(is_node_type(st, "Expr") &&
                    is_node_type(json_member(st, "value"), "Constant")))
            other_stmt = true;
        }
        if(ret != nullptr && !other_stmt)
        {
          const jsont &rv = json_member(*ret, "value");
          if(is_node_type(rv, "Constant"))
          {
            const jsont &cv = json_member(rv, "value");
            if(cv.is_true())
              k = truthiness_kindt::TRUTHY;
            else if(cv.is_false())
              k = truthiness_kindt::FALSY;
            else if(cv.is_number())
              k = (cv.value == "0" || cv.value == "0.0")
                    ? truthiness_kindt::FALSY
                    : truthiness_kindt::TRUTHY;
          }
        }
      }
      class_truthiness[class_name] = k;
    }
  }
  // PLR §3.3.1: classify the class's __iter__ against the iterator
  // protocol (see classify_iter_protocol). Idempotent per class.
  if(body.is_array() && class_iter_protocol.count(class_name) == 0)
  {
    bool has_next_m = false;
    const jsont *iter_def = nullptr;
    for(const auto &item : as_array(body))
    {
      if(!is_node_type(item, "FunctionDef"))
        continue;
      const std::string mn = json_string(json_member(item, "name"));
      if(mn == "__next__")
        has_next_m = true;
      else if(mn == "__iter__")
        iter_def = &item;
    }
    if(iter_def != nullptr)
      classify_iter_protocol(class_name, *iter_def, has_next_m);
  }

  // Dispatcher-summary registration for METHODS (bound: param 0 is self).
  if(body.is_array())
    for(const auto &item : as_array(body))
      if(is_node_type(item, "FunctionDef"))
        register_dispatcher_summary(
          "python::" + class_name +
            "::" + json_string(json_member(item, "name")),
          item,
          /*first_param_index=*/1);
  if(body.is_array())
    for(const auto &item : as_array(body))
    {
      if(
        !is_node_type(item, "FunctionDef") ||
        json_string(json_member(item, "name")) != "__len__")
        continue;
      const jsont &lb = json_member(item, "body");
      if(!lb.is_array() || as_array(lb).size() != 1)
        continue;
      const jsont &s0 = *as_array(lb).begin();
      if(!is_node_type(s0, "Return"))
        continue;
      const jsont &rv = json_member(s0, "value");
      bool neg = false;
      if(is_node_type(rv, "Constant"))
      {
        const jsont &cv = json_member(rv, "value");
        if(cv.is_number() && !cv.value.empty() && cv.value[0] == '-')
          neg = true;
      }
      else if(
        is_node_type(rv, "UnaryOp") &&
        is_node_type(json_member(rv, "op"), "USub"))
      {
        const jsont &operand = json_member(rv, "operand");
        if(is_node_type(operand, "Constant"))
        {
          const jsont &cv = json_member(operand, "value");
          if(cv.is_number() && !cv.value.empty() && cv.value != "0")
            neg = true;
        }
      }
      if(neg)
        class_len_negative.insert(class_name);
    }
  if(!class_tag_ids.count(class_name))
    class_tag_ids[class_name] = static_cast<int>(class_tag_ids.size()) + 1;

  // Record base classes for isinstance checks
  if(bases.is_array())
  {
    for(const auto &base : as_array(bases))
    {
      if(is_node_type(base, "Name"))
        class_bases[class_name].push_back(json_string(json_member(base, "id")));
    }
  }

  // PLR §3.3.2.1: compute C3 linearization MRO for this class.
  // MRO(C) = [C] + merge(MRO(B1), MRO(B2), ..., [B1, B2, ...])
  // where merge picks the head of the first list that isn't in
  // the tail of any other list; repeat until all lists empty.
  //
  // Idempotent: convert_class_def is invoked multiple times
  // across the passes (pre-register, full register, method
  // body convert); compute MRO exactly once per class to
  // avoid accumulating duplicates from re-runs over the
  // append-based class_bases map.
  if(class_mro.count(class_name) == 0)
  {
    // Dedup class_bases[class_name] in case previous passes
    // over the same ClassDef duplicated entries.
    auto &bv = class_bases[class_name];
    std::vector<std::string> dedup;
    std::set<std::string> seen;
    for(const auto &b : bv)
    {
      if(seen.insert(b).second)
        dedup.push_back(b);
    }
    bv = dedup;
    std::vector<std::string> mro{class_name};
    std::vector<std::vector<std::string>> seqs;
    for(const auto &b : class_bases[class_name])
    {
      auto it = class_mro.find(b);
      if(it != class_mro.end())
        seqs.push_back(it->second);
      else
        seqs.push_back({b}); // unknown base — assume just itself
    }
    if(!class_bases[class_name].empty())
      seqs.push_back(class_bases[class_name]);
    // Merge
    bool progress = true;
    while(progress)
    {
      progress = false;
      // Strip any now-empty lists.
      seqs.erase(
        std::remove_if(
          seqs.begin(),
          seqs.end(),
          [](const std::vector<std::string> &v) { return v.empty(); }),
        seqs.end());
      if(seqs.empty())
        break;
      for(std::size_t i = 0; i < seqs.size(); ++i)
      {
        const std::string &head = seqs[i].front();
        // head must not appear in the tail of any other list.
        bool in_tail = false;
        for(std::size_t j = 0; j < seqs.size() && !in_tail; ++j)
        {
          if(i == j)
            continue;
          for(std::size_t k = 1; k < seqs[j].size(); ++k)
          {
            if(seqs[j][k] == head)
            {
              in_tail = true;
              break;
            }
          }
        }
        if(!in_tail)
        {
          // Copy head by value — the following erase loop may
          // invalidate seqs[i]'s iterators/references, making
          // the `head` reference dangling and causing subsequent
          // seqs[j].front() != head mis-comparisons.
          std::string head_val = head;
          mro.push_back(head_val);
          // Remove head from the front of every sequence.
          for(auto &s : seqs)
          {
            if(!s.empty() && s.front() == head_val)
              s.erase(s.begin());
          }
          progress = true;
          break;
        }
      }
    }
    class_mro[class_name] = std::move(mro);
  }

  // Register the class type in the symbol table
  irep_idt type_symbol_id{"python::class::" + class_name};
  if(symbol_table.lookup(type_symbol_id) == nullptr)
  {
    symbolt type_sym{type_symbol_id, class_type, "python"};
    type_sym.base_name = class_name;
    type_sym.is_type = true;
    type_sym.location = loc;
    symbol_table.add(type_sym);
  }

  // Create a class object symbol for ClassName.attr access
  irep_idt class_obj_id{"python::" + class_name};
  if(symbol_table.lookup(class_obj_id) == nullptr)
  {
    symbolt class_obj{class_obj_id, class_type, "python"};
    class_obj.base_name = class_name;
    class_obj.is_lvalue = true;
    class_obj.is_state_var = true;
    class_obj.is_static_lifetime = true;
    // Initialize with class-level attribute values
    exprt::operandst field_values;
    for(const auto &comp : class_type.components())
    {
      // Set __class_tag to this class's unique ID
      if(id2string(comp.get_name()) == "__class_tag")
      {
        field_values.push_back(
          from_integer(class_tag_ids[class_name], signedbv_typet{32}));
        continue;
      }
      exprt val = safe_zero(comp.type());
      // Look for the value in the class body
      if(body.is_array())
      {
        for(const auto &item : as_array(body))
        {
          if(is_node_type(item, "AnnAssign"))
          {
            const jsont &tgt = json_member(item, "target");
            if(
              is_node_type(tgt, "Name") &&
              json_string(json_member(tgt, "id")) == id2string(comp.get_name()))
            {
              const jsont &v = json_member(item, "value");
              if(!v.is_null())
                val = safe_typecast(convert_expression(v), comp.type());
            }
          }
          else if(is_node_type(item, "Assign"))
          {
            const jsont &targets = json_member(item, "targets");
            if(targets.is_array())
            {
              for(const auto &tgt : as_array(targets))
              {
                if(
                  is_node_type(tgt, "Name") &&
                  json_string(json_member(tgt, "id")) ==
                    id2string(comp.get_name()))
                {
                  exprt rv = convert_expression(json_member(item, "value"));
                  val = safe_typecast(rv, comp.type());
                }
              }
            }
          }
        }
      }
      // If still zero and attribute is inherited, copy from parent
      if(val == safe_zero(comp.type()) && class_bases.count(class_name))
      {
        for(const auto &base_name : class_bases[class_name])
        {
          irep_idt parent_id{"python::" + base_name};
          const symbolt *parent_sym = symbol_table.lookup(parent_id);
          if(parent_sym != nullptr && parent_sym->value.id() == ID_struct)
          {
            const auto &parent_st = to_struct_type(parent_sym->value.type());
            if(parent_st.has_component(comp.get_name()))
            {
              auto idx = parent_st.component_number(comp.get_name());
              if(idx < parent_sym->value.operands().size())
              {
                val = parent_sym->value.operands()[idx];
                if(val.type() != comp.type())
                  val = safe_typecast(val, comp.type());
              }
            }
          }
        }
      }
      field_values.push_back(val);
    }
    class_obj.value = struct_exprt{std::move(field_values), class_type};
    symbol_table.add(class_obj);
  }

  // Generate class object initialization in the module body
  const symbolt *cls_sym = symbol_table.lookup(class_obj_id);
  if(cls_sym != nullptr && !cls_sym->value.is_nil())
  {
    // This will be included in the module body via convert_module_body
    // since class defs are processed in the first pass but the init
    // needs to run at module load time
  }

  // Pre-pass: register the names of every method declared in
  // the class body, so that during method-body conversion a
  // forward-reference call (e.g. self.helper() inside __init__,
  // where helper appears later in the class body) is recognised
  // as a real method rather than a missing-method bug.
  if(body.is_array())
  {
    for(const auto &item : as_array(body))
    {
      if(
        is_node_type(item, "FunctionDef") ||
        is_node_type(item, "AsyncFunctionDef"))
      {
        class_declared_methods[class_name].insert(
          json_string(json_member(item, "name")));
      }
      // Inner classes also act as 'attributes' callable from
      // self.<Inner>(...). Stubs commonly have
      //   self.exceptions = self._Exceptions()
      // where _Exceptions is a nested ClassDef. Without this
      // entry, the constructor-call falls into the missing-
      // method path and emits a spurious attribute-error.
      else if(is_node_type(item, "ClassDef"))
      {
        class_declared_methods[class_name].insert(
          json_string(json_member(item, "name")));
      }
    }
    // PLR §3.3.1: a class whose OWN body defines __eq__ but neither a __hash__
    // method nor `__hash__ = None`/a __hash__ value has __hash__ implicitly set
    // to None -> its instances are UNHASHABLE. (`__hash__ = None` alone is also
    // unhashable.) Detected on the own body only (inherited __eq__/__hash__ keep
    // their own hashability).
    {
      bool own_eq = false, own_hash = false, own_hash_none = false;
      for(const auto &item : as_array(body))
      {
        if(
          is_node_type(item, "FunctionDef") ||
          is_node_type(item, "AsyncFunctionDef"))
        {
          const std::string mn = json_string(json_member(item, "name"));
          if(mn == "__eq__")
            own_eq = true;
          else if(mn == "__hash__")
            own_hash = true;
        }
        else if(is_node_type(item, "Assign"))
        {
          for(const auto &tgt : as_array(json_member(item, "targets")))
            if(
              is_node_type(tgt, "Name") &&
              json_string(json_member(tgt, "id")) == "__hash__")
            {
              own_hash = true;
              const jsont &v = json_member(item, "value");
              if(
                (is_node_type(v, "Constant") &&
                 json_member(v, "value").is_null()) ||
                (is_node_type(v, "Name") &&
                 json_string(json_member(v, "id")) == "None"))
                own_hash_none = true;
            }
        }
      }
      if((own_eq && !own_hash) || own_hash_none)
        class_eq_without_hash.insert(class_name);
    }
    // PLR §3.3.2.4: record a `__slots__ = (...)` declaration (the permitted
    // instance-attribute names). Only a tuple/list/set of string CONSTANTS is
    // recognised; a single string `__slots__ = "x"` names one slot. A dynamic
    // __slots__ (a name / comprehension) is not modelled -> the class is left
    // without a slots record (so no enforcement, no false positive).
    for(const auto &item : as_array(body))
    {
      if(!is_node_type(item, "Assign"))
        continue;
      bool is_slots_target = false;
      for(const auto &tgt : as_array(json_member(item, "targets")))
        if(
          is_node_type(tgt, "Name") &&
          json_string(json_member(tgt, "id")) == "__slots__")
          is_slots_target = true;
      if(!is_slots_target)
        continue;
      const jsont &v = json_member(item, "value");
      std::set<std::string> names;
      bool ok = true;
      auto add_str = [&](const jsont &e)
      {
        if(is_node_type(e, "Constant") && json_member(e, "value").is_string())
          names.insert(json_string(json_member(e, "value")));
        else
          ok = false;
      };
      if(is_node_type(v, "Constant") && json_member(v, "value").is_string())
        names.insert(json_string(json_member(v, "value")));
      else if(
        is_node_type(v, "Tuple") || is_node_type(v, "List") ||
        is_node_type(v, "Set"))
      {
        const jsont &elts = json_member(v, "elts");
        if(elts.is_array())
          for(const auto &e : as_array(elts))
            add_str(e);
        else
          ok = false;
      }
      else
        ok = false;
      if(ok)
        class_slots[class_name] = std::move(names);
    }
    // Also include methods inherited from base classes —
    // a subclass calling self.parent_method() shouldn't
    // trip the missing-method detector when the method is
    // defined on a base.
    auto mro_it = class_mro.find(class_name);
    if(mro_it != class_mro.end())
    {
      for(const auto &base : mro_it->second)
      {
        if(base == class_name)
          continue;
        auto bi = class_declared_methods.find(base);
        if(bi != class_declared_methods.end())
          for(const auto &m : bi->second)
            class_declared_methods[class_name].insert(m);
      }
    }
  }

  // Now convert all methods
  if(body.is_array())
  {
    std::string saved_class = current_class;
    current_class = class_name;

    for(const auto &item : as_array(body))
    {
      if((is_node_type(item, "FunctionDef") ||
          is_node_type(item, "AsyncFunctionDef")))
      {
        std::string method_name = json_string(json_member(item, "name"));

        // PLR §3.3.2: a property accessor `@<prop>.setter` / `.getter` /
        // `.deleter` shares the method name with its getter (`def <prop>`).
        // Convert it under a DISTINCT symbol (`<prop>__<accessor>`) so it does
        // not clobber the getter (`python::<class>::<prop>`) -- without this a
        // class with both a getter and a setter had its getter silently
        // overwritten. Record setters so `obj.<prop> = v` dispatches the setter
        // (a data descriptor) instead of a shadowing field store.
        {
          const jsont &decs = json_member(item, "decorator_list");
          if(decs.is_array())
          {
            for(const auto &dec : as_array(decs))
            {
              if(!is_node_type(dec, "Attribute"))
                continue;
              const jsont &dval = json_member(dec, "value");
              if(!is_node_type(dval, "Name"))
                continue;
              const std::string acc = json_string(json_member(dec, "attr"));
              const std::string prop = json_string(json_member(dval, "id"));
              if(acc != "setter" && acc != "getter" && acc != "deleter")
                continue;
              if(prop == method_name)
                method_name = method_name + "__" + acc;
              if(acc == "setter")
                class_property_setters[class_name][prop] =
                  "python::" + class_name + "::" + method_name;
              break;
            }
          }
        }

        // PLR §8.7: Check for @classmethod/@staticmethod decorator
        bool is_classmethod = false;
        bool is_staticmethod = false;
        // Phase 7 of the icontract integration plan: collect
        // per-method @require / @ensure / @snapshot decorators
        // so class methods get the same treatment as top-level
        // functions. Phase 6 already handles class @invariant
        // separately; per-method contracts compose with the
        // class invariants below.
        std::vector<const jsont *> method_require_lambdas;
        std::vector<const jsont *> method_ensure_lambdas;
        std::vector<std::pair<std::string, const jsont *>> method_snapshots;
        const jsont &decorators = json_member(item, "decorator_list");
        if(decorators.is_array())
        {
          for(const auto &dec : as_array(decorators))
          {
            if(is_node_type(dec, "Name"))
            {
              std::string dname = json_string(json_member(dec, "id"));
              if(dname == "classmethod")
                is_classmethod = true;
              if(dname == "staticmethod")
                is_staticmethod = true;
              if(dname == "property")
              {
                // Record this method as a property of the class.
                std::string m = json_string(json_member(item, "name"));
                class_property_methods[class_name].insert(m);
              }
            }
            // icontract decorators (Call form):
            //   @icontract.X(lambda ...) or @X(lambda ...)
            if(is_node_type(dec, "Call"))
            {
              const jsont &dec_func = json_member(dec, "func");
              bool is_require = false;
              bool is_ensure = false;
              bool is_snapshot = false;
              if(is_node_type(dec_func, "Attribute"))
              {
                const jsont &v = json_member(dec_func, "value");
                if(
                  is_node_type(v, "Name") &&
                  json_string(json_member(v, "id")) == "icontract")
                {
                  std::string attr = json_string(json_member(dec_func, "attr"));
                  if(attr == "require")
                    is_require = true;
                  else if(attr == "ensure")
                    is_ensure = true;
                  else if(attr == "snapshot")
                    is_snapshot = true;
                }
              }
              else if(is_node_type(dec_func, "Name"))
              {
                std::string nm = json_string(json_member(dec_func, "id"));
                if(nm == "require")
                  is_require = true;
                else if(nm == "ensure")
                  is_ensure = true;
                else if(nm == "snapshot")
                  is_snapshot = true;
              }
              if(!is_require && !is_ensure && !is_snapshot)
                continue;
              const jsont &dec_args = json_member(dec, "args");
              if(!dec_args.is_array() || as_array(dec_args).empty())
                continue;
              const jsont &first = *as_array(dec_args).begin();
              if(!is_node_type(first, "Lambda"))
                continue;
              if(is_require)
                method_require_lambdas.push_back(&first);
              else if(is_ensure)
                method_ensure_lambdas.push_back(&first);
              else
              {
                std::string snap_name;
                const jsont &dec_kws = json_member(dec, "keywords");
                if(dec_kws.is_array())
                {
                  for(const auto &kw : as_array(dec_kws))
                  {
                    if(json_string(json_member(kw, "arg")) != "name")
                      continue;
                    const jsont &val = json_member(kw, "value");
                    if(is_node_type(val, "Constant"))
                    {
                      const jsont &v = json_member(val, "value");
                      if(v.is_string())
                        snap_name = v.value;
                    }
                  }
                }
                if(!snap_name.empty())
                  method_snapshots.emplace_back(std::move(snap_name), &first);
              }
            }
          }
        }
        // Phase 7 (Liskov inheritance composition): record this
        // method's own contracts so subclasses overriding the
        // method can compose with them.
        if(!method_require_lambdas.empty())
          class_method_require_lambdas[class_name][method_name] =
            method_require_lambdas;
        if(!method_ensure_lambdas.empty())
          class_method_ensure_lambdas[class_name][method_name] =
            method_ensure_lambdas;

        // Build method with class-qualified name
        const jsont &args_node = json_member(item, "args");
        const jsont &params = json_member(args_node, "args");

        code_typet::parameterst parameters;

        // Helper: add a (pos-only or regular) parameter to this method.
        auto add_method_param = [&](const jsont &param)
        {
          std::string param_name = json_string(json_member(param, "arg"));
          // For @staticmethod, the first parameter is an ordinary
          // parameter, not 'self' — we still bind it.
          // For @classmethod, bind 'cls' so the body can reference it.
          // (Previously we skipped it, which caused 'Unknown variable:
          // cls' whenever the body actually used it.)
          typet param_type;
          if(param_name == "self" && !is_staticmethod)
            param_type = pointer_typet{class_type, config.ansi_c.pointer_width};
          else if(param_name == "cls" && is_classmethod)
            param_type = pointer_typet{class_type, config.ansi_c.pointer_width};
          else
          {
            const jsont &annotation = json_member(param, "annotation");
            param_type = annotation.is_null()
                           ? python_value_type()
                           : convert_type_annotation(annotation);
            // PLR §3.1: genuine class instances are passed by
            // reference (matching the free-function path) so mutations
            // to the parameter propagate to the caller. TypedDict
            // classes are excluded: they are passed as dict literals at
            // call sites, not by reference, so pointer-wrapping them
            // breaks the call (the dict value is not addressable).
            if(
              param_type.id() == ID_struct &&
              id2string(to_struct_type(param_type).get_tag())
                  .find("python_class_") != std::string::npos)
            {
              std::string cn =
                id2string(to_struct_type(param_type).get_tag()).substr(13);
              auto bit = class_bases.find(cn);
              bool is_typeddict =
                bit != class_bases.end() &&
                std::find(
                  bit->second.begin(), bit->second.end(), "TypedDict") !=
                  bit->second.end();
              if(!is_typeddict)
                param_type = pointer_type(param_type);
            }
            else if(
              is_python_list_type(param_type) ||
              is_python_dict_type(param_type))
              // PLR §3.1: list / dict parameters are passed by
              // reference. The safe_typecast boundary promotes
              // value-widened element components and copies mutations
              // back; for a dict whose KEY type genuinely differs from
              // the argument (not a value-widening) it falls back to a
              // by-value temp, so this is sound and never regresses the
              // by-value behaviour. (set is a bitmap struct, not
              // array-shaped, so it is not wrapped here.)
              param_type = pointer_type(param_type);
          }

          code_typet::parametert p{param_type};
          p.set_identifier(
            "python::" + class_name + "::" + method_name + "::" + param_name);
          p.set_base_name(param_name);
          parameters.push_back(p);
          // Annotation provenance (mirrors convert_function_def): record a
          // method parameter whose type came from a GENUINE annotation, so the
          // call-site annotation/tag checks fire for it but NOT for inferred /
          // `*args` / `self` params. Without this, method params were absent
          // from explicitly_annotated_params and the provenance-gated method-
          // call check skipped EVERY method argument (missing real mismatches
          // like `calc.multiply(5, "ten")`).
          if(!json_member(param, "annotation").is_null())
            explicitly_annotated_params.insert(p.get_identifier());
          // PLR §6.10.3: mark Optional / Union[..., None] params
          // so the Is/IsNot fast-path treats them as nullable.
          if(!is_staticmethod || param_name != "self")
          {
            const jsont &ann = json_member(param, "annotation");
            if(!ann.is_null() && annotation_includes_none(ann))
              optional_params.insert(p.get_identifier());
          }
        };

        // PLR §8.7: positional-only parameters.
        const jsont &posonlyargs_m = json_member(args_node, "posonlyargs");
        if(posonlyargs_m.is_array())
        {
          for(const auto &param : as_array(posonlyargs_m))
            add_method_param(param);
        }
        if(params.is_array())
        {
          for(const auto &param : as_array(params))
            add_method_param(param);
        }

        // Bound-method call-site signature metadata (mirrors the
        // free-function path). max positional incl. self, captured
        // before *args / kwonly / **kwargs. Only for an undecorated
        // method (an exact signature) — staticmethod / classmethod /
        // property / custom decorators are excluded, so the implicit
        // self always accounts for exactly one positional slot.
        const irep_idt method_key{"python::" + class_name + "::" + method_name};
        const jsont &m_decorators = json_member(item, "decorator_list");
        const bool method_exact =
          !(m_decorators.is_array() && !as_array(m_decorators).empty());
        if(method_exact)
        {
          function_signature_checkable.insert(method_key);
          function_max_positional[method_key] = parameters.size();
          // Required positional-or-keyword params (incl. self) = all
          // minus the trailing defaulted ones (mirrors the
          // free-function path) — enables the missing-required /
          // multiple-values checks for method calls.
          const jsont &m_defaults = json_member(args_node, "defaults");
          std::size_t m_ndef =
            m_defaults.is_array() ? as_array(m_defaults).size() : 0;
          function_required_positional[method_key] =
            parameters.size() > m_ndef ? parameters.size() - m_ndef : 0;
          // Required keyword-only params (no kw_default) for methods.
          const jsont &m_kwonly = json_member(args_node, "kwonlyargs");
          const jsont &m_kwdef = json_member(args_node, "kw_defaults");
          if(m_kwonly.is_array())
          {
            std::size_t ki = 0;
            for(const auto &ka : as_array(m_kwonly))
            {
              bool has_default = false;
              if(m_kwdef.is_array())
              {
                std::size_t j = 0;
                for(const auto &d : as_array(m_kwdef))
                {
                  if(j == ki)
                  {
                    has_default = !d.is_null();
                    break;
                  }
                  ++j;
                }
              }
              if(!has_default)
                function_required_kwonly[method_key].insert(
                  json_string(json_member(ka, "arg")));
              ++ki;
            }
          }
        }

        // *args for class methods
        const jsont &vararg_m = json_member(args_node, "vararg");
        if(!vararg_m.is_null())
        {
          std::string va_name = json_string(json_member(vararg_m, "arg"));
          typet va_type = python_list_type(python_value_type());
          code_typet::parametert p{va_type};
          p.set_identifier(
            "python::" + class_name + "::" + method_name + "::" + va_name);
          p.set_base_name(va_name);
          function_vararg_index[method_key] = parameters.size();
          parameters.push_back(p);
        }

        // kwonlyargs and **kwargs for class methods
        const jsont &kwonlyargs_m = json_member(args_node, "kwonlyargs");
        if(kwonlyargs_m.is_array())
        {
          for(const auto &param : as_array(kwonlyargs_m))
          {
            std::string pn = json_string(json_member(param, "arg"));
            const jsont &ann = json_member(param, "annotation");
            // PLR §8.7: kwonly param without annotation defaults
            // to the tagged-union (Any), matching positional
            // params. Default of int previously typecast strings
            // to int and zeroed struct contents at the call.
            typet pt = ann.is_null() ? python_value_type()
                                     : convert_type_annotation(ann);
            code_typet::parametert p{pt};
            p.set_identifier(
              "python::" + class_name + "::" + method_name + "::" + pn);
            p.set_base_name(pn);
            parameters.push_back(p);
          }
        }
        const jsont &kwarg_m = json_member(args_node, "kwarg");
        if(!kwarg_m.is_null())
        {
          std::string kw_name = json_string(json_member(kwarg_m, "arg"));
          function_has_kwargs.insert(method_key);
          typet kw_type =
            python_dict_type(python_string_type(), python_value_type());
          code_typet::parametert p{kw_type};
          p.set_identifier(
            "python::" + class_name + "::" + method_name + "::" + kw_name);
          p.set_base_name(kw_name);
          parameters.push_back(p);
        }

        const jsont &returns = json_member(item, "returns");
        typet return_type = returns.is_null()
                              ? python_int_type()
                              : convert_type_annotation(returns);

        // NoneType → void
        if(
          !returns.is_null() &&
          json_string(json_member(returns, "id")) == "None")
          return_type = empty_typet{};

        // __init__ always returns void (it modifies self through pointer)
        if(method_name == "__init__")
          return_type = empty_typet{};

        // No annotation: infer from the body via the shared scanner
        // (the same one convert_function_def uses for free functions),
        // which covers class-instance / self / dict / list / tuple
        // returns and generator yields.
        bool method_is_generator = false;
        if(returns.is_null() && method_name != "__init__")
        {
          inferred_returnt inf = infer_return_type_from_body(
            json_member(item, "body"),
            parameters,
            class_name + "::" + method_name,
            class_name);
          if(inf.has_yield)
          {
            // PLR §6.2.9: generator method returns an eager list; the
            // yield-append / return rewrites key on current_function.
            return_type = python_list_type(inf.yield_element_type);
            generator_functions.insert(class_name + "::" + method_name);
            method_is_generator = true;
          }
          else if(inf.type.id() != ID_empty)
            return_type = inf.type;
        }

        code_typet func_type{parameters, return_type};
        irep_idt func_id{"python::" + class_name + "::" + method_name};

        if(symbol_table.lookup(func_id) == nullptr)
        {
          symbolt func_sym{func_id, func_type, "python"};
          func_sym.base_name = method_name;
          func_sym.location = get_location(item);
          func_sym.is_lvalue = true;
          symbol_table.add(func_sym);
        }
        else
        {
          // Idempotent re-call (sub-pass 1a-bis): the existing
          // symbol may have been registered with a placeholder
          // return type (e.g. when 'Bar' was only known as a
          // pre-registered placeholder during the first 1a
          // pass). Update the type now that all classes have
          // been fully registered.
          symbol_table.get_writeable_ref(func_id).type = func_type;
        }

        // Create parameter symbols
        for(const auto &p : parameters)
        {
          if(symbol_table.lookup(p.get_identifier()) == nullptr)
          {
            symbolt param_sym{p.get_identifier(), p.type(), "python"};
            param_sym.base_name = p.get_base_name();
            param_sym.location = loc;
            param_sym.is_lvalue = true;
            param_sym.is_state_var = true;
            param_sym.is_parameter = true;
            symbol_table.add(param_sym);
          }
          else
          {
            // Idempotent re-call: refresh type if it changed
            // (placeholder class -> full class struct).
            symbol_table.get_writeable_ref(p.get_identifier()).type = p.type();
          }
        }

        // Convert method body
        {
          std::string saved_func = current_function;
          if(!current_function.empty())
            enclosing_functions.push_back(current_function);
          current_function = class_name + "::" + method_name;

          // PLR §8.7: register class method defaults under the
          // bare method name. The function-call default-fill
          // path (call_method.cpp / call_user.cpp) keys lookups
          // by method_name so this lets f.foo(a='x') pick up
          // 'b="xyz"' rather than safe_zero'ing the slot.
          {
            const jsont &m_args_node = json_member(item, "args");
            const jsont &m_defaults = json_member(m_args_node, "defaults");
            if(m_defaults.is_array() && !as_array(m_defaults).empty())
            {
              std::size_t mn_defaults = as_array(m_defaults).size();
              std::size_t mfirst_default = parameters.size() - mn_defaults;
              auto m_def_it = as_array(m_defaults).begin();
              for(std::size_t i = mfirst_default; i < parameters.size();
                  i++, ++m_def_it)
              {
                exprt val = convert_expression(*m_def_it);
                if(val.is_nil())
                  continue;
                if(val.type().id() == ID_pointer)
                  continue;
                if(val.type().id() == ID_struct)
                {
                  std::string tag =
                    id2string(to_struct_type(val.type()).get_tag());
                  if(
                    tag != "python_string" && tag != "python_tuple" &&
                    tag != "python_value" && tag != "python_set" &&
                    tag != "python_complex" && tag != "python_list")
                    continue;
                }
                // PLR §8.7 mutable-default gotcha: a mutable (list/dict/set)
                // default is evaluated ONCE at def time and SHARED across all
                // calls, so mutations through it accumulate. Freeze it into a
                // static-lifetime symbol (once per class::method::index) and
                // queue a once-only initialiser for the module-init block, so
                // the shared state persists. Without this each call got a fresh
                // copy and the accumulation was lost (a10_mutable_default).
                // Immutable defaults keep the raw value.
                const bool mutable_default = is_python_list_type(val.type()) ||
                                             is_python_dict_type(val.type()) ||
                                             is_python_set_type(val.type());
                if(mutable_default)
                {
                  const std::string fkey =
                    class_name + "::" + method_name + "::" + std::to_string(i);
                  std::string tn = "__mdef_" + class_name + "_" + method_name +
                                   "_" + std::to_string(i);
                  irep_idt ti{qualify_name(tn)};
                  if(frozen_method_defaults.insert(fkey).second)
                  {
                    if(symbol_table.lookup(ti) == nullptr)
                    {
                      symbolt ts{ti, val.type(), "python"};
                      ts.base_name = tn;
                      ts.is_lvalue = true;
                      ts.is_state_var = true;
                      ts.is_static_lifetime = true;
                      ts.value = val;
                      symbol_table.add(ts);
                    }
                    deferred_static_default_inits.push_back(
                      code_frontend_assignt{
                        symbol_table.lookup_ref(ti).symbol_expr(), val});
                  }
                  default_values[{method_name, i}] =
                    symbol_table.lookup_ref(ti).symbol_expr();
                }
                else
                  default_values[{method_name, i}] = val;
              }
            }
          }

          code_blockt method_body;

          // PLR §6.2.9: initialise the generator's eager result list
          // at method entry (before any yield in the body).
          irep_idt method_gen_result_id;
          if(method_is_generator)
            method_gen_result_id = setup_generator_result(
              class_name + "::" + method_name, return_type, method_body);

          // Phase 6 of the icontract integration plan:
          // translate each class-level @icontract.invariant
          // lambda in the method's scope (so Name("self")
          // resolves to the method's self parameter), then
          // (a) prepend the assertions at method entry — but
          // skip __init__ since the object's fields haven't
          // been initialised yet, and (b) collect the
          // assertions for the post-walk that fires before
          // every return + at the end of method_body for
          // fall-through. Skipped for staticmethod/classmethod
          // since they don't operate on a self instance.
          std::vector<exprt> invariant_assertions;
          if(
            !icontract_invariant_lambdas.empty() && !is_staticmethod &&
            !is_classmethod)
          {
            for(const jsont *lam : icontract_invariant_lambdas)
            {
              const jsont &lam_body = json_member(*lam, "body");
              exprt cond;
              try
              {
                cond = convert_expression(lam_body);
              }
              catch(...)
              {
                cond = nil_exprt{};
              }
              if(cond.is_nil() || cond.type().id() == ID_empty)
                continue;
              if(cond.type().id() != ID_bool)
                cond = typecast_exprt{cond, bool_typet{}};
              invariant_assertions.push_back(cond);
            }
            if(method_name != "__init__")
            {
              for(const exprt &cond : invariant_assertions)
                method_body.add(code_assertt{cond});
            }
          }

          // Phase 7 of the icontract integration plan: translate
          // per-method @require / @snapshot / @ensure lambdas
          // (collected in the decorator inspection above) within
          // the method's scope so Name lookups resolve to method
          // parameters and self. require lambdas are emitted as
          // entry assumes; snapshots are emitted as entry-state
          // captures stored in synthesised __icontract_old_<name>
          // symbols and exposed via active_old_snapshots; ensure
          // lambdas are translated into a per-method
          // method_ensure_assertions vector that the
          // post-process walk further down injects before each
          // return + at the implicit fall-through.
          //
          // Phase 7 (Liskov inheritance composition): collect
          // inherited contracts from direct base classes here
          // FIRST so the result-symbol decision below knows
          // whether ANY ensures (own or inherited) will be
          // emitted. Walks direct bases left-to-right and
          // looks up class_method_*_lambdas for the same
          // method name.
          //
          // Walks the C3 MRO (skipping the class itself) so
          // grandparent and farther-ancestor contracts are
          // included transitively. Each ancestor's contracts
          // are added in MRO order; the OR/AND composition
          // below treats the union as a single weakening
          // (preconditions) or strengthening (postconditions)
          // step against the child's own.
          std::vector<const jsont *> inherited_requires;
          std::vector<const jsont *> inherited_ensures;
          {
            auto mro_lookup = class_mro.find(class_name);
            if(mro_lookup != class_mro.end())
            {
              for(std::size_t mi = 1; mi < mro_lookup->second.size(); ++mi)
              {
                const std::string &ancestor = mro_lookup->second[mi];
                auto rit = class_method_require_lambdas.find(ancestor);
                if(rit != class_method_require_lambdas.end())
                {
                  auto mit = rit->second.find(method_name);
                  if(mit != rit->second.end())
                    for(const jsont *lam : mit->second)
                      inherited_requires.push_back(lam);
                }
                auto eit = class_method_ensure_lambdas.find(ancestor);
                if(eit != class_method_ensure_lambdas.end())
                {
                  auto mit = eit->second.find(method_name);
                  if(mit != eit->second.end())
                    for(const jsont *lam : mit->second)
                      inherited_ensures.push_back(lam);
                }
              }
            }
          }
          irep_idt method_result_symbol_id;
          if(!method_ensure_lambdas.empty() || !inherited_ensures.empty())
          {
            typet rsym_type =
              return_type.id() == ID_empty ? python_int_type() : return_type;
            std::string rsym_name =
              "python::" + class_name + "::" + method_name + "::result";
            method_result_symbol_id = irep_idt{rsym_name};
            if(!symbol_table.has_symbol(method_result_symbol_id))
            {
              symbolt rsym{method_result_symbol_id, rsym_type, "python"};
              rsym.base_name = "result";
              rsym.is_lvalue = true;
              rsym.is_state_var = true;
              symbol_table.add(rsym);
            }
          }
          // Phase 7 (Liskov inheritance composition): if this
          // method overrides a base-class method, compose the
          // preconditions weakening (parent_pre OR child_pre)
          // and the postconditions strengthening (parent_post
          // AND child_post). Walk direct bases left-to-right
          // and look up class_method_*_lambdas for the same
          // method name.
          //
          // Translates each lambda body in the method's scope
          // (so `self` resolves to the current method's self
          // pointer; the lambda body's references to fields
          // resolve via the inherited struct layout). For
          // multiple parent bases we OR/AND across all of them
          // — common in mixin scenarios.
          //
          // This is single-level inheritance only. For
          // grandparent contracts, the parent's recorded set
          // already reflects ITS own contracts only (not its
          // own ancestors'); a follow-up commit can extend this
          // to walk transitively.
          // (inherited_requires / inherited_ensures already
          // populated above, before result-symbol creation.)
          // Helper: translate a list of lambdas into a list of
          // bool exprts in the current scope.
          auto translate_lambdas =
            [&](const std::vector<const jsont *> &lams) -> std::vector<exprt>
          {
            std::vector<exprt> out;
            for(const jsont *lam : lams)
            {
              const jsont &lb = json_member(*lam, "body");
              exprt c;
              try
              {
                c = convert_expression(lb);
              }
              catch(...)
              {
                c = nil_exprt{};
              }
              if(c.is_nil() || c.type().id() == ID_empty)
                continue;
              if(c.type().id() != ID_bool)
                c = typecast_exprt{c, bool_typet{}};
              out.push_back(c);
            }
            return out;
          };
          std::vector<exprt> own_require_exprs =
            translate_lambdas(method_require_lambdas);
          std::vector<exprt> parent_require_exprs =
            translate_lambdas(inherited_requires);
          // Compose effective precondition (Liskov weakening):
          //   if both own and parent: assume(AND(own) OR AND(parent))
          //   if only own: assume each
          //   if only parent: assume each
          if(!own_require_exprs.empty() && !parent_require_exprs.empty())
          {
            auto build_and = [&](const std::vector<exprt> &v) -> exprt
            {
              if(v.empty())
                return true_exprt{};
              if(v.size() == 1)
                return v.front();
              and_exprt::operandst ops;
              for(const exprt &e : v)
                ops.push_back(e);
              return and_exprt{ops};
            };
            exprt own_pre = build_and(own_require_exprs);
            exprt par_pre = build_and(parent_require_exprs);
            method_body.add(code_assumet{or_exprt{own_pre, par_pre}});
            if(symbol_table.has_symbol(func_id))
            {
              typet &t = symbol_table.get_writeable_ref(func_id).type;
              static_cast<exprt &>(t.add(ID_C_spec_requires))
                .operands()
                .push_back(or_exprt{own_pre, par_pre});
            }
          }
          else
          {
            for(const exprt &cond : own_require_exprs)
            {
              method_body.add(code_assumet{cond});
              if(symbol_table.has_symbol(func_id))
              {
                typet &t = symbol_table.get_writeable_ref(func_id).type;
                static_cast<exprt &>(t.add(ID_C_spec_requires))
                  .operands()
                  .push_back(cond);
              }
            }
            for(const exprt &cond : parent_require_exprs)
            {
              method_body.add(code_assumet{cond});
              if(symbol_table.has_symbol(func_id))
              {
                typet &t = symbol_table.get_writeable_ref(func_id).type;
                static_cast<exprt &>(t.add(ID_C_spec_requires))
                  .operands()
                  .push_back(cond);
              }
            }
          }
          // Translate snapshots and emit capture assignments.
          std::map<std::string, exprt> saved_method_old_snapshots;
          saved_method_old_snapshots.swap(active_old_snapshots);
          for(const auto &snap : method_snapshots)
          {
            const std::string &name = snap.first;
            const jsont &lam = *snap.second;
            const jsont &lam_body = json_member(lam, "body");
            exprt cap;
            try
            {
              cap = convert_expression(lam_body);
            }
            catch(...)
            {
              cap = nil_exprt{};
            }
            if(cap.is_nil() || cap.type().id() == ID_empty)
              continue;
            std::string old_id_str = "python::" + class_name +
                                     "::" + method_name + "::__icontract_old_" +
                                     name;
            irep_idt old_id{old_id_str};
            if(!symbol_table.has_symbol(old_id))
            {
              symbolt old_sym{old_id, cap.type(), "python"};
              old_sym.base_name = "__icontract_old_" + name;
              old_sym.is_lvalue = true;
              old_sym.is_state_var = true;
              symbol_table.add(old_sym);
            }
            const symbolt &old_sym = symbol_table.lookup_ref(old_id);
            method_body.add(code_frontend_assignt{old_sym.symbol_expr(), cap});
            active_old_snapshots[name] = old_sym.symbol_expr();
          }
          // Translate ensure lambdas (with `result` and OLD.NAME
          // bindings now in scope).
          std::vector<exprt> method_ensure_assertions;
          for(const jsont *lam : method_ensure_lambdas)
          {
            const jsont &lam_body = json_member(*lam, "body");
            exprt cond;
            try
            {
              cond = convert_expression(lam_body);
            }
            catch(...)
            {
              cond = nil_exprt{};
            }
            if(cond.is_nil() || cond.type().id() == ID_empty)
              continue;
            if(cond.type().id() != ID_bool)
              cond = typecast_exprt{cond, bool_typet{}};
            method_ensure_assertions.push_back(cond);
            if(symbol_table.has_symbol(func_id))
            {
              typet &t = symbol_table.get_writeable_ref(func_id).type;
              static_cast<exprt &>(t.add(ID_C_spec_ensures))
                .operands()
                .push_back(cond);
            }
          }
          // Phase 7 Liskov: parent's postconditions are
          // strengthened (AND-composed) with child's. We
          // simply translate inherited_ensures into the same
          // method_ensure_assertions list — the post-process
          // walk asserts each in turn, which is equivalent to
          // their conjunction.
          for(const jsont *lam : inherited_ensures)
          {
            const jsont &lam_body = json_member(*lam, "body");
            exprt cond;
            try
            {
              cond = convert_expression(lam_body);
            }
            catch(...)
            {
              cond = nil_exprt{};
            }
            if(cond.is_nil() || cond.type().id() == ID_empty)
              continue;
            if(cond.type().id() != ID_bool)
              cond = typecast_exprt{cond, bool_typet{}};
            method_ensure_assertions.push_back(cond);
            if(symbol_table.has_symbol(func_id))
            {
              typet &t = symbol_table.get_writeable_ref(func_id).type;
              static_cast<exprt &>(t.add(ID_C_spec_ensures))
                .operands()
                .push_back(cond);
            }
          }
          // Under --python-lazy-stubs AND while processing an
          // imported module, skip the method body. The stub
          // becomes a pure type surface: method signature only,
          // returns nondet. This avoids the cascaded assertion
          // failures and string-refinement-solver blow-ups
          // that boto3 / similar stubs cause when their dense
          // regex preconditions interact with nondet kwargs.
          bool skip_body = python_lazy_stubs && processing_import;

          // Auto-detect PySpec-style typed stubs: a method with
          // 'kwargs: Unpack[TypedDict]' is almost certainly a
          // generated stub whose body is precondition
          // assertions on kwargs. When the caller passes known
          // constants these are easy for the solver; when the
          // caller passes nondet values (common for imports
          // transiently loaded from stub modules), the dense
          // regex preconditions interact badly with the
          // string refinement loop. Skip such bodies when
          // processing imported modules.
          std::string unpack_td_name;
          std::string kwargs_param_name;
          if(processing_import && !skip_body)
          {
            const jsont &margs = json_member(item, "args");
            const jsont &kwarg = json_member(margs, "kwarg");
            if(!kwarg.is_null())
            {
              kwargs_param_name = json_string(json_member(kwarg, "arg"));
              const jsont &ann = json_member(kwarg, "annotation");
              if(is_node_type(ann, "Subscript"))
              {
                const jsont &av = json_member(ann, "value");
                if(is_node_type(av, "Name"))
                {
                  std::string an = json_string(json_member(av, "id"));
                  if(an == "Unpack")
                  {
                    skip_body = true;
                    // Extract TypedDict name from Unpack's slice.
                    const jsont &slc = json_member(ann, "slice");
                    if(is_node_type(slc, "Name"))
                    {
                      unpack_td_name = json_string(json_member(slc, "id"));
                      // Persist the Unpack[TypedDict] mapping
                      // for the call site to consult when
                      // emitting field-type checks on values
                      // spread via PEP 448 **kwargs.
                      method_kwargs_unpack[func_id] = unpack_td_name;
                    }
                  }
                }
              }
            }
          }
          const jsont &method_body_json = json_member(item, "body");

          // Stage 1 of the re-precision plan: walk the method
          // body looking for regex-assertion patterns of the
          // shape
          //     assert compile("...").search(kwargs[K1][K2]...) is not None
          // and record the (kwarg_path, pattern) so user call
          // sites can emit a regex-no-match property when their
          // nested-Dict kwarg value resolves to an empty string.
          if(!kwargs_param_name.empty() && method_body_json.is_array())
          {
            std::function<void(const jsont &)> scan;
            scan = [&](const jsont &node)
            {
              if(!node.is_object() && !node.is_array())
                return;
              if(node.is_array())
              {
                for(const auto &c : as_array(node))
                  scan(c);
                return;
              }
              // Identify a Subscript chain rooted at
              // Name(kwargs_param_name).
              auto extract_kwarg_path =
                [&](const jsont &n) -> std::optional<std::vector<std::string>>
              {
                std::vector<std::string> path;
                const jsont *cur = &n;
                while(is_node_type(*cur, "Subscript"))
                {
                  const jsont &s_node = json_member(*cur, "slice");
                  if(!is_node_type(s_node, "Constant"))
                    return std::nullopt;
                  const jsont &k = json_member(s_node, "value");
                  if(!k.is_string())
                    return std::nullopt;
                  path.insert(path.begin(), k.value);
                  cur = &json_member(*cur, "value");
                }
                if(
                  is_node_type(*cur, "Name") &&
                  json_string(json_member(*cur, "id")) == kwargs_param_name &&
                  !path.empty())
                  return path;
                return std::nullopt;
              };
              // Look for Call(Attribute(value=<Compile>, attr=
              //   {search,match,fullmatch}), args=[<subj>])
              if(is_node_type(node, "Call"))
              {
                const jsont &fn = json_member(node, "func");
                const jsont &cargs = json_member(node, "args");
                if(
                  is_node_type(fn, "Attribute") && cargs.is_array() &&
                  !as_array(cargs).empty())
                {
                  std::string attr = json_string(json_member(fn, "attr"));
                  if(attr == "search" || attr == "match" || attr == "fullmatch")
                  {
                    const jsont &recv = json_member(fn, "value");
                    // Pattern recovery: receiver is Call to
                    // compile(LITERAL) [optionally re.compile].
                    std::optional<std::string> pat;
                    if(is_node_type(recv, "Call"))
                    {
                      const jsont &cf = json_member(recv, "func");
                      bool is_compile = false;
                      if(
                        is_node_type(cf, "Name") &&
                        json_string(json_member(cf, "id")) == "compile")
                        is_compile = true;
                      else if(
                        is_node_type(cf, "Attribute") &&
                        json_string(json_member(cf, "attr")) == "compile")
                      {
                        const jsont &cv = json_member(cf, "value");
                        if(
                          is_node_type(cv, "Name") &&
                          json_string(json_member(cv, "id")) == "re")
                          is_compile = true;
                      }
                      if(is_compile)
                      {
                        const jsont &cca = json_member(recv, "args");
                        if(cca.is_array() && !as_array(cca).empty())
                        {
                          const jsont &p0 = *as_array(cca).begin();
                          if(is_node_type(p0, "Constant"))
                          {
                            const jsont &v = json_member(p0, "value");
                            if(v.is_string())
                              pat = v.value;
                          }
                        }
                      }
                    }
                    if(pat.has_value())
                    {
                      const jsont &subj = *as_array(cargs).begin();
                      auto path = extract_kwarg_path(subj);
                      if(path.has_value())
                      {
                        method_regex_asserts[func_id].push_back(
                          stub_regex_assertt{
                            std::move(*path), std::move(*pat)});
                      }
                    }
                  }
                }
              }
              // Recurse into all sub-objects.
              if(node.is_object())
              {
                const auto &obj = static_cast<const json_objectt &>(node);
                for(const auto &kv : obj)
                  scan(kv.second);
              }
            };
            for(const auto &s : as_array(method_body_json))
              scan(s);
          }

          if(!skip_body && method_body_json.is_array())
          {
            // Run the function-body pre-scans for METHODS too (whole-group:
            // convert_function_def runs these for plain functions, but the
            // ClassDef method path converts inline and skipped them --
            // current_function is set above, so the scans key symbols with
            // the correct method scope). Without the empty-dict scan a
            // method-local `providers = {}` kept scalar value slots and a
            // later `providers[k] = []` PUNNED the list (bedrock's shape:
            // items() then bound the value as int and the not-iterable
            // obligation false-alarmed).
            collect_escaped_mutables(method_body_json);
            collect_empty_list_inferred_types(method_body_json);
            for(const auto &s : as_array(method_body_json))
              method_body.add(convert_statement(s));

            // PLR §6.2.9: a generator method falls off the end returning
            // its eager result list.
            if(method_is_generator)
              method_body.add(code_frontend_returnt{
                symbol_table.lookup_ref(method_gen_result_id).symbol_expr()});
          }

          // Phase 6 of the icontract integration plan: inject
          // invariant assertions before every return in the
          // method body, plus a final assertion at the end so
          // fall-through paths see it too. Phase 7 unifies the
          // class invariant + per-method ensure assertions into
          // a single walk: invariants come first (entry-vs-exit
          // semantics for the class), then ensures (postcondition
          // semantics for the method). When the method has
          // ensures clauses, we also bind `result` to the return
          // value before asserting so postconditions can
          // reference it.
          std::vector<exprt> exit_assertions = invariant_assertions;
          for(const exprt &cond : method_ensure_assertions)
            exit_assertions.push_back(cond);
          if(!exit_assertions.empty())
          {
            std::function<void(codet &)> inject_at_returns =
              [&](codet &c) -> void
            {
              for(auto &op : c.operands())
              {
                if(op.id() != ID_code)
                  continue;
                codet &inner = static_cast<codet &>(op);
                if(inner.get_statement() == ID_return)
                {
                  const code_frontend_returnt &ret =
                    static_cast<const code_frontend_returnt &>(inner);
                  code_blockt blk;
                  // Bind result symbol if there are ensures and
                  // the return carries a value.
                  if(
                    !method_ensure_assertions.empty() &&
                    ret.has_return_value() &&
                    !method_result_symbol_id.empty() &&
                    symbol_table.has_symbol(method_result_symbol_id))
                  {
                    const symbolt &rsym =
                      symbol_table.lookup_ref(method_result_symbol_id);
                    exprt rv = ret.return_value();
                    if(rv.type() != rsym.type)
                      rv = safe_typecast(rv, rsym.type);
                    blk.add(code_frontend_assignt{rsym.symbol_expr(), rv});
                  }
                  for(const exprt &cond : exit_assertions)
                    blk.add(code_assertt{cond});
                  blk.add(static_cast<const codet &>(inner));
                  op = std::move(blk);
                }
                else
                {
                  inject_at_returns(inner);
                }
              }
            };
            inject_at_returns(method_body);
            // Fall-through: append the assertions at the end. If
            // the last statement was a return, this is
            // unreachable (no harm). Otherwise it covers the
            // implicit return case.
            for(const exprt &cond : exit_assertions)
              method_body.add(code_assertt{cond});
          }
          // Restore the parent's @snapshot scope.
          active_old_snapshots.swap(saved_method_old_snapshots);

          // Tier 1B: selective precondition processing. When we
          // skip the stub body due to Unpack detection, emit
          // key-presence checks for each Required field of the
          // TypedDict. This catches 'missing required argument'
          // bugs without triggering the regex/length assertions
          // that overwhelm the string refinement solver.
          //
          // Opt-in via --python-required-kwarg-checks: callers
          // that use **dict spread don't currently populate
          // kwargs reliably, which produces FPs for spread-based
          // callers.
          if(
            skip_body && python_required_kwarg_checks &&
            !unpack_td_name.empty() && !kwargs_param_name.empty())
          {
            auto ri = typed_dict_required.find(unpack_td_name);
            if(ri != typed_dict_required.end() && !ri->second.empty())
            {
              // Resolve the kwargs parameter symbol (already
              // registered as python::<ClassName>::<method>::<name>).
              std::string kwargs_sym =
                "python::" + current_function + "::" + kwargs_param_name;
              const symbolt *ks = symbol_table.lookup(irep_idt{kwargs_sym});
              if(ks != nullptr && is_python_dict_type(ks->type))
              {
                const auto &dict_st = to_struct_type(ks->type);
                const auto &keys_type =
                  to_array_type(dict_st.components()[1].type());
                member_exprt length{
                  ks->symbol_expr(), "length", signedbv_typet{64}};
                member_exprt keys_arr{ks->symbol_expr(), "keys", keys_type};
                for(const auto &req_key : ri->second)
                {
                  // Build 'assert exists i. 0<=i<length &&
                  // keys[i] == req_key'. Same formula shape
                  // as the dict subscript's KeyError check.
                  exprt typed_req_key = python_string_literal(req_key);
                  if(typed_req_key.type() != keys_type.element_type())
                    typed_req_key =
                      coerce_element(typed_req_key, keys_type.element_type());
                  exprt found = false_exprt{};
                  for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
                  {
                    exprt idx = from_integer(i, signedbv_typet{64});
                    exprt in_range = binary_relation_exprt{idx, ID_lt, length};
                    exprt match = equal_exprt{
                      python_dict_unbox_key(index_exprt{keys_arr, idx}),
                      python_dict_unbox_key(typed_req_key)};
                    found = or_exprt{found, and_exprt{in_range, match}};
                  }
                  code_assertt req_assert{found};
                  source_locationt rloc;
                  rloc.set_file(filename);
                  rloc.set_property_class("required-kwarg");
                  rloc.set_comment(
                    "missing required keyword argument '" + req_key + "'");
                  req_assert.add_source_location() = rloc;
                  method_body.add(std::move(req_assert));
                }
              }
            }
          }

          current_function = saved_func;
          if(
            !enclosing_functions.empty() &&
            enclosing_functions.back() == saved_func)
            enclosing_functions.pop_back();

          symbolt *sym_ptr = symbol_table.get_writeable(func_id);
          if(sym_ptr != nullptr)
            sym_ptr->value = method_body;
        }
      }
    }

    // Phase 7 (icontract): when this class has invariants and
    // inherits methods that aren't overridden here, synthesise
    // wrapper methods that delegate to the parent's body but
    // assert this class's invariants at entry and exit. Without
    // wrappers, `child.parent_method()` reaches the parent body
    // via the MRO dispatch but the child's own invariants never
    // fire — a soundness gap when the child's invariant
    // constrains a field the parent's method mutates.
    //
    // For __init__ specifically we skip the entry assertion
    // (the object's fields haven't been initialised yet) but
    // still emit the exit assertion so the child sees the
    // post-construction state.
    //
    // The synthesised wrapper has the same signature as the
    // parent's method, but `self` becomes a pointer to the
    // child's struct type. The body casts self to the parent
    // type before the inherited call.
    if(!icontract_invariant_lambdas.empty())
    {
      auto mro_it = class_mro.find(class_name);
      if(mro_it != class_mro.end() && class_types.count(class_name) > 0)
      {
        const struct_typet &child_struct = class_types[class_name];
        // class_declared_methods[class_name] also includes
        // inherited names (populated by the missing-method
        // detector). For wrapper synthesis we need names
        // declared DIRECTLY on this class, so walk the body to
        // compute the own set.
        std::set<std::string> own_methods;
        for(const auto &item : as_array(body))
        {
          if(
            is_node_type(item, "FunctionDef") ||
            is_node_type(item, "AsyncFunctionDef"))
            own_methods.insert(json_string(json_member(item, "name")));
        }
        std::set<std::string> already_wrapped;
        for(std::size_t mi = 1; mi < mro_it->second.size(); ++mi)
        {
          const std::string &ancestor = mro_it->second[mi];
          auto adm_it = class_declared_methods.find(ancestor);
          if(adm_it == class_declared_methods.end())
            continue;
          for(const std::string &mname : adm_it->second)
          {
            if(own_methods.count(mname))
              continue;
            if(already_wrapped.count(mname))
              continue;
            already_wrapped.insert(mname);
            // staticmethod / property / classmethod methods need
            // bespoke handling — skip wrapping for now.
            if(class_property_methods[ancestor].count(mname))
              continue;
            irep_idt parent_method_id{"python::" + ancestor + "::" + mname};
            const symbolt *parent_sym = symbol_table.lookup(parent_method_id);
            if(parent_sym == nullptr || parent_sym->type.id() != ID_code)
              continue;
            const code_typet &parent_ct = to_code_type(parent_sym->type);
            const code_typet::parameterst &parent_params =
              parent_ct.parameters();
            if(parent_params.empty())
              continue; // no self -> not an instance method
            const typet &parent_self_t = parent_params[0].type();
            if(parent_self_t.id() != ID_pointer)
              continue;
            // Build wrapper signature with self as ptr-to-child.
            code_typet::parameterst wrapper_params;
            std::vector<symbol_exprt> wrapper_param_exprs;
            std::string wrapper_qfn = class_name + "::" + mname;
            // self comes first.
            {
              typet child_self_ptr =
                pointer_typet{child_struct, config.ansi_c.pointer_width};
              code_typet::parametert p{child_self_ptr};
              std::string id_str = "python::" + wrapper_qfn + "::self";
              p.set_identifier(id_str);
              p.set_base_name("self");
              wrapper_params.push_back(p);
              irep_idt sid{id_str};
              if(!symbol_table.has_symbol(sid))
              {
                symbolt s{sid, child_self_ptr, "python"};
                s.base_name = "self";
                s.location = loc;
                s.is_lvalue = true;
                s.is_state_var = true;
                s.is_parameter = true;
                symbol_table.add(s);
              }
              wrapper_param_exprs.push_back(
                symbol_table.lookup_ref(sid).symbol_expr());
            }
            // Remaining params: copy from parent's signature with
            // wrapper-scoped identifiers.
            for(std::size_t pi = 1; pi < parent_params.size(); ++pi)
            {
              const auto &pp = parent_params[pi];
              std::string pname = id2string(pp.get_base_name());
              code_typet::parametert wp{pp.type()};
              std::string id_str = "python::" + wrapper_qfn + "::" + pname;
              wp.set_identifier(id_str);
              wp.set_base_name(pname);
              wrapper_params.push_back(wp);
              irep_idt sid{id_str};
              if(!symbol_table.has_symbol(sid))
              {
                symbolt s{sid, pp.type(), "python"};
                s.base_name = pname;
                s.location = loc;
                s.is_lvalue = true;
                s.is_state_var = true;
                s.is_parameter = true;
                symbol_table.add(s);
              }
              wrapper_param_exprs.push_back(
                symbol_table.lookup_ref(sid).symbol_expr());
            }
            typet wrapper_return = parent_ct.return_type();
            code_typet wrapper_type{wrapper_params, wrapper_return};
            irep_idt wrapper_func_id{"python::" + wrapper_qfn};
            if(!symbol_table.has_symbol(wrapper_func_id))
            {
              symbolt fsym{wrapper_func_id, wrapper_type, "python"};
              fsym.base_name = mname;
              fsym.location = loc;
              fsym.is_lvalue = true;
              symbol_table.add(fsym);
            }
            else
            {
              symbol_table.get_writeable_ref(wrapper_func_id).type =
                wrapper_type;
            }
            // Translate child invariants in the wrapper's scope
            // so Name(self) resolves to the wrapper's self
            // parameter.
            std::string saved_cf = current_function;
            current_function = wrapper_qfn;
            std::vector<exprt> wrapper_invariants;
            for(const jsont *lam : icontract_invariant_lambdas)
            {
              const jsont &lam_body = json_member(*lam, "body");
              exprt cond;
              try
              {
                cond = convert_expression(lam_body);
              }
              catch(...)
              {
                cond = nil_exprt{};
              }
              if(cond.is_nil() || cond.type().id() == ID_empty)
                continue;
              if(cond.type().id() != ID_bool)
                cond = typecast_exprt{cond, bool_typet{}};
              wrapper_invariants.push_back(cond);
            }
            current_function = saved_cf;
            // Build wrapper body.
            code_blockt wrapper_body;
            // Entry assertions (skip for __init__).
            if(mname != "__init__")
            {
              for(const exprt &cond : wrapper_invariants)
                wrapper_body.add(code_assertt{cond});
            }
            // Build call to parent's method.
            // self is passed cast to the parent's pointer type
            // (the underlying struct shape is compatible because
            // child is a structural extension).
            exprt::operandst call_args;
            exprt self_arg = wrapper_param_exprs[0];
            if(self_arg.type() != parent_self_t)
              self_arg = typecast_exprt{self_arg, parent_self_t};
            call_args.push_back(self_arg);
            for(std::size_t pi = 1; pi < wrapper_param_exprs.size(); ++pi)
            {
              exprt arg = wrapper_param_exprs[pi];
              const typet &want = parent_params[pi].type();
              if(arg.type() != want)
                arg = typecast_exprt{arg, want};
              call_args.push_back(arg);
            }
            side_effect_expr_function_callt parent_call{
              parent_sym->symbol_expr(),
              std::move(call_args),
              wrapper_return,
              loc};
            if(wrapper_return.id() == ID_empty)
            {
              wrapper_body.add(code_expressiont{std::move(parent_call)});
              for(const exprt &cond : wrapper_invariants)
                wrapper_body.add(code_assertt{cond});
            }
            else
            {
              // Capture the return value in a temp so we can
              // assert invariants AFTER the body's effects but
              // BEFORE returning. The temp is per-class-method
              // so it's stable across re-conversion.
              std::string tmp_name =
                "__icontract_wrap_ret_" + class_name + "_" + mname;
              std::string tmp_qname = "python::" + tmp_name;
              irep_idt tmp_id{tmp_qname};
              if(!symbol_table.has_symbol(tmp_id))
              {
                symbolt tmp_sym{tmp_id, wrapper_return, "python"};
                tmp_sym.base_name = tmp_name;
                tmp_sym.is_lvalue = true;
                tmp_sym.is_state_var = true;
                symbol_table.add(tmp_sym);
              }
              const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);
              wrapper_body.add(code_frontend_assignt{
                tmp_sym.symbol_expr(), std::move(parent_call)});
              for(const exprt &cond : wrapper_invariants)
                wrapper_body.add(code_assertt{cond});
              wrapper_body.add(code_frontend_returnt{tmp_sym.symbol_expr()});
            }
            symbol_table.get_writeable_ref(wrapper_func_id).value =
              wrapper_body;
            // Record the synthesised wrapper so future
            // reconversion / dispatch sees the method.
            class_declared_methods[class_name].insert(mname);
          }
        }
      }
    }

    current_class = saved_class;
  }

  return code_skipt{};
}

codet python_convertert::convert_expr_stmt(const jsont &stmt)
{
  // Expression statement (e.g., function call as statement)
  const jsont &value = json_member(stmt, "value");

  // PLR object identity (§9 #nested-aliasing): a mutating method on an element
  // of a list whose by-value mutable elements are aliased (`g[i].append(..)`
  // after `g=[[..]]*n` / `g=a[:]` / ...) is not modelled by the value
  // representation -- report + cut rather than silently false-prove. The
  // receiver `g[i]` is func.value. (`g.method(..)` mutates the OUTER list, not
  // an aliased element, so its receiver is a Name and is NOT guarded.)
  if(is_node_type(value, "Call"))
  {
    const jsont &cf = json_member(value, "func");
    if(is_node_type(cf, "Attribute"))
    {
      // Extraction-then-mutate (Name receiver aliasing a container element):
      // `r = c[i]; r.append(...)`. The is_aliased_list_element guard below only
      // covers Subscript receivers; here `r` is a Name recorded as an extracted
      // mutable alias, so havoc its source container (PLR reference semantics —
      // the by-value element copy would otherwise hide the mutation).
      {
        const jsont &recv0 = json_member(cf, "value");
        if(is_node_type(recv0, "Name"))
          invalidate_extracted_source_on_mutation(
            convert_expression(recv0), json_string(json_member(cf, "attr")));
        // Direct mutation of a non-int-keyed dict value (`d["k"].append(...)`):
        // the value is returned by copy, so the mutation is lost -- havoc the
        // dict so a later read is nondet (not a stale false-proof value).
        else if(is_node_type(recv0, "Subscript"))
          invalidate_dict_value_on_mutation(
            recv0, json_string(json_member(cf, "attr")));
      }
      static const std::set<std::string> mutating_methods{
        "append",
        "extend",
        "insert",
        "remove",
        "pop",
        "clear",
        "sort",
        "reverse"};
      if(
        mutating_methods.count(json_string(json_member(cf, "attr"))) &&
        is_aliased_list_element(json_member(cf, "value")))
        emit_aliased_mutation_guard(get_location(stmt));

      // --python-check-annotations: appending/inserting a value whose type is
      // definitely incompatible with a list's CONCRETE element type
      // (`list[int].append("s")`) is an annotation mismatch (the ty-007
      // laundering witness). Opt-in only -- it is legal at runtime (the error
      // arises on a later USE), so it is gated behind the flag, like the
      // call-arg / assign annotation checks. Restricted to a Constant/Name
      // argument: those are idempotent to convert (no side effect), so reading
      // the arg type here does not double-evaluate a side-effecting expression
      // (a `Call` arg such as `xs.append(src())` is skipped -- see the §7
      // single-coercion-point note). Lists with a `python_value` (Any) element
      // type never mismatch.
      if(python_check_annotations)
      {
        const std::string m = json_string(json_member(cf, "attr"));
        if(m == "append" || m == "insert")
        {
          const jsont &cargs = json_member(value, "args");
          if(cargs.is_array())
          {
            auto it = as_array(cargs).begin();
            const auto end = as_array(cargs).end();
            if(m == "insert" && it != end)
              ++it; // insert(index, value): the value is the 2nd arg
            if(it != end)
            {
              const jsont &arg_node = *it;
              // Resolve the argument's type WITHOUT re-converting a
              // side-effecting expression. Constant/Name are idempotent to
              // convert; for a Call (`xs.append(src())`, the ty-007 witness) we
              // read the callee's declared/inferred RETURN type from its symbol
              // instead of converting the call (which would double-evaluate it).
              typet val_t;
              bool have_type = false;
              if(
                is_node_type(arg_node, "Constant") ||
                is_node_type(arg_node, "Name"))
              {
                exprt val_e = convert_expression(arg_node);
                if(!val_e.is_nil())
                {
                  val_t = val_e.type();
                  have_type = true;
                }
              }
              else if(is_node_type(arg_node, "Call"))
              {
                const jsont &acf = json_member(arg_node, "func");
                if(is_node_type(acf, "Name"))
                {
                  const symbolt *fs = symbol_table.lookup(
                    irep_idt{"python::" + json_string(json_member(acf, "id"))});
                  if(fs != nullptr && fs->type.id() == ID_code)
                  {
                    val_t = to_code_type(fs->type).return_type();
                    have_type = true;
                  }
                }
              }
              if(have_type)
              {
                exprt recv_e = convert_expression(json_member(cf, "value"));
                // Provenance gate: only flag when the receiver list's element
                // type comes from an EXPLICIT annotation (`xs: list[int]`, in
                // variable_annotations), not from inference. An inferred empty
                // `[]` (e.g. `a.setdefault(1, [])`) gets a DEFAULT element type
                // that may not reflect the real contents, so checking it
                // false-alarms (dict_setdefault_list). PLR-safe: only narrows
                // when we fire.
                if(
                  !recv_e.is_nil() && recv_e.id() == ID_symbol &&
                  variable_annotations.count(
                    to_symbol_expr(recv_e).get_identifier()) &&
                  is_python_list_type(recv_e.type()))
                {
                  const typet elem_t =
                    to_array_type(
                      to_struct_type(recv_e.type()).components()[1].type())
                      .element_type();
                  if(
                    !is_python_value_type(elem_t) &&
                    annotation_types_incompatible(elem_t, val_t))
                    add_check(
                      false_exprt{},
                      "annotation-mismatch",
                      "appended element type does not match list element "
                      "annotation",
                      get_location(stmt));
                }
              }
            }
          }
        }
      }

      // Nested-aliasing soundness (PLR §9): `a.append(a[i])` /
      // `a.insert(_, g[i])` inserts a MUTABLE INNER element (a subscript
      // of a list, or a name already tracked as a shared inner) into
      // `a`, so `a` now holds an aliased inner object — mutating it later
      // through any position is the same unmodelled aliasing that
      // repetition/concat taint. Taint `a` so the element-mutation guard
      // fires (sound over-approximation; the appended subscript case is
      // harmless for scalars since no `a[j][k]` mutation follows).
      {
        const std::string m = json_string(json_member(cf, "attr"));
        const jsont &recv = json_member(cf, "value");
        if((m == "append" || m == "insert") && is_node_type(recv, "Name"))
        {
          const jsont &cargs = json_member(value, "args");
          bool shares_inner = false;
          if(cargs.is_array())
            for(const auto &a : as_array(cargs))
            {
              if(is_node_type(a, "Subscript"))
                shares_inner = true;
              else if(
                is_node_type(a, "Name") &&
                shared_inner_mutables.count(irep_idt{
                  qualify_name(json_string(json_member(a, "id")))}) > 0)
                shares_inner = true;
            }
          if(shares_inner)
            aliased_mutable_lists.insert(
              irep_idt{qualify_name(json_string(json_member(recv, "id")))});
        }
      }
    }
  }

  // PLR §6.2.9: yield X → __gen_result.append(X) (eager evaluation)
  if(
    is_node_type(value, "Yield") && !current_function.empty() &&
    generator_functions.count(current_function))
  {
    const jsont &yield_val = json_member(value, "value");
    exprt val =
      yield_val.is_null() ? python_none_value() : convert_expression(yield_val);
    code_blockt block = build_gen_result_append(val);
    if(!block.statements().empty())
      return std::move(block);
  }
  // PLR §6.2.9: 'yield from G()' delegates: each value yielded
  // by G is yielded by the enclosing generator. Equivalent to:
  //     for v in G():
  //         yield v
  // We model this by evaluating G() (which produces a list under
  // our eager generator model) and appending each element to
  // __gen_result.
  if(
    is_node_type(value, "YieldFrom") && !current_function.empty() &&
    generator_functions.count(current_function))
  {
    const jsont &yf_val = json_member(value, "value");
    exprt src = convert_expression(yf_val);
    std::string grn = "__gen_result_" + current_function;
    std::string grq = qualify_name(grn);
    irep_idt gri{grq};
    const symbolt *grs = symbol_table.lookup(gri);
    if(!src.is_nil() && is_python_list_type(src.type()) && grs != nullptr)
    {
      const auto &dst_list_st = to_struct_type(grs->type);
      const auto &dst_data_type =
        to_array_type(dst_list_st.components()[1].type());
      member_exprt dst_data{grs->symbol_expr(), "data", dst_data_type};
      member_exprt dst_len{grs->symbol_expr(), "length", signedbv_typet{64}};
      const auto &src_list_st = to_struct_type(src.type());
      const auto &src_data_type =
        to_array_type(src_list_st.components()[1].type());
      member_exprt src_data{src, "data", src_data_type};
      member_exprt src_len{src, "length", signedbv_typet{64}};
      code_blockt block;
      // For each i in [0, MAX), if i < src_len, append src.data[i].
      for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        exprt elem = index_exprt{src_data, idx};
        if(elem.type() != dst_data_type.element_type())
          elem = coerce_element(elem, dst_data_type.element_type());
        code_blockt append;
        append.add(code_frontend_assignt{index_exprt{dst_data, dst_len}, elem});
        append.add(code_frontend_assignt{
          dst_len, plus_exprt{dst_len, from_integer(1, signedbv_typet{64})}});
        block.add(code_ifthenelset{
          binary_relation_exprt{idx, ID_lt, src_len}, std::move(append)});
      }
      return std::move(block);
    }
  }

  // Check for __CPROVER_assume calls
  if(is_node_type(value, "Call"))
  {
    const jsont &func = json_member(value, "func");
    std::string func_name;
    if(is_node_type(func, "Name"))
      func_name = json_string(json_member(func, "id"));

    if(func_name == "__CPROVER_assume" || func_name == "__ESBMC_assume")
    {
      const jsont &args = json_member(value, "args");
      if(args.is_array() && !as_array(args).empty())
      {
        exprt cond = convert_expression(*as_array(args).begin());
        if(cond.type() != bool_typet{})
          cond = safe_typecast(cond, bool_typet{});
        code_assumet assume{cond};
        assume.add_source_location() = get_location(stmt);
        return std::move(assume);
      }
    }

    // Handle list.append(val)
    if(is_node_type(func, "Attribute"))
    {
      std::string method = json_string(json_member(func, "attr"));
      // PLR §3.3: a content-changing in-place list mutation invalidates the
      // list_literals constant-fold snapshot of the receiver. A stale snapshot
      // lets sorted()/min()/max()/index()/the mixed-orderable check fold against
      // PRE-mutation data -- a SOUNDNESS bug (`xs.append(-5); min(xs)` folded to
      // the old min 0 and false-proved `min(xs) == 0`). Erasing forces those
      // ops to read the runtime (mutated) list. Placed at the statement-level
      // dispatch so it runs regardless of which sub-handler emits the mutation;
      // sort()/reverse() maintain the snapshot themselves and are excluded.
      {
        static const std::set<std::string> list_content_mutators = {
          "append", "insert", "extend", "remove", "pop", "clear"};
        const jsont &recv = json_member(func, "value");
        if(
          list_content_mutators.count(method) > 0 && is_node_type(recv, "Name"))
          list_literals.erase(
            irep_idt{qualify_name(json_string(json_member(recv, "id")))});
      }
      if(method == "append")
      {
        exprt obj = convert_expression(json_member(func, "value"));
        obj = unwrap_any_container_receiver(obj, method);
        const jsont &call_args = json_member(value, "args");
        if(
          !obj.is_nil() && is_python_list_type(obj.type()) &&
          call_args.is_array() && !as_array(call_args).empty())
        {
          exprt val = convert_expression(*as_array(call_args).begin());
          source_locationt loc = get_location(stmt);

          const auto &list_st = to_struct_type(obj.type());
          const auto &data_type = to_array_type(list_st.components()[1].type());

          // lst.data[lst.length] = val
          member_exprt length{obj, "length", python_int_type()};
          member_exprt data{obj, "data", data_type};
          index_exprt slot{data, length};

          if(val.type() != data_type.element_type())
          {
            // PLR §4.5: tagged-union list element. wrap_value
            // sets the right __tag and underlying field so
            // numbers.data[k].__int_val (etc.) are observable
            // after the append. The naive typecast would zero
            // every field.
            if(is_python_value_type(data_type.element_type()))
              val = wrap_value(val);
            else
              val = typecast_exprt{val, data_type.element_type()};
          }

          code_blockt block;
          emit_capacity_guard(block, length, PYTHON_MAX_LIST_LENGTH, loc);
          code_frontend_assignt store{slot, val};
          store.add_source_location() = loc;
          block.add(std::move(store));

          // lst.length += 1
          code_frontend_assignt inc_len{
            length, plus_exprt{length, from_integer(1, python_int_type())}};
          inc_len.add_source_location() = loc;
          block.add(std::move(inc_len));

          return std::move(block);
        }
      }
      else if(method == "insert")
      {
        exprt obj = convert_expression(json_member(func, "value"));
        obj = unwrap_any_container_receiver(obj, method);
        const jsont &call_args = json_member(value, "args");
        if(
          !obj.is_nil() && is_python_list_type(obj.type()) &&
          call_args.is_array() && as_array(call_args).size() >= 2)
        {
          auto arg_it = as_array(call_args).begin();
          exprt idx_expr = convert_expression(*arg_it);
          ++arg_it;
          exprt val = convert_expression(*arg_it);
          source_locationt loc = get_location(stmt);
          const auto &list_st = to_struct_type(obj.type());
          const auto &data_type = to_array_type(list_st.components()[1].type());
          member_exprt length{obj, "length", python_int_type()};
          member_exprt data{obj, "data", data_type};
          if(idx_expr.type() != python_int_type())
            idx_expr = safe_typecast(idx_expr, python_int_type());
          if(val.type() != data_type.element_type())
            val = coerce_element(val, data_type.element_type());
          // PLR §4.6.3: list.insert(i, x) clamps i to
          // [0, len(list)]. Index past the end appends; very
          // negative index inserts at front (idx + len <= 0 → 0,
          // else idx + len). We materialise the clamped index
          // into a temp so the shift loop and the write below
          // both reference the same value.
          // clamped =
          //   idx < 0 ? max(0, idx + length)
          //           : min(idx, length)
          exprt zero64 = from_integer(0, python_int_type());
          exprt neg_branch = if_exprt{
            binary_relation_exprt{plus_exprt{idx_expr, length}, ID_lt, zero64},
            zero64,
            plus_exprt{idx_expr, length}};
          exprt pos_branch = if_exprt{
            binary_relation_exprt{idx_expr, ID_gt, length}, length, idx_expr};
          exprt clamped = if_exprt{
            binary_relation_exprt{idx_expr, ID_lt, zero64},
            neg_branch,
            pos_branch};
          static unsigned ins_ctr = 0;
          std::string tname = "__ins_idx_" + std::to_string(ins_ctr++);
          irep_idt tid{qualify_name(tname)};
          if(symbol_table.lookup(tid) == nullptr)
          {
            symbolt ts{tid, python_int_type(), "python"};
            ts.base_name = tname;
            ts.is_lvalue = true;
            ts.is_state_var = true;
            ts.is_static_lifetime = current_function.empty();
            symbol_table.add(ts);
          }
          symbol_exprt ins_idx = symbol_table.lookup_ref(tid).symbol_expr();
          code_blockt block;
          emit_capacity_guard(block, length, PYTHON_MAX_LIST_LENGTH, loc);
          block.add(code_frontend_assignt{ins_idx, std::move(clamped)});
          for(int i = PYTHON_MAX_LIST_LENGTH - 1; i >= 1; i--)
          {
            exprt ii = from_integer(i, python_int_type());
            exprt prev = from_integer(i - 1, python_int_type());
            block.add(code_ifthenelset{
              and_exprt{
                binary_relation_exprt{prev, ID_ge, ins_idx},
                binary_relation_exprt{prev, ID_lt, length}},
              code_frontend_assignt{
                index_exprt{data, ii}, index_exprt{data, prev}}});
          }
          block.add(code_frontend_assignt{index_exprt{data, ins_idx}, val});
          block.add(code_frontend_assignt{
            length, plus_exprt{length, from_integer(1, python_int_type())}});
          return std::move(block);
        }
      }
    }
  }

  exprt expr = convert_expression(value);

  if(expr.is_nil())
    return code_skipt{};

  code_expressiont code_expr{expr};
  code_expr.add_source_location() = get_location(stmt);
  return std::move(code_expr);
}

void python_convertert::classify_iter_protocol(
  const std::string &cls_name,
  const jsont &fdef,
  bool has_next)
{
  // A generator FUNCTION (any yield in the body, not nested in an inner
  // def) returns a generator object -- always a valid iterator.
  std::function<bool(const jsont &)> has_yield = [&](const jsont &n) -> bool
  {
    if(is_node_type(n, "FunctionDef") || is_node_type(n, "AsyncFunctionDef"))
      return false; // nested def: its yields are not ours
    if(is_node_type(n, "Yield") || is_node_type(n, "YieldFrom"))
      return true;
    if(n.is_object())
    {
      const auto &obj = static_cast<const json_objectt &>(n);
      for(const auto &kv : obj)
      {
        const jsont &child = kv.second;
        if(child.is_array())
        {
          for(const auto &c : as_array(child))
            if(has_yield(c))
              return true;
        }
        else if(child.is_object() && has_yield(child))
          return true;
      }
    }
    return false;
  };
  const jsont &body = json_member(fdef, "body");
  if(!body.is_array())
    return;
  if(has_yield(fdef))
  {
    class_iter_protocol[cls_name] = iter_protocol_kindt::VALID;
    return;
  }
  // Classify every Return statement; the def is VALID/INVALID only if ALL
  // its returns agree (mixed or unclassifiable -> UNKNOWN, not flagged).
  bool saw_return = false, all_valid = true, all_invalid = true;
  std::function<void(const jsont &)> scan = [&](const jsont &n)
  {
    if(is_node_type(n, "FunctionDef") || is_node_type(n, "AsyncFunctionDef"))
      return; // nested def
    if(is_node_type(n, "Return"))
    {
      saw_return = true;
      const jsont &rv = json_member(n, "value");
      bool valid = false, invalid = false;
      if(rv.is_null())
        invalid = true; // bare return -> None: not an iterator
      else if(is_node_type(rv, "Call"))
      {
        const jsont &fn = json_member(rv, "func");
        if(is_node_type(fn, "Name"))
        {
          const std::string callee = json_string(json_member(fn, "id"));
          // iter()/reversed() return iterators; map/filter/zip/enumerate
          // likewise. Other calls: UNKNOWN.
          if(
            callee == "iter" || callee == "reversed" || callee == "map" ||
            callee == "filter" || callee == "zip" || callee == "enumerate")
            valid = true;
        }
      }
      else if(is_node_type(rv, "GeneratorExp"))
        valid = true;
      else if(
        is_node_type(rv, "List") || is_node_type(rv, "Tuple") ||
        is_node_type(rv, "Dict") || is_node_type(rv, "Set") ||
        is_node_type(rv, "ListComp") || is_node_type(rv, "DictComp") ||
        is_node_type(rv, "SetComp") || is_node_type(rv, "JoinedStr"))
        invalid = true;
      else if(is_node_type(rv, "Constant"))
        invalid = true; // numbers, strings, None, bools: not iterators
      else if(is_node_type(rv, "Name"))
      {
        const std::string nm = json_string(json_member(rv, "id"));
        if(nm == "self")
        {
          // `return self` is valid iff the class defines __next__.
          (has_next ? valid : invalid) = true;
        }
      }
      if(!valid)
        all_valid = false;
      if(!invalid)
        all_invalid = false;
      return;
    }
    if(n.is_object())
    {
      const auto &obj = static_cast<const json_objectt &>(n);
      for(const auto &kv : obj)
      {
        const jsont &child = kv.second;
        if(child.is_array())
        {
          for(const auto &c : as_array(child))
            scan(c);
        }
        else if(child.is_object())
          scan(child);
      }
    }
  };
  for(const auto &st : as_array(body))
    scan(st);
  if(!saw_return)
  {
    // No return -> falls through returning None: not an iterator.
    class_iter_protocol[cls_name] = iter_protocol_kindt::INVALID;
    return;
  }
  if(all_valid)
    class_iter_protocol[cls_name] = iter_protocol_kindt::VALID;
  else if(all_invalid)
    class_iter_protocol[cls_name] = iter_protocol_kindt::INVALID;
  else
    class_iter_protocol[cls_name] = iter_protocol_kindt::UNKNOWN;
}

void python_convertert::register_dispatcher_summary(
  const std::string &func_id,
  const jsont &fdef,
  std::size_t first_param_index)
{
  if(dispatcher_summaries.count(func_id) > 0)
    return;
  const jsont &args_node = json_member(fdef, "args");
  const jsont &params = json_member(args_node, "args");
  if(!params.is_array() || as_array(params).size() <= first_param_index)
    return;
  const jsont &body = json_member(fdef, "body");
  if(!body.is_array())
    return;
  std::vector<std::string> param_names;
  for(const auto &p : as_array(params))
    param_names.push_back(json_string(json_member(p, "arg")));

  dispatcher_summaryt sum;
  bool have_param = false;
  bool saw_forwarder = false;
  for(const auto &st : as_array(body))
  {
    // Docstring / bare-constant expression statements are ignored.
    if(
      is_node_type(st, "Expr") &&
      is_node_type(json_member(st, "value"), "Constant"))
      continue;
    // Trailing `assert False[, msg]` -- the unknown-key default.
    if(is_node_type(st, "Assert"))
    {
      const jsont &tv = json_member(st, "test");
      if(is_node_type(tv, "Constant") && json_member(tv, "value").is_false())
      {
        sum.assert_false_default = true;
        continue;
      }
      return;
    }
    // A `return <anything>` after the assert-False default is unreachable.
    if(is_node_type(st, "Return") && sum.assert_false_default)
      continue;
    // FORWARDER: sole `return g(<param>, ...)`.
    if(is_node_type(st, "Return") && !have_param && sum.branches.empty())
    {
      const jsont &rv = json_member(st, "value");
      if(
        is_node_type(rv, "Call") &&
        is_node_type(json_member(rv, "func"), "Name"))
      {
        const jsont &cargs = json_member(rv, "args");
        if(cargs.is_array() && !as_array(cargs).empty())
        {
          const jsont &a0 = *as_array(cargs).begin();
          if(is_node_type(a0, "Name"))
          {
            const std::string an = json_string(json_member(a0, "id"));
            for(std::size_t i = first_param_index; i < param_names.size(); ++i)
            {
              if(param_names[i] == an)
              {
                sum.param_index = i;
                sum.forwards_to =
                  json_string(json_member(json_member(rv, "func"), "id"));
                saw_forwarder = true;
                break;
              }
            }
          }
        }
      }
      if(!saw_forwarder)
        return;
      continue;
    }
    // Chain link: `if <param> == "lit": return ClassName()`.
    if(!is_node_type(st, "If") || saw_forwarder)
      return;
    const jsont &test = json_member(st, "test");
    if(!is_node_type(test, "Compare"))
      return;
    const jsont &left = json_member(test, "left");
    const jsont &ops = json_member(test, "ops");
    const jsont &comps = json_member(test, "comparators");
    if(
      !is_node_type(left, "Name") || !ops.is_array() ||
      as_array(ops).size() != 1 ||
      !is_node_type(*as_array(ops).begin(), "Eq") || !comps.is_array() ||
      as_array(comps).size() != 1)
      return;
    const jsont &lit = *as_array(comps).begin();
    if(!is_node_type(lit, "Constant") || !json_member(lit, "value").is_string())
      return;
    const std::string pname = json_string(json_member(left, "id"));
    std::size_t pidx = SIZE_MAX;
    for(std::size_t i = first_param_index; i < param_names.size(); ++i)
      if(param_names[i] == pname)
      {
        pidx = i;
        break;
      }
    if(pidx == SIZE_MAX || (have_param && pidx != sum.param_index))
      return;
    sum.param_index = pidx;
    have_param = true;
    const jsont &ibody = json_member(st, "body");
    const jsont &ielse = json_member(st, "orelse");
    if(
      !ibody.is_array() || as_array(ibody).size() != 1 ||
      (ielse.is_array() && !as_array(ielse).empty()))
      return;
    const jsont &ret = *as_array(ibody).begin();
    if(!is_node_type(ret, "Return"))
      return;
    const jsont &rv = json_member(ret, "value");
    // Only a no-arg constructor call of a Name folds safely (no dependence
    // on other params/locals; the class is resolved at FOLD time).
    if(
      !is_node_type(rv, "Call") ||
      !is_node_type(json_member(rv, "func"), "Name"))
      return;
    const jsont &cargs = json_member(rv, "args");
    if(cargs.is_array() && !as_array(cargs).empty())
      return;
    sum.branches[json_string(json_member(lit, "value"))] =
      json_string(json_member(json_member(rv, "func"), "id"));
  }
  const bool is_dispatcher = !sum.branches.empty() && sum.assert_false_default;
  if(is_dispatcher || saw_forwarder)
    dispatcher_summaries[func_id] = sum;
}

std::optional<exprt> python_convertert::try_dispatcher_fold(
  const std::string &func_id,
  const jsont &args,
  const jsont &expr,
  std::size_t first_param_index)
{
  if(getenv("CBMC_NO_DISPFOLD") != nullptr)
    return std::nullopt;
  auto it = dispatcher_summaries.find(func_id);
  if(it == dispatcher_summaries.end())
    return std::nullopt;
  const dispatcher_summaryt *sum = &it->second;
  std::size_t args_index = sum->param_index - first_param_index;
  // Follow a FORWARDER one level (e.g. _Session.client -> module client):
  // the forwarder passes its switch param as the inner call's FIRST arg,
  // so the OUTER literal at args_index selects in the INNER's branches.
  if(!sum->forwards_to.empty())
  {
    auto iit = dispatcher_summaries.find("python::" + sum->forwards_to);
    if(iit == dispatcher_summaries.end() || !iit->second.forwards_to.empty())
      return std::nullopt;
    sum = &iit->second;
  }
  if(!args.is_array() || as_array(args).size() <= args_index)
    return std::nullopt;
  auto ait = as_array(args).begin();
  std::advance(ait, args_index);
  const jsont &sw = *ait;
  if(!is_node_type(sw, "Constant") || !json_member(sw, "value").is_string())
    return std::nullopt;
  const std::string key = json_string(json_member(sw, "value"));
  auto bit = sum->branches.find(key);
  if(bit == sum->branches.end())
  {
    // The dispatcher's own default: `assert False` (its unknown-key
    // contract). Preserve it as a definite property at the call site.
    if(!sum->assert_false_default)
      return std::nullopt;
    source_locationt aloc = get_location(expr);
    aloc.set_property_class("assertion");
    aloc.set_comment("dispatcher default reached (assert False)");
    code_assertt af{false_exprt{}};
    af.add_source_location() = aloc;
    pending_checks.push_back(std::move(af));
    return exprt{
      side_effect_expr_nondett{python_value_type(), get_location(expr)}};
  }
  const std::string &cls = bit->second;
  if(class_types.count(cls) == 0)
    return std::nullopt;
  // Materialise the selected branch's construction -- exactly what the
  // inline `ClassName()` expression path does.
  const struct_typet &cls_type = class_types.at(cls);
  static unsigned dispfold_ctr = 0;
  const std::string tn =
    "__dispfold_" + cls + "_" + std::to_string(dispfold_ctr++);
  const irep_idt tid{qualify_name(tn)};
  if(symbol_table.lookup(tid) == nullptr)
  {
    symbolt ts{tid, cls_type, "python"};
    ts.base_name = tn;
    ts.is_lvalue = true;
    ts.is_state_var = true;
    // STATIC: the folded result is BOXED (a pv holding &temp), and the
    // fold may run inside a method (self.client = boto3.client("ecs")) --
    // a frame-local temp would dangle after the method returns. A static
    // per-site instance matches the non-folded semantics (the dispatcher's
    // own return-materialisation temp is per-callee storage).
    ts.is_static_lifetime = true;
    symbol_table.add(ts);
  }
  const symbolt &tsym = symbol_table.lookup_ref(tid);
  // Class-level defaults, then construction (__init__ / dataclass /
  // __class_tag stamp) -- mirrors the ctor-expression path.
  const irep_idt class_obj_id{"python::" + cls};
  const symbolt *class_obj = symbol_table.lookup(class_obj_id);
  if(class_obj != nullptr && !class_obj->value.is_nil())
    pending_checks.push_back(
      code_frontend_assignt{tsym.symbol_expr(), class_obj->symbol_expr()});
  // The selected branch is a NO-ARG `ClassName()` (enforced at
  // registration): bind the ctor with an EMPTY synthetic call node so
  // __init__ params take their DEFAULTS. Passing the OUTER dispatcher
  // call here bound its args ("ecs", region_name=...) to __init__ -- a
  // semantic change that regressed ecs_utils.
  static const jsont empty_call = []()
  {
    json_objectt o;
    o["args"] = json_arrayt{};
    o["keywords"] = json_arrayt{};
    return jsont{o};
  }();
  for(auto &st : build_class_construction(
        cls, tsym.symbol_expr(), empty_call, get_location(expr)))
    pending_checks.push_back(std::move(st));
  // Preserve the dispatcher's TYPE CONTRACT: it returns `-> Any` (a boxed
  // python_value), so callers must see the same shape -- returning the raw
  // class struct changed downstream field/receiver typing and regressed
  // ecs_utils (the fold must be a pure PERF transform, invisible to
  // semantics).
  return exprt{make_python_value(python_type_tagt::CLASS, tsym.symbol_expr())};
}
