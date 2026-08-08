/// \file
/// Python to GOTO converter — container-operation choke points.
///
/// P0 of doc/python-frontend-unbounded-containers-plan.md: the
/// bounded lowerings of container operations (dict key lookup,
/// element equality, boxed-dict access) are consolidated here so
/// every call site shares ONE spelling of each step. Before this
/// file, the per-slot key/element comparison was open-coded at
/// ten+ sites and had diverged: the read-side copies (get,
/// subscript read, In/NotIn) compared string keys by CONTENT via
/// the string solver, while the write/mutate-side copies
/// (setdefault, pop, del, subscript write, list index/count/
/// remove) still used raw struct equality — the refined-string
/// DATA POINTER — and falsely refuted any needle built at
/// runtime (loop-concatenated keys, values that crossed a call
/// boundary). PLR §6.4.6 / §6.10.1: dict lookup and sequence
/// membership compare by VALUE.
///
/// P1 re-targets these helpers to cprover_list_*/cprover_dict_*
/// function applications under --python-smt-containers; the
/// call-site contracts stay unchanged.

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/expr.h>
#include <util/expr_util.h>
#include <util/floatbv_expr.h>
#include <util/mathematical_expr.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>

#include <goto-programs/goto_instruction_code.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

exprt python_convertert::container_slot_equal(
  const exprt &a,
  const exprt &b,
  std::vector<codet> *sink)
{
  // Tagged unions: dispatch on the runtime tag (PLR §3.3). If only
  // one side is boxed, box the other — value_equal requires two
  // python_value operands and compares STR content, FLOAT/BOOL/INT
  // payloads under a tag guard.
  const bool a_pv = is_python_value_type(a.type());
  const bool b_pv = is_python_value_type(b.type());
  if(a_pv || b_pv)
    return value_equal(a_pv ? a : wrap_value(a), b_pv ? b : wrap_value(b));

  // Strings: CONTENT equality via the string solver. Raw struct
  // equality compares the data pointer of the refined-string view
  // (or the raw handle bits on the native backend) and wrongly
  // refutes equal content with distinct storage.
  if(is_python_string_type(a.type()) && is_python_string_type(b.type()))
  {
    exprt eq = emit_string_bool_function(
      ID_cprover_string_equal_func,
      a,
      b,
      symbol_table,
      sink != nullptr ? *sink : pending_checks);
    if(eq.type() != bool_typet{})
      eq = typecast_exprt{std::move(eq), bool_typet{}};
    return eq;
  }

  // Scalars / identical layouts: plain equality, with a
  // value-preserving cast when the scalar types differ
  // (PLR §6.10.1: 1 == 1.0 is True).
  exprt rhs = b;
  if(a.type() != rhs.type())
    rhs = safe_typecast(rhs, a.type());
  if(a.type().id() == ID_floatbv)
    return ieee_float_equal_exprt{a, rhs};
  return equal_exprt{a, rhs};
}

exprt python_convertert::dict_slot_match(
  const exprt &keys_array,
  const exprt &length,
  std::size_t i,
  const exprt &key_probe,
  std::vector<codet> *sink)
{
  // P2 fail-closed scan bound, emitted ONCE per scan (at slot 0):
  // under --python-smt-containers the dict's arrays are infinite, but
  // every key lookup visits only the first PYTHON_MAX_DICT_SIZE slots
  // — a longer dict must be reported (python-model-bound) and cut,
  // never silently mis-looked-up (PLR §6.4.6: lookup is total over
  // the dict's keys). Placing the guard in the ONE lookup step every
  // routed scan shares covers get/setdefault/pop/del/subscript/
  // membership/update in one edit.
  if(i == 0)
  {
    emit_scan_bound_guard(
      length,
      source_locationt{},
      static_cast<long>(PYTHON_MAX_DICT_SIZE),
      sink);
  }
  const exprt idx = from_integer(i, signedbv_typet{64});
  const exprt in_range = binary_relation_exprt{idx, ID_lt, length};
  exprt key_at = python_dict_unbox_key(index_exprt{keys_array, idx});
  exprt probe = python_dict_unbox_key(key_probe);
  return and_exprt{in_range, container_slot_equal(key_at, probe, sink)};
}

/// SPIKE structure-of-arrays provenance recorder: at a single-Name
/// binding, remember (a) `resp = f()` where f returns a TypedDict
/// (var -> TD name), and (b) `apps = resp['field']` where resp's TD
/// declares field as List[TD-SoA-eligible] (var -> element TD). The
/// comprehension-iterable hook derefs such a variable's pv box to
/// the SoA type. Reassignment to any other shape CLEARS the entries
/// (stale provenance would re-type a rebound variable).
void python_convertert::record_soa_provenance(
  const irep_idt &target_id,
  const jsont &value)
{
  var_typeddict.erase(target_id);
  var_soa_elem.erase(target_id);
  name_list_ast.erase(target_id);
  if(is_node_type(value, "List"))
    name_list_ast[target_id] = &value;
  if(!python_smt_containers_flag())
    return;
  if(is_node_type(value, "Call"))
  {
    const jsont &fn = json_member(value, "func");
    if(is_node_type(fn, "Name"))
    {
      auto it =
        function_return_typeddict.find(json_string(json_member(fn, "id")));
      if(it != function_return_typeddict.end())
        var_typeddict[target_id] = it->second;
    }
    return;
  }
  if(is_node_type(value, "Subscript"))
  {
    const jsont &sv = json_member(value, "value");
    const jsont &sl = json_member(value, "slice");
    if(
      is_node_type(sv, "Name") && is_node_type(sl, "Constant") &&
      json_member(sl, "value").is_string())
    {
      auto vt = var_typeddict.find(
        irep_idt{qualify_name(json_string(json_member(sv, "id")))});
      if(vt != var_typeddict.end())
      {
        auto fle = typed_dict_field_list_elem.find(vt->second);
        if(fle != typed_dict_field_list_elem.end())
        {
          auto fe = fle->second.find(json_member(sl, "value").value);
          if(fe != fle->second.end() && soa_eligible_td(fe->second))
            var_soa_elem[target_id] = fe->second;
        }
      }
    }
  }
}

exprt python_convertert::td_field_read_memo(
  const jsont &subscript_node,
  const typet &soa_type)
{
  const jsont &sv = json_member(subscript_node, "value");
  const jsont &sl = json_member(subscript_node, "slice");
  if(
    !is_node_type(sv, "Name") || !is_node_type(sl, "Constant") ||
    !json_member(sl, "value").is_string())
    return nil_exprt{};
  const std::string key = qualify_name(json_string(json_member(sv, "id"))) +
                          "." + json_member(sl, "value").value;
  auto it = td_field_read_cache.find(key);
  if(it != td_field_read_cache.end())
    return it->second;
  exprt read = convert_expression(subscript_node);
  if(read.is_nil() || !is_python_value_type(read.type()))
    return nil_exprt{};
  // Materialise the DEREFERENCED SoA value ONCE (a whole-struct
  // copy, like any dict-struct assignment). The pointer travelled
  // through the dict's INFINITE values array, where value-set
  // precision cannot resolve it uniquely -- two derefs of the
  // SYNTACTICALLY IDENTICAL expression produced fresh failure
  // objects, so even len(resp['f']) == len(resp['f']) was
  // unprovable. One copy shared by every read makes them equal by
  // construction. SOUND for the READ-ONLY consumers wired to this
  // memo (comprehension iterables, len); the cache is cleared on
  // dict stores. In-place MUTATION of the field through other paths
  // (resp['f'].append) must not use the memo -- those paths do not.
  dereference_exprt soa_obj{
    typecast_exprt{python_value_class_ptr(read), pointer_typet{soa_type, 64}}};
  static unsigned tdr_ctr = 0;
  const std::string tn = "__td_soa_" + std::to_string(tdr_ctr++);
  const irep_idt tid{qualify_name(tn)};
  if(symbol_table.lookup(tid) == nullptr)
  {
    symbolt ts{tid, soa_type, "python"};
    ts.base_name = tn;
    ts.is_lvalue = true;
    ts.is_state_var = true;
    ts.is_static_lifetime = current_function.empty();
    symbol_table.add(ts);
  }
  symbol_exprt tsym = symbol_table.lookup_ref(tid).symbol_expr();
  pending_checks.push_back(code_frontend_assignt{tsym, std::move(soa_obj)});
  // The copy's length inherits the representation invariant.
  pending_checks.push_back(code_assumet{binary_relation_exprt{
    member_exprt{tsym, "length", signedbv_typet{64}},
    ID_ge,
    from_integer(0, signedbv_typet{64})}});
  td_field_read_cache.emplace(key, tsym);
  return tsym;
}

/// Memoized SoA materialisation for a NAME bound to a boxed SoA
/// value (`apps = resp['f']`): every consumer (comprehension
/// iterable, len) must read ONE dereferenced copy -- independent
/// derefs of even the same pv expression yield unrelated failure
/// objects through the infinite-array value set (see
/// td_field_read_memo).
exprt python_convertert::soa_value_of_name(
  const irep_idt &name_id,
  const typet &soa_type)
{
  const std::string key = "name:" + id2string(name_id);
  auto it = td_field_read_cache.find(key);
  if(it != td_field_read_cache.end())
    return it->second;
  const symbolt *vs = symbol_table.lookup(name_id);
  if(vs == nullptr || !is_python_value_type(vs->type))
    return nil_exprt{};
  dereference_exprt soa_obj{typecast_exprt{
    python_value_class_ptr(vs->symbol_expr()), pointer_typet{soa_type, 64}}};
  static unsigned soan_ctr = 0;
  const std::string tn = "__soa_of_" + std::to_string(soan_ctr++);
  const irep_idt tid{qualify_name(tn)};
  if(symbol_table.lookup(tid) == nullptr)
  {
    symbolt ts{tid, soa_type, "python"};
    ts.base_name = tn;
    ts.is_lvalue = true;
    ts.is_state_var = true;
    ts.is_static_lifetime = current_function.empty();
    symbol_table.add(ts);
  }
  symbol_exprt tsym = symbol_table.lookup_ref(tid).symbol_expr();
  pending_checks.push_back(code_frontend_assignt{tsym, std::move(soa_obj)});
  pending_checks.push_back(code_assumet{binary_relation_exprt{
    member_exprt{tsym, "length", signedbv_typet{64}},
    ID_ge,
    from_integer(0, signedbv_typet{64})}});
  td_field_read_cache.emplace(key, tsym);
  return tsym;
}

exprt python_convertert::build_dict_value_user_eq(
  std::vector<std::pair<exprt, exprt>> pairs,
  const std::string &key_cls,
  const source_locationt &loc)
{
  // Heterogeneous values wrap; keys keep the class struct type.
  const typet key_t = pairs.front().first.type();
  typet val_t = pairs.front().second.type();
  for(std::size_t i = 1; i < pairs.size(); i++)
    if(pairs[i].second.type() != val_t)
    {
      val_t = python_value_type();
      break;
    }
  struct_typet dict_t = python_dict_type(key_t, val_t);
  const auto &keys_at = to_array_type(dict_t.components()[1].type());
  const auto &vals_at = to_array_type(dict_t.components()[2].type());
  static unsigned ue_ctr = 0;
  const std::string dn = "__ue_dict_" + std::to_string(ue_ctr++);
  const irep_idt did{qualify_name(dn)};
  if(symbol_table.lookup(did) == nullptr)
  {
    symbolt ds{did, dict_t, "python"};
    ds.base_name = dn;
    ds.is_lvalue = true;
    ds.is_state_var = true;
    ds.is_static_lifetime = current_function.empty();
    symbol_table.add(ds);
  }
  symbol_exprt d = symbol_table.lookup_ref(did).symbol_expr();
  pending_checks.push_back(code_frontend_assignt{d, safe_zero(dict_t)});
  member_exprt dlen{d, "length", signedbv_typet{64}};
  member_exprt dkeys{d, "keys", keys_at};
  member_exprt dvals{d, "values", vals_at};
  const std::size_t n = pairs.size();
  for(std::size_t i = 0; i < n; i++)
  {
    exprt k = pairs[i].first;
    exprt v = pairs[i].second;
    if(v.type() != vals_at.element_type())
      v = is_python_value_type(vals_at.element_type())
            ? wrap_value(v)
            : coerce_element(v, vals_at.element_type());
    // Materialise the probe key once (it is both compared and
    // possibly stored).
    static unsigned uek_ctr = 0;
    const std::string kn = "__ue_key_" + std::to_string(uek_ctr++);
    const irep_idt kid{qualify_name(kn)};
    if(symbol_table.lookup(kid) == nullptr)
    {
      symbolt ks{kid, key_t, "python"};
      ks.base_name = kn;
      ks.is_lvalue = true;
      ks.is_state_var = true;
      ks.is_static_lifetime = current_function.empty();
      symbol_table.add(ks);
    }
    symbol_exprt ksym = symbol_table.lookup_ref(kid).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{ksym, k});
    // found flag
    static unsigned uef_ctr = 0;
    const std::string fn2 = "__ue_fnd_" + std::to_string(uef_ctr++);
    const irep_idt fid{qualify_name(fn2)};
    if(symbol_table.lookup(fid) == nullptr)
    {
      symbolt fs{fid, bool_typet{}, "python"};
      fs.base_name = fn2;
      fs.is_lvalue = true;
      fs.is_state_var = true;
      fs.is_static_lifetime = current_function.empty();
      symbol_table.add(fs);
    }
    symbol_exprt fnd = symbol_table.lookup_ref(fid).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{fnd, false_exprt{}});
    // Per-slot __eq__ scan over slots that can be occupied at this
    // point (< i). CPython invocation-count/order under hashing is
    // unspecified; the store dedup uses only the RESULTS. The probe
    // is a FRESH object display element only when it is a
    // constructor call; the materialised copy is fresh here by
    // construction of the display/comprehension enumeration, so the
    // identity disjunct is off.
    for(std::size_t s2 = 0; s2 < i; s2++)
    {
      const exprt idx = from_integer(s2, signedbv_typet{64});
      exprt in_range = binary_relation_exprt{idx, ID_lt, dlen};
      std::vector<codet> eq_stmts;
      exprt m = emit_user_eq_match(
        key_cls, index_exprt{dkeys, idx}, ksym, true, eq_stmts, loc);
      if(m.is_nil())
        return safe_zero(dict_t); // no __eq__ symbol: bail safe
      // The call must only run for occupied slots (its own body may
      // have obligations): guard the WHOLE per-slot block.
      code_blockt slotb;
      for(auto &st : eq_stmts)
        slotb.add(std::move(st));
      code_blockt upd;
      upd.add(code_frontend_assignt{index_exprt{dvals, idx}, v});
      upd.add(code_frontend_assignt{fnd, true_exprt{}});
      slotb.add(code_ifthenelset{and_exprt{not_exprt{fnd}, m}, std::move(upd)});
      pending_checks.push_back(
        code_ifthenelset{std::move(in_range), std::move(slotb)});
    }
    code_blockt app;
    app.add(code_frontend_assignt{index_exprt{dkeys, dlen}, ksym});
    app.add(code_frontend_assignt{index_exprt{dvals, dlen}, v});
    app.add(code_frontend_assignt{
      dlen, plus_exprt{dlen, from_integer(1, signedbv_typet{64})}});
    pending_checks.push_back(code_ifthenelset{not_exprt{fnd}, std::move(app)});
  }
  return std::move(d);
}

std::string python_convertert::receiver_name_of_def(const jsont &funcdef) const
{
  // @staticmethod has NO receiver.
  const jsont &decos = json_member(funcdef, "decorator_list");
  if(decos.is_array())
    for(const auto &d : as_array(decos))
    {
      if(
        is_node_type(d, "Name") &&
        json_string(json_member(d, "id")) == "staticmethod")
        return std::string{};
    }
  const jsont &args = json_member(funcdef, "args");
  const jsont &pos = json_member(args, "args");
  if(!pos.is_array() || as_array(pos).empty())
    return std::string{};
  return json_string(json_member(*as_array(pos).begin(), "arg"));
}

std::string python_convertert::class_name_of_type(const typet &t) const
{
  std::string tag;
  if(t.id() == ID_struct)
    tag = id2string(to_struct_type(t).get_tag());
  else if(t.id() == ID_struct_tag)
    tag = id2string(to_struct_tag_type(t).get_identifier());
  if(tag.rfind("tag-", 0) == 0)
    tag = tag.substr(4);
  if(tag.rfind("python_class_", 0) == 0)
    return tag.substr(13);
  return std::string{};
}

bool python_convertert::class_defines_eq(const std::string &cls)
{
  return class_mro_defines(cls, "__eq__");
}

bool python_convertert::class_defines_hash(const std::string &cls)
{
  return class_mro_defines(cls, "__hash__");
}

exprt python_convertert::emit_user_eq_match(
  const std::string &cls,
  const exprt &stored,
  const exprt &probe,
  bool probe_is_fresh_object,
  std::vector<codet> &sink,
  const source_locationt &loc)
{
  const symbolt *eq_sym = nullptr;
  for(const std::string &cand :
      {"python::" + cls + "::__eq__",
       "python::python_class_" + cls + "::__eq__"})
  {
    eq_sym = symbol_table.lookup(irep_idt{cand});
    if(eq_sym != nullptr)
      break;
  }
  if(eq_sym == nullptr || eq_sym->type.id() != ID_code)
    return nil_exprt{};
  const code_typet &ct = to_code_type(eq_sym->type);
  // ALWAYS materialise the stored key into a temp and pass its
  // address: a pointer INTO an infinite-array element (address_of
  // keys[i]) has no representable offset in the pointer encoding
  // (pointer_logic abort). A copy suffices for __eq__'s READS;
  // an __eq__ that mutates self is pathological and its effect on
  // the stored key is not modeled (documented).
  exprt self_arg = stored;
  {
    static unsigned eqs_ctr = 0;
    const std::string sn = "__eq_self_" + std::to_string(eqs_ctr++);
    const irep_idt sid{qualify_name(sn)};
    if(symbol_table.lookup(sid) == nullptr)
    {
      symbolt ss{sid, self_arg.type(), "python"};
      ss.base_name = sn;
      ss.is_lvalue = true;
      ss.is_state_var = true;
      ss.is_static_lifetime = current_function.empty();
      symbol_table.add(ss);
    }
    symbol_exprt ssym = symbol_table.lookup_ref(sid).symbol_expr();
    sink.push_back(code_frontend_assignt{ssym, self_arg});
    self_arg = ssym;
  }
  exprt::operandst eq_args{address_of_exprt{self_arg}, probe};
  coerce_call_args(eq_sym->type, eq_args);
  // Materialise the call result (calls cannot sit in expressions).
  static unsigned eqr_ctr = 0;
  const std::string rn = "__eq_res_" + std::to_string(eqr_ctr++);
  const irep_idt rid{qualify_name(rn)};
  const typet rt = ct.return_type().id() == ID_empty ? typet{python_int_type()}
                                                     : ct.return_type();
  if(symbol_table.lookup(rid) == nullptr)
  {
    symbolt rs{rid, rt, "python"};
    rs.base_name = rn;
    rs.is_lvalue = true;
    rs.is_state_var = true;
    rs.is_static_lifetime = current_function.empty();
    symbol_table.add(rs);
  }
  symbol_exprt rsym = symbol_table.lookup_ref(rid).symbol_expr();
  code_function_callt call{
    rsym,
    eq_sym->symbol_expr(),
    code_function_callt::argumentst{eq_args.begin(), eq_args.end()}};
  call.add_source_location() = loc;
  sink.push_back(std::move(call));
  exprt match = python_truthiness(rsym);
  if(match.type() != bool_typet{})
    match = typecast_exprt{std::move(match), bool_typet{}};
  // CPython's identity short-circuit (`stored is probe or eq`): with
  // BY-VALUE key storage object identity is unrepresentable, so a
  // probe that could alias a stored key contributes a sound NONDET
  // disjunct (both outcomes explored). A FRESH object (a constructor
  // call at the probe site) can alias nothing.
  if(!probe_is_fresh_object)
  {
    static unsigned eqi_ctr = 0;
    const std::string in_ = "__eq_id_" + std::to_string(eqi_ctr++);
    const irep_idt iid{qualify_name(in_)};
    if(symbol_table.lookup(iid) == nullptr)
    {
      symbolt is_{iid, bool_typet{}, "python"};
      is_.base_name = in_;
      is_.is_lvalue = true;
      is_.is_state_var = true;
      is_.is_static_lifetime = current_function.empty();
      symbol_table.add(is_);
    }
    symbol_exprt isym = symbol_table.lookup_ref(iid).symbol_expr();
    sink.push_back(
      code_frontend_assignt{isym, side_effect_expr_nondett{bool_typet{}, loc}});
    match = or_exprt{std::move(match), isym};
  }
  return match;
}

bool python_convertert::python_eq_is_structural(const typet &t) const
{
  const irep_idt &tid = t.id();
  if(
    tid == ID_signedbv || tid == ID_unsignedbv || tid == ID_integer ||
    tid == ID_floatbv || tid == ID_bool || tid == ID_string)
    return true;
  if(is_python_string_type(t) || is_python_string_handle_type(t))
    return true;
  // Tuples: structural iff every component is.
  if(is_python_tuple_type(t))
  {
    for(const auto &c : to_struct_type(t).components())
      if(!python_eq_is_structural(c.type()))
        return false;
    return true;
  }
  // Class instances (identity / user __eq__), python_value (may hold
  // one), lists/dicts/sets (may contain one; also Python compares
  // them element-wise through the same rules): NOT structural.
  return false;
}

void python_convertert::emit_eq_semantics_guard(
  const source_locationt &loc,
  const std::string &context)
{
  source_locationt aloc = loc;
  aloc.set_property_class("python-model-limitation");
  aloc.set_comment(
    context +
    " equality uses Python object semantics (identity / user __eq__), "
    "which this model compares structurally: rejected (fail-closed)");
  code_assertt guard{false_exprt{}};
  guard.add_source_location() = aloc;
  pending_checks.push_back(std::move(guard));
  code_assumet cut{false_exprt{}};
  cut.add_source_location() = loc;
  pending_checks.push_back(std::move(cut));
}

bool python_convertert::soa_eligible_td(const std::string &td_name) const
{
  auto tdb = class_bases.find(td_name);
  const bool is_td =
    tdb != class_bases.end() &&
    std::find(tdb->second.begin(), tdb->second.end(), "TypedDict") !=
      tdb->second.end();
  if(!is_td)
    return false;
  auto tff = typeddict_class_fields.find(td_name);
  auto tft = typed_dict_field_types.find(td_name);
  if(
    tff == typeddict_class_fields.end() || tff->second.empty() ||
    tft == typed_dict_field_types.end())
    return false;
  for(const auto &f : tff->second)
  {
    auto ft = tft->second.find(f);
    if(
      ft == tft->second.end() ||
      (ft->second != "str" && ft->second != "int" && ft->second != "bool" &&
       ft->second != "float"))
      return false;
  }
  // Optional fields are supported via per-row presence arrays.
  return true;
}

typet python_convertert::soa_list_type(const std::string &td_name)
{
  // Field CATEGORIES come from the same map the TypedDict stub
  // synthesis uses; only known scalar categories qualify (a nested
  // container field would re-introduce boxing -- the caller gates).
  struct_typet::componentst comps;
  comps.push_back(struct_typet::componentt{"length", signedbv_typet{64}});
  const auto &fields = typeddict_class_fields.at(td_name);
  const auto &ftypes = typed_dict_field_types.at(td_name);
  const std::set<std::string> *opt_fields = nullptr;
  {
    auto oit = typeddict_optional_fields.find(td_name);
    if(oit != typeddict_optional_fields.end())
      opt_fields = &oit->second;
  }
  for(const auto &f : fields)
  {
    const std::string &cat = ftypes.at(f);
    typet et;
    if(cat == "str")
      et = python_smt_string_native_flag() ? typet{python_string_handle_type()}
                                           : typet{python_string_type()};
    else if(cat == "int")
      et = python_int_type();
    else if(cat == "bool")
      et = bool_typet{};
    else if(cat == "float")
      et = double_type();
    else
      et = python_value_type();
    comps.push_back(struct_typet::componentt{
      f + "_data", array_typet{et, exprt{infinity_exprt{signedbv_typet{64}}}}});
    // PEP 589 requiredness: an OPTIONAL field (NotRequired /
    // total=False) gets a parallel per-row PRESENCE array; reads
    // carry a KeyError obligation guarded by it.
    if(opt_fields != nullptr && opt_fields->count(f) > 0)
      comps.push_back(struct_typet::componentt{
        f + "_present",
        array_typet{bool_typet{}, exprt{infinity_exprt{signedbv_typet{64}}}}});
  }
  struct_typet result{comps};
  result.set_tag("python_soa_list_" + td_name);
  return std::move(result);
}

bool python_convertert::is_soa_list_type(const typet &t) const
{
  return t.id() == ID_struct &&
         id2string(to_struct_type(t).get_tag()).rfind("python_soa_list_", 0) ==
           0;
}

exprt python_convertert::soa_field_data(
  const exprt &soa_value,
  const std::string &field)
{
  const auto &st = to_struct_type(soa_value.type());
  const std::string comp = field + "_data";
  if(!st.has_component(comp))
    return nil_exprt{};
  return member_exprt{soa_value, comp, st.get_component(comp).type()};
}

typet python_convertert::canonical_str_dict_type() const
{
  return python_dict_type(python_string_type(), python_value_type());
}

dereference_exprt python_convertert::boxed_dict_deref(const exprt &boxed) const
{
  return dereference_exprt{typecast_exprt{
    python_value_class_ptr(boxed),
    pointer_typet{canonical_str_dict_type(), 64}}};
}

exprt python_convertert::build_list_data(
  exprt::operandst elements,
  const array_typet &data_array) const
{
  const typet &slot_type = data_array.element_type();
  if(python_smt_containers_flag())
  {
    exprt data = array_of_exprt{safe_zero(slot_type), data_array};
    for(std::size_t i = 0; i < elements.size(); i++)
    {
      data = with_exprt{
        std::move(data),
        from_integer(i, signedbv_typet{64}),
        std::move(elements[i])};
    }
    return data;
  }
  // Pad/trim to the array type's OWN declared size (a literal larger
  // than PYTHON_MAX_LIST_LENGTH gets a grown array type — e.g.
  // stdlib __all__ lists; see convert_list).
  std::size_t cap = PYTHON_MAX_LIST_LENGTH;
  mp_integer size_val;
  if(
    data_array.size().is_constant() &&
    !to_integer(to_constant_expr(data_array.size()), size_val))
    cap = static_cast<std::size_t>(size_val.to_ulong());
  while(elements.size() < cap)
    elements.push_back(safe_zero(slot_type));
  if(elements.size() > cap)
    elements.resize(cap);
  return array_exprt{std::move(elements), data_array};
}

exprt python_convertert::build_list_data(
  exprt::operandst elements,
  const typet &element_type) const
{
  const typet list_type = python_list_type(element_type);
  // python_list_type may rewrite the element type (str -> handle on
  // the native string backend); honor the SLOT type it chose.
  const auto &data_array =
    to_array_type(to_struct_type(list_type).components()[1].type());
  const typet &slot_type = data_array.element_type();

  (void)slot_type;
  return build_list_data(std::move(elements), data_array);
}

exprt python_convertert::build_list_value(
  exprt::operandst elements,
  const typet &element_type) const
{
  const std::size_t n = elements.size();
  const typet list_type = python_list_type(element_type);
  exprt data = build_list_data(std::move(elements), element_type);
  return struct_exprt{
    {from_integer(n, signedbv_typet{64}), std::move(data)}, list_type};
}

std::optional<exprt>
python_convertert::list_literal_element(const exprt &data, std::size_t i) const
{
  if(data.id() == ID_array)
  {
    if(i < data.operands().size())
      return data.operands()[i];
    return {};
  }
  // Store chain: newest store wins — walk outside-in.
  const exprt *e = &data;
  while(e->id() == ID_with)
  {
    const auto &w = to_with_expr(*e);
    // with_exprt supports multi-update (where/value pairs); ours are
    // built pairwise, but decode the general form.
    for(std::size_t k = 1; k + 1 < e->operands().size(); k += 2)
    {
      const exprt &where = e->operands()[k];
      mp_integer idx_val;
      if(where.is_constant() && !to_integer(to_constant_expr(where), idx_val))
      {
        if(idx_val == static_cast<long long>(i))
          return e->operands()[k + 1];
      }
      else
        return {}; // symbolic store index — cannot decode statically
    }
    e = &w.old();
  }
  if(e->id() == ID_array_of)
    return to_array_of_expr(*e).what();
  return {};
}

std::optional<std::size_t>
python_convertert::list_literal_data_size(const exprt &data) const
{
  if(data.id() == ID_array)
    return data.operands().size();
  std::size_t max_idx = 0;
  bool any = false;
  const exprt *e = &data;
  while(e->id() == ID_with)
  {
    const auto &w = to_with_expr(*e);
    for(std::size_t k = 1; k + 1 < e->operands().size(); k += 2)
    {
      const exprt &where = e->operands()[k];
      mp_integer idx_val;
      if(!where.is_constant() || to_integer(to_constant_expr(where), idx_val))
        return {};
      const std::size_t iv = static_cast<std::size_t>(idx_val.to_ulong());
      max_idx = std::max(max_idx, iv + 1);
      any = true;
    }
    e = &w.old();
  }
  if(e->id() == ID_array_of)
    return any ? std::optional<std::size_t>{max_idx}
               : std::optional<std::size_t>{0};
  return {};
}

void python_convertert::emit_scan_bound_guard(
  const exprt &length,
  const source_locationt &loc,
  long cap,
  std::vector<codet> *sink)
{
  if(!python_smt_containers_flag())
    return;
  if(cap < 0)
    cap = static_cast<long>(PYTHON_MAX_LIST_LENGTH);
  std::vector<codet> &out = sink != nullptr ? *sink : pending_checks;
  binary_relation_exprt in_bounds{
    length, ID_le, from_integer(cap, length.type())};
  source_locationt aloc = loc;
  aloc.set_property_class("python-model-bound");
  aloc.set_comment(
    "operation scans a bounded prefix (verifier model bound; "
    "--python-smt-containers exact ops: list index/append/len/slice, "
    "dict insertion order/len)");
  code_assertt bound_assert{in_bounds};
  bound_assert.add_source_location() = aloc;
  out.push_back(std::move(bound_assert));
  code_assumet bound_assume{in_bounds};
  bound_assume.add_source_location() = loc;
  out.push_back(std::move(bound_assume));
}

std::optional<exprt::operandst>
python_convertert::list_literal_leading(const exprt &list_value) const
{
  if(list_value.id() != ID_struct || list_value.operands().size() < 2)
    return {};
  const exprt &len = list_value.operands()[0];
  mp_integer len_val;
  if(!len.is_constant() || to_integer(to_constant_expr(len), len_val))
    return {};
  const exprt &data = list_value.operands()[1];
  exprt::operandst out;
  const std::size_t n = static_cast<std::size_t>(len_val.to_ulong());
  for(std::size_t i = 0; i < n; i++)
  {
    auto el = list_literal_element(data, i);
    if(!el.has_value())
      return {};
    out.push_back(std::move(*el));
  }
  return out;
}

std::optional<std::vector<std::pair<exprt, exprt>>>
python_convertert::dict_literal_leading(const exprt &dict_value) const
{
  if(dict_value.id() != ID_struct || dict_value.operands().size() < 3)
    return {};
  const exprt &len = dict_value.operands()[0];
  mp_integer len_val;
  if(!len.is_constant() || to_integer(to_constant_expr(len), len_val))
    return {};
  const exprt &keys = dict_value.operands()[1];
  const exprt &vals = dict_value.operands()[2];
  std::vector<std::pair<exprt, exprt>> out;
  const std::size_t n = static_cast<std::size_t>(len_val.to_ulong());
  for(std::size_t i = 0; i < n; i++)
  {
    auto k = list_literal_element(keys, i);
    auto v = list_literal_element(vals, i);
    if(!k.has_value() || !v.has_value())
      return {};
    out.emplace_back(std::move(*k), std::move(*v));
  }
  return out;
}

exprt python_convertert::dict_key_for_slot(
  const exprt &key_elem,
  const typet &slot_type) const
{
  if(
    is_python_string_handle_type(key_elem.type()) &&
    is_python_string_handle_type(slot_type))
    return key_elem;
  exprt unboxed = python_dict_unbox_key(key_elem);
  if(unboxed.type() != slot_type && slot_type.id() != ID_empty)
  {
    // Value-preserving adaptation for remaining mismatches (e.g. a
    // refined-string key into a python_value slot) — never a raw pun.
    return const_cast<python_convertert *>(this)->coerce_element(
      unboxed, slot_type);
  }
  return unboxed;
}

// --- Closed-form iteration (comprehension-closedform plan, Tier 2+) ---
//
// ONE seam for every construct that consumes "each element of an
// iterable" in a boolean/aggregate position: genexp all()/any(),
// sequence membership, list equality. Each was (or would be) a
// separate bounded scan with a fail-closed python-model-bound guard
// under --python-smt-containers; a quantifier over the index range
// is EXACT at any symbolic length, and empty ranges give the PLR
// vacuous values for free (all([]) is True = vacuous forall,
// any([]) is False = vacuous exists, x in [] is False).

std::optional<python_convertert::iteration_viewt>
python_convertert::make_iteration_view(const exprt &iterable)
{
  if(!python_smt_containers_flag())
    return {};
  exprt cont = iterable;
  // A boxed list (python_value with a LIST payload): read the slot.
  if(is_python_value_type(cont.type()))
  {
    exprt lv = python_value_list(cont);
    if(!is_python_list_type(lv.type()))
      return {};
    cont = std::move(lv);
  }
  if(!is_python_list_type(cont.type()))
    return {};
  const auto &st = to_struct_type(cont.type());
  const auto &data_t = to_array_type(st.components()[1].type());
  iteration_viewt view;
  view.length = member_exprt{cont, "length", signedbv_typet{64}};
  view.data = member_exprt{cont, "data", data_t};
  view.element_type = data_t.element_type();
  return view;
}

symbol_exprt python_convertert::fresh_bound_index(const std::string &stem)
{
  static unsigned bound_ctr = 0;
  const std::string nm = stem + std::to_string(bound_ctr++);
  const irep_idt id{qualify_name(nm)};
  if(symbol_table.lookup(id) == nullptr)
  {
    // Symex L0 renaming requires bound variables to be registered
    // symbols (binder and body rename consistently -- the same route
    // C-frontend quantifiers and the Tier-1 comprehension take).
    symbolt s{id, signedbv_typet{64}, "python"};
    s.base_name = nm;
    s.is_lvalue = true;
    s.is_state_var = true;
    s.is_static_lifetime = current_function.empty();
    symbol_table.add(s);
  }
  return symbol_table.lookup_ref(id).symbol_expr();
}

exprt python_convertert::forall_in_range(
  const symbol_exprt &j,
  const exprt &length,
  exprt pred)
{
  const exprt lo = from_integer(0, j.type());
  and_exprt range{
    binary_relation_exprt{lo, ID_le, j},
    binary_relation_exprt{j, ID_lt, length}};
  return forall_exprt{j, implies_exprt{std::move(range), std::move(pred)}};
}

exprt python_convertert::exists_in_range(
  const symbol_exprt &j,
  const exprt &length,
  exprt pred)
{
  const exprt lo = from_integer(0, j.type());
  and_exprt range{
    binary_relation_exprt{lo, ID_le, j},
    binary_relation_exprt{j, ID_lt, length}};
  return exists_exprt{j, and_exprt{std::move(range), std::move(pred)}};
}

std::function<exprt(const exprt &)>
python_convertert::dict_key_matcher(const exprt &keys, const exprt &key)
{
  const auto &keys_arr = to_array_type(keys.type());
  const bool keys_are_strings = is_python_string_type(
    python_dict_logical_key_type(keys_arr.element_type()));
  const bool keys_are_values = is_python_value_type(keys_arr.element_type());
  const bool slots_are_handles =
    is_python_string_handle_type(keys_arr.element_type());
  // Wrap the query once (not per slot): value_equal compares it
  // against each value-typed key in the value domain.
  const exprt wrapped_key = keys_are_values ? wrap_value(key) : key;
  return [this,
          keys,
          key,
          wrapped_key,
          keys_are_strings,
          keys_are_values,
          slots_are_handles](const exprt &idx_e) -> exprt
  {
    exprt key_i = python_dict_unbox_key(index_exprt{keys, idx_e});
    if(keys_are_values)
      return value_equal(key_i, wrapped_key);
    // HANDLE probe against handle slots: compare by strtab DENOTATION
    // (String equality), never by handle identity -- string_to_handle
    // mints a FRESH handle per allocation, so two handles for equal
    // strings need not be bit-equal, and the fallback String->bv64
    // typecast severed the value entirely (a nondet under the binder,
    // which the quantifier-safety gate then rejected -- observed at
    // the d.get() site, which pre-coerces its probe to a handle).
    if(slots_are_handles && is_python_string_handle_type(key.type()))
      return equal_exprt{key_i, python_dict_unbox_key(key)};
    if(keys_are_strings && is_python_string_type(key.type()))
    {
      if(key_i.type() != key.type())
        key_i = safe_typecast(key_i, key.type());
      return string_equal(key_i, key);
    }
    if(key_i.type() != key.type())
      key_i = safe_typecast(key_i, key.type());
    // Float keys compare IEEE-wise (parity with container_slot_equal,
    // the bounded scans' comparator).
    if(key_i.type().id() == ID_floatbv)
      return exprt{ieee_float_equal_exprt{key_i, key}};
    return equal_exprt{key_i, key};
  };
}

python_convertert::dict_witness_resultt python_convertert::dict_lookup_witness(
  const exprt &dict_value,
  const exprt &key)
{
  const auto &dict_st = to_struct_type(dict_value.type());
  const auto &keys_type = to_array_type(dict_st.components()[1].type());
  member_exprt length{dict_value, "length", signedbv_typet{64}};
  member_exprt keys{dict_value, "keys", keys_type};
  return dict_lookup_witness_members(keys, length, key);
}

python_convertert::dict_witness_resultt
python_convertert::dict_lookup_witness_members(
  const exprt &keys,
  const exprt &length,
  const exprt &key,
  std::function<exprt(const exprt &)> matcher)
{
  dict_witness_resultt r;
  if(!python_smt_containers_flag())
    return r;
  // Materialise an effectful key (embedded nondet / call result /
  // string-op side effect) into a temp: the match term is replicated
  // under a forall binder, where side effects are forbidden
  // (goto-convert aborts) and re-evaluation would be wrong anyway.
  exprt key_v = key;
  if(has_subexpr(key_v, ID_side_effect))
  {
    symbol_exprt tmp = mint_witness_symbol("__dk_key_", key_v.type());
    pending_checks.push_back(code_frontend_assignt{tmp, key_v});
    key_v = tmp;
  }
  auto match_at = matcher ? std::move(matcher) : dict_key_matcher(keys, key_v);

  // Eligibility: the match term must be quantifier-safe and its
  // construction side-effect-free (same gates as the list.index lift).
  const std::size_t pc_before = pending_checks.size();
  symbol_exprt qj = fresh_bound_index("__dk_j_");
  exprt qmatch = match_at(qj);
  if(
    pending_checks.size() != pc_before || qmatch.is_nil() ||
    !quantifier_safe_term(qmatch))
  {
    pending_checks.erase(
      pending_checks.begin() + pc_before, pending_checks.end());
    return r;
  }

  const typet w_t = signedbv_typet{64};
  symbol_exprt w = mint_witness_symbol("__dk_w_", w_t);
  symbol_exprt pj = fresh_bound_index("__dk_p_");
  exprt w_min = forall_in_range(pj, w, not_exprt{match_at(pj)});
  exprt in_len = binary_relation_exprt{w, ID_lt, length};
  pending_checks.push_back(code_assumet{and_exprt{
    binary_relation_exprt{from_integer(0, w_t), ID_le, w},
    binary_relation_exprt{w, ID_le, length},
    std::move(w_min),
    implies_exprt{in_len, match_at(w)}}});
  r.found = std::move(in_len);
  r.index = std::move(w);
  return r;
}

void python_convertert::emit_dict_store(
  code_blockt &block,
  const exprt &keys_arr,
  const exprt &vals_arr,
  const exprt &length,
  const exprt &typed_key,
  const exprt &typed_val,
  const source_locationt &loc)
{
  // Any dict store invalidates the TypedDict-field read memo
  // (conservative: the memo only serves the SoA provenance hooks).
  td_field_read_cache.clear();

  const typet len_t = signedbv_typet{64};
  // Fresh found flag (shared by both encodings).
  static unsigned ds_ctr = 0;
  const std::string fn = "__dstore_fnd_" + std::to_string(ds_ctr++);
  const irep_idt fi{qualify_name(fn)};
  if(symbol_table.lookup(fi) == nullptr)
  {
    symbolt fs{fi, bool_typet{}, "python"};
    fs.base_name = fn;
    fs.is_lvalue = true;
    fs.is_state_var = true;
    fs.is_static_lifetime = current_function.empty();
    symbol_table.add(fs);
  }
  symbol_exprt found = symbol_table.lookup_ref(fi).symbol_expr();

  bool lifted_ok = false;
  if(python_smt_containers_flag())
  {
    // The witness's defining assume must be a STATEMENT in `block`
    // (not a pending check hoisted before the enclosing statement):
    // the constraint refers to the CURRENT keys/length, which earlier
    // statements in this very block may have just written.
    const std::size_t pc_before = pending_checks.size();
    auto lifted = dict_lookup_witness_members(keys_arr, length, typed_key);
    if(lifted.found.is_not_nil())
    {
      for(std::size_t k = pc_before; k < pending_checks.size(); ++k)
        block.add(std::move(pending_checks[k]));
      pending_checks.erase(
        pending_checks.begin() + pc_before, pending_checks.end());
      block.add(code_frontend_assignt{found, lifted.found});
      code_blockt replace;
      replace.add(
        code_frontend_assignt{index_exprt{vals_arr, lifted.index}, typed_val});
      block.add(code_ifthenelset{lifted.found, std::move(replace)});
      lifted_ok = true;
    }
    else
      pending_checks.erase(
        pending_checks.begin() + pc_before, pending_checks.end());
  }
  if(!lifted_ok)
  {
    block.add(code_frontend_assignt{found, false_exprt{}});
    for(std::size_t i = 0; i < static_cast<std::size_t>(PYTHON_MAX_DICT_SIZE);
        i++)
    {
      exprt idx = from_integer(i, len_t);
      std::vector<codet> eq_seq;
      exprt match = dict_slot_match(keys_arr, length, i, typed_key, &eq_seq);
      for(auto &c : eq_seq)
        block.add(std::move(c));
      code_blockt update;
      update.add(code_frontend_assignt{index_exprt{vals_arr, idx}, typed_val});
      update.add(code_frontend_assignt{found, true_exprt{}});
      block.add(code_ifthenelset{std::move(match), std::move(update)});
    }
  }
  // INSERT arm. With the witness encoding the length-indexed store
  // into the INFINITE arrays needs no capacity guard; the bounded
  // fallback keeps the fail-closed cut.
  code_blockt append;
  if(!lifted_ok)
    emit_capacity_guard(append, length, PYTHON_MAX_DICT_SIZE, loc);
  append.add(code_frontend_assignt{
    index_exprt{keys_arr, length},
    coerce_element(typed_key, to_array_type(keys_arr.type()).element_type())});
  append.add(code_frontend_assignt{index_exprt{vals_arr, length}, typed_val});
  append.add(
    code_frontend_assignt{length, plus_exprt{length, from_integer(1, len_t)}});
  block.add(code_ifthenelset{not_exprt{found}, std::move(append)});
}

bool python_convertert::quantifier_safe_term(const exprt &e) const
{
  // A term placed under a forall/exists binder must be a pure SMT
  // term. Refined-string function applications (cprover_string_*)
  // are NOT: the string-refinement solver instantiates their axioms
  // outside any binder scope and is not quantifier-aware -- a bound
  // index inside such an application risks wrong axiom
  // instantiation, not just slowness. Under the native SMT-strings
  // backend string equalities lower to String-sort terms and strtab
  // UF applications, which quantify soundly.
  // Side effects (nondet, function calls, allocations) can never
  // appear under a binder: goto-convert's clean_expr enforces
  // "quantifier must not contain side effects" with an invariant
  // abort. Callers materialise effectful operands into temps first
  // (see dict_lookup_witness_members); this is the backstop.
  if(has_subexpr(e, ID_side_effect))
    return false;
  if(python_smt_string_native_flag())
    return true;
  return !has_subexpr(e, ID_function_application);
}

symbol_exprt python_convertert::mint_witness_symbol(
  const std::string &stem,
  const typet &result_type)
{
  // The witness pattern shared by list.index (first-occurrence
  // minimality) and min/max (extremum): mint a fresh nondet-
  // initialized result symbol; the caller then ASSUMEs its defining
  // constraints, GUARDED (assume(guard => constraints)) so an
  // empty/absent case -- which per PLR raises instead of producing
  // a value -- cannot over-constrain the path. Callers must ensure
  // the constraints are SATISFIABLE whenever the guard holds (the
  // first occurrence exists whenever some occurrence does; an
  // extremum exists whenever the sequence is non-empty), so the
  // assume never prunes feasible paths.
  static unsigned witness_ctr = 0;
  const std::string nm = stem + std::to_string(witness_ctr++);
  const irep_idt id{qualify_name(nm)};
  if(symbol_table.lookup(id) == nullptr)
  {
    symbolt s{id, result_type, "python"};
    s.base_name = nm;
    s.is_lvalue = true;
    s.is_state_var = true;
    s.is_static_lifetime = current_function.empty();
    symbol_table.add(s);
  }
  const symbol_exprt w = symbol_table.lookup_ref(id).symbol_expr();
  pending_checks.push_back(code_frontend_assignt{
    w, side_effect_expr_nondett{result_type, source_locationt{}}});
  return w;
}
