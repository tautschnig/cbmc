/// Python to GOTO converter — control-flow statement
/// handlers: assert (PLR §7.3), if (§8.1), while (§8.2),
/// for (§8.3), return (§7.6).
///
/// Extracted from python_converter.cpp to reduce the main file size.
/// All logic and class-member state remains unchanged — this file is
/// a pure source-split.

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/json.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/symbol.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

// PLR §7.3: The assert statement
// "Assert statements are a convenient way to insert debugging assertions
// into a program."
codet python_convertert::convert_assert(const jsont &stmt)
{
  exprt test = convert_expression(json_member(stmt, "test"));
  if(test.is_nil())
  {
    // Fail-closed (PLR §7.3: the assert statement evaluates its
    // condition — an assertion the front-end cannot encode must never
    // be silently dropped, or VERIFICATION SUCCESSFUL means "I didn't
    // encode your assertion" (a false-proof class reported against
    // real sentinel-pattern code). Emit a definite-failure property
    // carrying its own property class so it is distinguishable from a
    // genuine assertion violation.
    source_locationt loc = get_location(stmt);
    loc.set_property_class("python-not-encoded");
    loc.set_comment(
      "assertion could not be encoded (unresolved name or unsupported "
      "construct)");
    code_assertt assertion{false_exprt{}};
    assertion.add_source_location() = loc;
    return std::move(assertion);
  }

  // Ensure the test is boolean
  if(test.type() != bool_typet{})
    test = safe_typecast(test, bool_typet{});

  source_locationt loc = get_location(stmt);
  loc.set_property_class("assertion");
  loc.set_comment("Python assertion");

  code_assertt assertion{test};
  assertion.add_source_location() = loc;
  return std::move(assertion);
}

// PLR §8.1: The if statement
// "The if statement is used for conditional execution."
codet python_convertert::convert_if(const jsont &stmt)
{
  // PLR data flow: recognize the idiom
  //     if K not in D:
  //         D[K] = default_value
  // After the if, K is guaranteed to be in D in both branches
  // (body-path inserted it; else-path already had it).
  // Record (D, structural-key-of-K) in dict_guaranteed_keys so
  // subsequent D[K'] subscript reads with structurally-matching
  // K' can skip the KeyError check.
  {
    const jsont &test_node = json_member(stmt, "test");
    if(is_node_type(test_node, "Compare"))
    {
      const jsont &ops = json_member(test_node, "ops");
      const jsont &comps = json_member(test_node, "comparators");
      if(
        ops.is_array() && comps.is_array() && as_array(ops).size() == 1 &&
        as_array(comps).size() == 1 &&
        is_node_type(*as_array(ops).begin(), "NotIn"))
      {
        const jsont &key_ast = json_member(test_node, "left");
        const jsont &dict_ast = *as_array(comps).begin();
        if(is_node_type(dict_ast, "Name"))
        {
          std::string dict_name = json_string(json_member(dict_ast, "id"));
          irep_idt dict_id{qualify_name(dict_name)};
          // Check first statement of body is
          // 'D[same-K] = something'.
          const jsont &body = json_member(stmt, "body");
          if(body.is_array() && !as_array(body).empty())
          {
            const jsont &first = *as_array(body).begin();
            if(is_node_type(first, "Assign"))
            {
              const jsont &targets = json_member(first, "targets");
              if(targets.is_array() && !as_array(targets).empty())
              {
                const jsont &target0 = *as_array(targets).begin();
                if(is_node_type(target0, "Subscript"))
                {
                  const jsont &tvalue = json_member(target0, "value");
                  const jsont &tslice = json_member(target0, "slice");
                  if(
                    is_node_type(tvalue, "Name") &&
                    json_string(json_member(tvalue, "id")) == dict_name)
                  {
                    // Compare structural key.
                    auto ast_key = [&](const jsont &n) -> std::string
                    {
                      if(is_node_type(n, "Name"))
                        return "Name:" + json_string(json_member(n, "id"));
                      if(is_node_type(n, "Constant"))
                      {
                        const jsont &cv = json_member(n, "value");
                        if(cv.is_string())
                          return "Const:" + cv.value;
                      }
                      return std::string{};
                    };
                    std::string tk = ast_key(tslice);
                    std::string kk = ast_key(key_ast);
                    if(!tk.empty() && tk == kk)
                      dict_guaranteed_keys[dict_id].insert(tk);
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  exprt test = convert_expression(json_member(stmt, "test"));
  if(test.is_nil())
    return code_skipt{};

  if(test.type() != bool_typet{})
    test = safe_typecast(test, bool_typet{});

  // Flush pending checks from test expression evaluation
  code_blockt pre_checks;
  for(auto &check : pending_checks)
    pre_checks.add(std::move(check));
  pending_checks.clear();

  // Save version state before branches
  auto saved_versions = variable_versions;
  // PLR control-flow correctness: capture the conversion-time
  // tracking maps before processing either arm, so each arm sees
  // the pre-branch state and we can merge their post-states at
  // the join. See tracking_snapshott in python_converter.h.
  tracking_snapshott pre_branch_tracking = snapshot_tracking();
  if_else_depth++;

  // Path-sensitive truthiness: when the test is a bare Name
  // referring to a list-typed or string-typed variable, the
  // body executes only when the value is non-empty (Python
  // truthiness for lists/strings is `len > 0`). Record
  // min-length 1 for the body conversion to suppress spurious
  // IndexError checks on `L[0]` / `s[0]` etc. inside
  // `if L: L[0]` / `if s: s[0]` patterns. Restored on body
  // exit so it doesn't leak into the else branch or the
  // post-if continuation.
  std::vector<irep_idt> body_list_bounds_added;
  std::map<irep_idt, mp_integer> body_list_bounds_overwritten;
  std::vector<irep_idt> body_string_bounds_added;
  std::map<irep_idt, mp_integer> body_string_bounds_overwritten;
  bool then_branch_inverted = false;
  {
    const jsont &test_node = json_member(stmt, "test");
    const jsont *target_node = &test_node;
    // Handle `if not x: ... else: <body uses x>`: the else-branch
    // gets the non-empty fact, not the body. We don't yet specially
    // handle this — only the direct `if x:` form below.
    if(
      is_node_type(test_node, "UnaryOp") &&
      json_string(json_member(json_member(test_node, "op"), "_type")) == "Not")
    {
      then_branch_inverted = true;
      target_node = &json_member(test_node, "operand");
    }
    if(!then_branch_inverted && is_node_type(*target_node, "Name"))
    {
      std::string vname = json_string(json_member(*target_node, "id"));
      irep_idt vid{qualify_name(vname)};
      const symbolt *sym = symbol_table.lookup(vid);
      if(sym != nullptr && is_python_list_type(sym->type))
      {
        auto it = list_min_lengths.find(vid);
        if(it == list_min_lengths.end())
        {
          list_min_lengths[vid] = mp_integer{1};
          body_list_bounds_added.push_back(vid);
        }
        else if(it->second < 1)
        {
          body_list_bounds_overwritten[vid] = it->second;
          it->second = mp_integer{1};
        }
      }
      else if(sym != nullptr && is_python_string_type(sym->type))
      {
        auto it = string_min_lengths.find(vid);
        if(it == string_min_lengths.end())
        {
          string_min_lengths[vid] = mp_integer{1};
          body_string_bounds_added.push_back(vid);
        }
        else if(it->second < 1)
        {
          body_string_bounds_overwritten[vid] = it->second;
          it->second = mp_integer{1};
        }
      }
    }
  }

  // Convert body
  code_blockt then_block;
  const jsont &body = json_member(stmt, "body");
  if(body.is_array())
  {
    for(const auto &s : as_array(body))
      then_block.add(convert_statement(s));
  }

  // Restore list_min_lengths to pre-body state.
  // Restore list_min_lengths and string_min_lengths to pre-body state.
  for(const auto &id : body_list_bounds_added)
    list_min_lengths.erase(id);
  for(const auto &kv : body_list_bounds_overwritten)
    list_min_lengths[kv.first] = kv.second;
  for(const auto &id : body_string_bounds_added)
    string_min_lengths.erase(id);
  for(const auto &kv : body_string_bounds_overwritten)
    string_min_lengths[kv.first] = kv.second;

  // Save then-branch versions, restore for else branch
  auto then_versions = variable_versions;
  variable_versions = saved_versions;
  // Capture the then-branch's post-state of the tracking maps,
  // then restore the pre-branch snapshot before processing the
  // else branch so it starts from the same state.
  tracking_snapshott then_tracking = snapshot_tracking();
  restore_tracking(pre_branch_tracking);

  // Convert orelse (may be empty, elif chain, or else block)
  const jsont &orelse = json_member(stmt, "orelse");
  if(orelse.is_array() && !as_array(orelse).empty())
  {
    code_blockt else_block;
    for(const auto &s : as_array(orelse))
      else_block.add(convert_statement(s));

    // Merge: if both branches modified the same variable, keep the
    // then-branch version (the else branch's version is only live on
    // the else path, which CBMC handles via the if-then-else structure)
    variable_versions = then_versions;
    // Capture the else-branch's post-state and merge it with the
    // then-branch's. After merge_tracking, the live tracking maps
    // contain only entries that both arms agree on; disagreements
    // are dropped, falling through to the SSA-aware solver.
    tracking_snapshott else_tracking = snapshot_tracking();
    merge_tracking(then_tracking, else_tracking);

    if_else_depth--;

    code_ifthenelset if_stmt{
      test, std::move(then_block), std::move(else_block)};
    if_stmt.add_source_location() = get_location(stmt);
    if(!pre_checks.statements().empty())
    {
      pre_checks.add(std::move(if_stmt));
      return std::move(pre_checks);
    }
    return std::move(if_stmt);
  }
  else
  {
    variable_versions = then_versions;
    // No else branch: the else-arm post-state equals the
    // pre-branch snapshot. Merge the then-arm's tracking with
    // that snapshot so entries that survive only on the
    // then-path are dropped (they're path-dependent).
    merge_tracking(then_tracking, pre_branch_tracking);
    if_else_depth--;

    code_ifthenelset if_stmt{test, std::move(then_block)};
    if_stmt.add_source_location() = get_location(stmt);
    if(!pre_checks.statements().empty())
    {
      pre_checks.add(std::move(if_stmt));
      return std::move(pre_checks);
    }
    return std::move(if_stmt);
  }
}

// PLR §8.2: The while statement
// "The while statement is used for repeated execution as long as an
// expression is true."
codet python_convertert::convert_while(const jsont &stmt)
{
  // PLR §8.2: while test: body [else: orelse]
  // If orelse is non-empty, create a break-flag symbol so the
  // else clause is skipped on break.
  const jsont &orelse_stmts = json_member(stmt, "orelse");
  bool have_orelse = orelse_stmts.is_array() && !as_array(orelse_stmts).empty();

  irep_idt break_flag_id;
  if(have_orelse)
  {
    static unsigned while_else_ctr = 0;
    std::string flag_name = "__loop_broke_" + std::to_string(while_else_ctr++);
    std::string flag_q = qualify_name(flag_name);
    break_flag_id = irep_idt{flag_q};
    if(symbol_table.lookup(break_flag_id) == nullptr)
    {
      symbolt fs{break_flag_id, bool_typet{}, "python"};
      fs.base_name = flag_name;
      fs.is_lvalue = true;
      fs.is_state_var = true;
      fs.is_static_lifetime = current_function.empty();
      symbol_table.add(fs);
    }
    loop_break_flags.push_back(break_flag_id);
  }
  // Convert the test. Walrus (NamedExpr) inside the test
  // generates pending_checks that bind variables used by
  // the body; those bindings must re-execute every
  // iteration, not only at loop entry. We rewrite
  //
  //     while cond: body
  //
  // as
  //
  //     while true:
  //         <test-side-effects>
  //         if not cond: break
  //         body
  //
  // to put the pending_checks inside the loop body. This
  // is a no-op when the test has no side effects.
  std::vector<codet> test_side_effects;
  pending_checks.swap(test_side_effects);
  exprt test = convert_expression(json_member(stmt, "test"));
  std::vector<codet> test_pending;
  pending_checks.swap(test_pending);
  pending_checks.swap(test_side_effects); // restore outer state
  if(test.is_nil())
    return code_skipt{};

  if(test.type() != bool_typet{})
    test = safe_typecast(test, bool_typet{});

  code_blockt body_block;
  const jsont &body = json_member(stmt, "body");
  if(body.is_array())
  {
    // PLR §8.2: invalidate constant tracking for variables
    // assigned inside the loop body. See
    // invalidate_loop_writes — without this, a body expression
    // referencing a loop-mutated variable folds against its
    // pre-loop value.
    invalidate_loop_writes(body);
    loop_depth++;
    for(const auto &s : as_array(body))
      body_block.add(convert_statement(s));
    loop_depth--;
  }

  auto wrap_with_else = [&](codet &&loop_code) -> codet
  {
    if(!have_orelse)
      return std::move(loop_code);

    loop_break_flags.pop_back();

    code_blockt outer;
    const symbolt &flag_sym = symbol_table.lookup_ref(break_flag_id);
    outer.add(code_frontend_assignt{flag_sym.symbol_expr(), false_exprt{}});
    outer.add(std::move(loop_code));

    code_blockt else_block;
    for(const auto &s : as_array(orelse_stmts))
      else_block.add(convert_statement(s));
    outer.add(code_ifthenelset{
      not_exprt{flag_sym.symbol_expr()}, std::move(else_block)});
    return std::move(outer);
  };

  if(!test_pending.empty())
  {
    // Wrap: while(true) { side_effects; if(!test) break; body; }
    code_blockt loop_body;
    for(auto &c : test_pending)
      loop_body.add(std::move(c));
    loop_body.add(code_ifthenelset{not_exprt{test}, code_breakt{}});
    for(const auto &s : body_block.statements())
      loop_body.add(s);
    code_whilet while_stmt{true_exprt{}, std::move(loop_body)};
    while_stmt.add_source_location() = get_location(stmt);
    return wrap_with_else(std::move(while_stmt));
  }

  code_whilet while_stmt{test, std::move(body_block)};
  while_stmt.add_source_location() = get_location(stmt);
  return wrap_with_else(std::move(while_stmt));
}

// PLR §8.3: The for statement
// "The for statement is used to iterate over the elements of a
// sequence (such as a string, tuple or list) or other iterable."
// We desugar to while loops with explicit index variables.
// "The for statement is used to iterate over the elements of a sequence
// (such as a string, tuple or list) or other iterable object."
codet python_convertert::convert_for(const jsont &stmt)
{
  const jsont &target = json_member(stmt, "target");
  const jsont &iter = json_member(stmt, "iter");
  source_locationt loc = get_location(stmt);
  typet int_type = python_int_type();

  // PLR §8.3: the loop variable(s) are (re)assigned each iteration, so
  // invalidate their tracking -- else `b = 1; for b in [3]: pass; t[b]` folds
  // t[b] on the stale b=1 (found by proactively probing the reassignment
  // whole-group). Recurse into tuple/list/starred targets.
  {
    std::function<void(const jsont &)> inv = [&](const jsont &tn)
    {
      if(is_node_type(tn, "Name"))
        invalidate_reassigned_symbol(
          irep_idt{qualify_name(json_string(json_member(tn, "id")))});
      else if(
        (is_node_type(tn, "Tuple") || is_node_type(tn, "List")) &&
        json_member(tn, "elts").is_array())
        for(const auto &e : as_array(json_member(tn, "elts")))
          inv(e);
      else if(is_node_type(tn, "Starred"))
        inv(json_member(tn, "value"));
    };
    inv(target);
  }

  // PLR §8.3: for target in iter: body [else: orelse]
  // If orelse is non-empty, create a break-flag symbol so the
  // else clause is skipped on break.
  const jsont &orelse_stmts_for = json_member(stmt, "orelse");
  bool have_orelse_for =
    orelse_stmts_for.is_array() && !as_array(orelse_stmts_for).empty();
  irep_idt for_break_flag_id;
  if(have_orelse_for)
  {
    static unsigned for_else_ctr = 0;
    std::string flag_name = "__loop_broke_f_" + std::to_string(for_else_ctr++);
    std::string flag_q = qualify_name(flag_name);
    for_break_flag_id = irep_idt{flag_q};
    if(symbol_table.lookup(for_break_flag_id) == nullptr)
    {
      symbolt fs{for_break_flag_id, bool_typet{}, "python"};
      fs.base_name = flag_name;
      fs.is_lvalue = true;
      fs.is_state_var = true;
      fs.is_static_lifetime = current_function.empty();
      symbol_table.add(fs);
    }
    loop_break_flags.push_back(for_break_flag_id);
  }

  // RAII-like guard: any early return from convert_for must
  // still pop the break-flag stack. For the non-orelse case
  // this is a no-op.
  // Also prepends `iter_none_check` (populated after we
  // convert the iterable) so the PLR §6.13 'iterating None'
  // TypeError property fires before the loop body, regardless
  // of which sub-handler builds the loop.
  code_blockt iter_none_check;
  auto finalize_for = [&](codet &&c) -> codet
  {
    code_blockt outer;
    for(auto &s : iter_none_check.statements())
      outer.add(std::move(s));
    if(!have_orelse_for)
    {
      if(outer.statements().empty())
        return std::move(c);
      outer.add(std::move(c));
      return std::move(outer);
    }
    loop_break_flags.pop_back();
    const symbolt &flag_sym = symbol_table.lookup_ref(for_break_flag_id);
    outer.add(code_frontend_assignt{flag_sym.symbol_expr(), false_exprt{}});
    outer.add(std::move(c));
    code_blockt else_block;
    for(const auto &s : as_array(orelse_stmts_for))
      else_block.add(convert_statement(s));
    outer.add(code_ifthenelset{
      not_exprt{flag_sym.symbol_expr()}, std::move(else_block)});
    return std::move(outer);
  };

  // Flush the pending checks generated by converting the loop-HEADER
  // expression(s) (subscript bounds/tag obligations, conditional exceptions,
  // temp materialisations) into iter_none_check, which finalize_for prepends
  // BEFORE the loop on every return path. Without this they were silently
  // DROPPED: the body-conversion prologue clears pending_checks, so
  // finalize-time flushing is too late -- e.g. `for c in xs[5]` on a
  // python_value xs lost its IndexError obligation, and
  // `for i in range(xs[5])` likewise (false proofs; the concrete-list path
  // survived only because it asserts inline).
  auto flush_header_checks = [&]()
  {
    for(auto &pc : pending_checks)
      iter_none_check.add(std::move(pc));
    pending_checks.clear();
  };

  std::string var_name = json_string(json_member(target, "id"));
  // For tuple targets (for a, b in ...), use a synthetic name
  if(var_name.empty() && is_node_type(target, "Tuple"))
  {
    static unsigned tuple_iter_ctr = 0;
    var_name = "__tuple_iter_" + std::to_string(tuple_iter_ctr++);
  }
  std::string qualified_name = qualify_name(var_name);

  // PLR §6.10: for x in reversed(range(...)) — a common idiom
  // for descending iteration. Detect the call shape at the AST
  // level and lower as a range loop with reversed bounds and
  // negated step. Equivalent to:
  //   reversed(range(stop))             == range(stop-1, -1, -1)
  //   reversed(range(start, stop))      == range(stop-1, start-1, -1)
  //   reversed(range(start, stop, step))
  //                  step > 0           ==
  //     range(start + ((stop-1-start)//step)*step, start-1, -step)
  //                  step < 0           == similar with sign flip
  // For a constant-step input we materialise the equivalent
  // bounds at conversion time; for symbolic step we punt to the
  // generic iter path below.
  bool is_reversed_range =
    is_node_type(iter, "Call") &&
    is_node_type(json_member(iter, "func"), "Name") &&
    json_string(json_member(json_member(iter, "func"), "id")) == "reversed" &&
    json_member(iter, "args").is_array() &&
    !as_array(json_member(iter, "args")).empty() &&
    is_node_type(*as_array(json_member(iter, "args")).begin(), "Call") &&
    json_string(json_member(
      json_member(*as_array(json_member(iter, "args")).begin(), "func"),
      "id")) == "range";
  if(is_reversed_range)
  {
    const jsont &inner_range = *as_array(json_member(iter, "args")).begin();
    const jsont &range_args = json_member(inner_range, "args");
    if(!range_args.is_array() || as_array(range_args).empty())
      return finalize_for(code_skipt{});

    exprt rstart, rstop, rstep;
    if(as_array(range_args).size() == 1)
    {
      rstart = from_integer(0, int_type);
      rstop = convert_expression(*as_array(range_args).begin());
      rstep = from_integer(1, int_type);
    }
    else if(as_array(range_args).size() >= 2)
    {
      rstart = convert_expression(*as_array(range_args).begin());
      rstop = convert_expression(*std::next(as_array(range_args).begin(), 1));
      if(as_array(range_args).size() >= 3)
        rstep = convert_expression(*std::next(as_array(range_args).begin(), 2));
      else
        rstep = from_integer(1, int_type);
    }
    else
    {
      rstart = from_integer(0, int_type);
      rstop = from_integer(0, int_type);
      rstep = from_integer(1, int_type);
    }
    flush_header_checks();
    rstart = safe_typecast(rstart, int_type);
    rstop = safe_typecast(rstop, int_type);
    rstep = safe_typecast(rstep, int_type);

    // Compute the last value the forward range would produce:
    //   last = start + ((stop - start - 1) / step) * step  (step > 0)
    //   last = start + ((stop - start + 1) / step) * step  (step < 0)
    // Equivalent simpler form for constant step:
    //   step == 1:  last = stop - 1
    //   step == -1: last = stop + 1
    // For other step, fall through to the generic path.
    bool reversible = false;
    exprt new_start, new_stop, new_step;
    // PLR §6.3.1: the AST for negative integer literals is
    // UnaryOp(USub, Constant(N)). Fold so the constant detection
    // below recognises -3 as a constant. Stripping a top-level
    // typecast(int) is also needed because safe_typecast may
    // wrap the constant.
    auto fold_const = [](exprt e) -> exprt
    {
      if(e.id() == ID_typecast && e.operands().size() == 1)
        e = e.operands()[0];
      if(
        e.id() == ID_unary_minus && e.operands().size() == 1 &&
        e.operands()[0].is_constant())
      {
        mp_integer v;
        if(!to_integer(to_constant_expr(e.operands()[0]), v))
          return from_integer(-v, e.type());
      }
      return e;
    };
    exprt rstep_const = fold_const(rstep);
    if(rstep_const.is_constant())
    {
      mp_integer sv;
      if(!to_integer(to_constant_expr(rstep_const), sv))
      {
        if(sv == 1)
        {
          new_start = minus_exprt{rstop, from_integer(1, int_type)};
          new_stop = minus_exprt{rstart, from_integer(1, int_type)};
          new_step = from_integer(-1, int_type);
          reversible = true;
        }
        else if(sv == -1)
        {
          new_start = plus_exprt{rstop, from_integer(1, int_type)};
          new_stop = plus_exprt{rstart, from_integer(1, int_type)};
          new_step = from_integer(1, int_type);
          reversible = true;
        }
        else if(sv > 0)
        {
          // last = start + ((stop - start - 1) / step) * step
          exprt diff_minus_1 =
            minus_exprt{minus_exprt{rstop, rstart}, from_integer(1, int_type)};
          // For an empty forward range (stop <= start), the
          // reversed iteration is also empty; we'd build
          // last < start which never matches. Sound.
          exprt last = plus_exprt{
            rstart, mult_exprt{div_exprt{diff_minus_1, rstep}, rstep}};
          new_start = std::move(last);
          new_stop = minus_exprt{rstart, from_integer(1, int_type)};
          // Reverse step is the negation of the original.
          new_step = from_integer(-sv, int_type);
          reversible = true;
        }
        else if(sv < 0)
        {
          exprt diff_plus_1 =
            plus_exprt{minus_exprt{rstop, rstart}, from_integer(1, int_type)};
          exprt last = plus_exprt{
            rstart, mult_exprt{div_exprt{diff_plus_1, rstep}, rstep}};
          new_start = std::move(last);
          new_stop = plus_exprt{rstart, from_integer(1, int_type)};
          new_step = from_integer(-sv, int_type);
          reversible = true;
        }
      }
    }

    if(reversible)
    {
      const jsont &body = json_member(stmt, "body");
      irep_idt symbol_id{qualified_name};
      if(symbol_table.lookup(symbol_id) == nullptr)
      {
        symbolt new_symbol{symbol_id, int_type, "python"};
        new_symbol.base_name = var_name;
        new_symbol.location = loc;
        new_symbol.is_lvalue = true;
        new_symbol.is_state_var = true;
        symbol_table.add(new_symbol);
      }
      symbol_exprt loop_sym = symbol_table.lookup_ref(symbol_id).symbol_expr();
      code_frontend_assignt init{loop_sym, new_start};
      init.add_source_location() = loc;
      code_blockt body_block;
      {
        invalidate_loop_writes(body);
        loop_depth++;
        for(const auto &s : as_array(body))
          body_block.add(convert_statement(s));
        loop_depth--;
      }
      mp_integer ns_val;
      to_integer(to_constant_expr(new_step), ns_val);
      exprt cond = ns_val < 0
                     ? binary_relation_exprt{loop_sym, ID_gt, new_stop}
                     : binary_relation_exprt{loop_sym, ID_lt, new_stop};
      // C-style for, NOT while+tail-increment: `continue` must jump to
      // the INCREMENT (PLR §8.3 -- the next loop iteration), and the
      // while encoding's continue re-tested the condition with the
      // induction variable unchanged: an INFINITE goto loop, silently
      // truncated under --no-unwinding-assertions into VACUOUS proofs
      // (the whole for+continue family; ESBMC for_range_continue_fail).
      code_fort while_stmt{
        nil_exprt{},
        cond,
        side_effect_expr_assignt{loop_sym, plus_exprt{loop_sym, new_step}, loc},
        std::move(body_block)};
      while_stmt.add_source_location() = loc;
      code_blockt result;
      result.add(std::move(init));
      result.add(std::move(while_stmt));
      return finalize_for(std::move(result));
    }
  }

  // Check for range() call
  if(
    is_node_type(iter, "Call") &&
    json_string(json_member(json_member(iter, "func"), "id")) == "range")
  {
    const jsont &range_args = json_member(iter, "args");
    // PLR §6.10.2: range() takes 1..3 positional args; 0 or >3 is a
    // TypeError raised before the loop runs. Capture the emitted check
    // into the loop preamble (pending_checks won't survive body
    // conversion).
    {
      auto saved_pc = pending_checks;
      pending_checks.clear();
      bool fatal = emit_range_arg_checks(range_args, nullptr);
      code_blockt errb;
      for(auto &c : pending_checks)
        errb.add(c);
      pending_checks = saved_pc;
      if(fatal)
        return finalize_for(std::move(errb));
    }
    if(!range_args.is_array() || as_array(range_args).empty())
      return finalize_for(code_skipt{});

    // PLib stdtypes: range(start, stop[, step])
    exprt start, stop, step;
    if(as_array(range_args).size() == 1)
    {
      start = from_integer(0, int_type);
      stop = convert_expression(*as_array(range_args).begin());
      step = from_integer(1, int_type);
    }
    else if(as_array(range_args).size() >= 2)
    {
      start = convert_expression(*as_array(range_args).begin());
      stop = convert_expression(*std::next(as_array(range_args).begin(), 1));
      if(as_array(range_args).size() >= 3)
        step = convert_expression(*std::next(as_array(range_args).begin(), 2));
      else
        step = from_integer(1, int_type);
    }
    else
    {
      start = from_integer(0, int_type);
      stop = from_integer(0, int_type);
      step = from_integer(1, int_type);
    }
    flush_header_checks();

    start = safe_typecast(start, int_type);
    stop = safe_typecast(stop, int_type);
    step = safe_typecast(step, int_type);

    irep_idt symbol_id{qualified_name};
    if(symbol_table.lookup(symbol_id) == nullptr)
    {
      symbolt new_symbol{symbol_id, int_type, "python"};
      new_symbol.base_name = var_name;
      new_symbol.location = loc;
      new_symbol.is_lvalue = true;
      new_symbol.is_state_var = true;
      symbol_table.add(new_symbol);
    }

    symbol_exprt loop_sym = symbol_table.lookup_ref(symbol_id).symbol_expr();

    code_frontend_assignt init{loop_sym, start};
    init.add_source_location() = loc;

    code_blockt body_block;
    const jsont &body = json_member(stmt, "body");
    if(body.is_array())
    {
      // PLR §8.3: see invalidate_loop_writes.
      invalidate_loop_writes(body);
      loop_depth++;
      for(const auto &s : as_array(body))
        body_block.add(convert_statement(s));
      loop_depth--;
    }

    // Condition: step > 0 ? i < stop : i > stop
    exprt cond;
    if(step.is_constant())
    {
      mp_integer sv;
      if(!to_integer(to_constant_expr(step), sv) && sv < 0)
        cond = binary_relation_exprt{loop_sym, ID_gt, stop};
      else
        cond = binary_relation_exprt{loop_sym, ID_lt, stop};
    }
    else
    {
      // Dynamic step: if(step > 0) then i < stop else i > stop
      cond = if_exprt{
        binary_relation_exprt{step, ID_gt, from_integer(0, int_type)},
        binary_relation_exprt{loop_sym, ID_lt, stop},
        binary_relation_exprt{loop_sym, ID_gt, stop}};
    }

    // C-style for (see the constant-range variant's note: continue must
    // reach the increment).
    code_fort while_stmt{
      nil_exprt{},
      cond,
      side_effect_expr_assignt{loop_sym, plus_exprt{loop_sym, step}, loc},
      std::move(body_block)};
    while_stmt.add_source_location() = loc;

    code_blockt result;
    // PLR §6.10.2: a zero step raises ValueError ("range() arg 3 must not
    // be zero") — unconditional for a literal 0, conditional on the zero
    // path for a symbolic step. Prepend so it fires before the loop.
    if(as_array(range_args).size() >= 3)
    {
      auto saved_pc = pending_checks;
      pending_checks.clear();
      emit_conditional_exception(
        equal_exprt{step, from_integer(0, int_type)}, "ValueError");
      for(auto &c : pending_checks)
        result.add(c);
      pending_checks = saved_pc;
    }
    result.add(std::move(init));
    result.add(std::move(while_stmt));
    return finalize_for(std::move(result));
  }

  // for x in iterable (list or string)
  // Desugar to: __idx = 0; while(__idx < iterable.length) {
  //   x = iterable.data[__idx]; body; __idx += 1; }
  exprt iterable = convert_expression(iter);
  if(iterable.is_nil())
    return finalize_for(code_skipt{});
  flush_header_checks();

  // PLR §3.3.1: a PROVABLY non-iterable scalar (concrete int/float/bool/complex
  // or a constant None) is not iterable -> `for x in 5` / `for x in None` raises
  // TypeError ('object is not iterable'). str/list/dict/set/tuple/range/class/
  // python_value are legitimately iterable (or MIGHT be) and are not flagged.
  {
    if(provably_non_iterable_scalar(iterable))
    {
      source_locationt tloc = loc;
      tloc.set_property_class("type-error");
      tloc.set_comment("object is not iterable");
      code_assertt te{false_exprt{}};
      te.add_source_location() = tloc;
      return finalize_for(std::move(te));
    }
  }

  // PLR: "dictionary changed size during iteration" (RuntimeError). CPython's
  // dict views (the dict itself and .items()/.keys()/.values()) check the
  // dict's size at every __next__ and raise if it changed. Model it by
  // snapshotting len(d) at loop entry and, at each __next__ point (the body
  // top, plus a synthetic terminal probe at idx == snapshot), raising
  // RuntimeError when the current size differs. This fires only on
  // size-changing mutations (add/del) -- a value-update `d[k] = v` keeps
  // len(d) -- and a user `break` exits before the next __next__, so it does not
  // fire. Only a simple lvalue receiver is watched (side-effect-free to
  // re-read).
  std::optional<exprt> cm_dict;
  {
    const jsont *recv = nullptr;
    if(is_node_type(iter, "Call"))
    {
      const jsont &fn = json_member(iter, "func");
      if(is_node_type(fn, "Attribute"))
      {
        const std::string m = json_string(json_member(fn, "attr"));
        if(m == "items" || m == "keys" || m == "values")
          recv = &json_member(fn, "value");
      }
    }
    else if(is_node_type(iter, "Name"))
      recv = &iter;
    if(
      recv != nullptr &&
      (is_node_type(*recv, "Name") || is_node_type(*recv, "Attribute")))
    {
      exprt d = convert_expression(*recv);
      if(!d.is_nil() && is_python_dict_type(d.type()))
        cm_dict = d;
    }
  }
  std::optional<symbol_exprt> cm_snapshot;
  if(cm_dict)
  {
    static unsigned cm_ctr = 0;
    irep_idt cmid{qualify_name("__cm_size_" + std::to_string(cm_ctr++))};
    if(symbol_table.lookup(cmid) == nullptr)
    {
      symbolt cs{cmid, signedbv_typet{64}, "python"};
      cs.base_name = id2string(cmid);
      cs.is_lvalue = true;
      cs.is_state_var = true;
      cs.is_static_lifetime = current_function.empty();
      symbol_table.add(cs);
    }
    cm_snapshot = symbol_table.lookup_ref(cmid).symbol_expr();
  }
  auto cm_dict_len = [&]() -> exprt {
    return member_exprt{*cm_dict, "length", signedbv_typet{64}};
  };
  auto cm_snapshot_stmt = [&]() -> codet {
    return code_frontend_assignt{*cm_snapshot, cm_dict_len()};
  };
  // Loop-body prologue (the __next__ size check + synthetic terminal stop).
  auto cm_body_prologue = [&](const exprt &idx) -> code_blockt
  {
    code_blockt b;
    code_blockt raise;
    const symbolt *exc = symbol_table.lookup("python::__exception_active");
    const symbolt *exct = symbol_table.lookup("python::__exception_type");
    if(exc != nullptr)
    {
      raise.add(code_frontend_assignt{exc->symbol_expr(), true_exprt{}});
      if(exct != nullptr)
        raise.add(code_frontend_assignt{
          exct->symbol_expr(),
          from_integer(exception_type_hash("RuntimeError"), exct->type)});
    }
    raise.add(code_breakt{});
    b.add(code_ifthenelset{
      notequal_exprt{cm_dict_len(), *cm_snapshot}, std::move(raise)});
    exprt idx64 = idx;
    if(idx64.type() != signedbv_typet{64})
      idx64 = typecast_exprt{idx64, signedbv_typet{64}};
    b.add(code_ifthenelset{
      binary_relation_exprt{idx64, ID_ge, *cm_snapshot}, code_breakt{}});
    return b;
  };
  // Loop bound for a watched dict: iterate snapshot+1 times (the +1 runs the
  // terminal __next__ size check) while staying statically bounded by the
  // model's max dict size.
  exprt failclosed_bound_len = nil_exprt{};
  auto cm_bound = [&](const exprt &idx, const exprt &orig) -> exprt
  {
    if(!cm_dict)
    {
      // Fail-closed loop bound (--python-smt-containers): a for-loop
      // over a SYMBOLIC-length container has no static trip count and
      // symex unwinds forever -- silently, before any property exists
      // (the perf-study corpus wall; same root as the comprehension
      // fallback, this is the plain-for lowering site: study
      // ex3/ex4). Conjoin a hard bound and report the truncation via
      // the scan-bound guard, so the run terminates LOUDLY. Concrete
      // lengths fold the conjunct away; the guard assert folds to
      // true for lengths within the model bound.
      if(
        python_smt_containers_flag() && orig.id() == ID_lt &&
        orig.operands().size() == 2 && !orig.operands()[1].is_constant())
      {
        // The truncation-report guard is emitted by the CALL SITE
        // into the loop's own block (right before the loop, after
        // the iterable's materialising assignments -- via pending it
        // ran BEFORE them and read an unconstrained length,
        // false-alarming on concrete literals).
        failclosed_bound_len = orig.operands()[1];
        return and_exprt{
          orig,
          binary_relation_exprt{
            idx,
            ID_lt,
            from_integer(PYTHON_MAX_LIST_LENGTH, signedbv_typet{64})}};
      }
      return orig;
    }
    return and_exprt{
      binary_relation_exprt{
        idx,
        ID_lt,
        plus_exprt{*cm_snapshot, from_integer(1, signedbv_typet{64})}},
      binary_relation_exprt{
        idx,
        ID_lt,
        from_integer(PYTHON_MAX_DICT_SIZE + 1, signedbv_typet{64})}};
  };
  // symbolic check fires false positives when the iterable
  // is a function-call result whose tag CBMC cannot prove
  // statically. Populate iter_none_check (declared up at
  // finalize_for) so the property is prepended to whatever
  // for-result the various sub-handlers below build:
  //   - Literal None: unconditional assertion failure (the
  //     for-loop body is unreachable).
  //   - python_value-typed iterable: tag-check assertion
  //     `iter.tag != NONE` so symex paths where the iterable
  //     could be None_tagged at runtime are reported.
  // Concrete typed-natural iterables (str, list, dict, set,
  // etc.) skip the check — they can't carry None at runtime
  // for a non-Optional annotation, and Optional[T] = None
  // defaults already bind to per-type empty markers
  // (length=0) which iterate to zero iterations naturally.
  if(python_check_iter_none && is_python_none_constant(iterable))
  {
    source_locationt tloc = loc;
    tloc.set_property_class("type-error");
    tloc.set_comment("'NoneType' object is not iterable");
    code_assertt te{false_exprt{}};
    te.add_source_location() = tloc;
    iter_none_check.add(std::move(te));
  }
  else if(python_check_iter_none && is_python_value_type(iterable.type()))
  {
    source_locationt tloc = loc;
    tloc.set_property_class("type-error");
    tloc.set_comment("'NoneType' object is not iterable");
    code_assertt te{
      not_exprt{python_value_is(iterable, python_type_tagt::NONE)}};
    te.add_source_location() = tloc;
    iter_none_check.add(std::move(te));
  }
  // If the iterable is a complex expression (e.g., enumerate() result),
  // store it in a temp symbol so it doesn't get simplified away.
  //
  // A *dereference* (e.g. `*d` for a by-reference / pointer dict or list
  // parameter) is a stable lvalue, not a transient rvalue: copying it into a
  // temp would make the loop iterate a snapshot whose entries are NOT aliased
  // to the object the loop body mutates through the same pointer. That breaks
  // `for k in d: d[k] = v` -- the body's key-scan over `*d` never matches the
  // copied loop key, so it spuriously appends (runaway length growth). Iterate
  // the dereference in place so iteration and mutation share one object.
  code_blockt pre_loop;
  if(
    iterable.id() != ID_symbol && iterable.id() != ID_dereference &&
    iterable.type().id() == ID_struct)
  {
    static unsigned iter_tmp_ctr = 0;
    std::string tn = "__iter_tmp_" + std::to_string(iter_tmp_ctr++);
    std::string tq = qualify_name(tn);
    irep_idt ti{tq};
    if(symbol_table.lookup(ti) == nullptr)
    {
      symbolt ts{ti, iterable.type(), "python"};
      ts.base_name = tn;
      ts.is_lvalue = true;
      ts.is_state_var = true;
      symbol_table.add(ts);
    }
    // Flush pending checks (e.g., from enumerate's wrap_value)
    for(auto &pc : pending_checks)
      pre_loop.add(std::move(pc));
    pending_checks.clear();
    pre_loop.add(code_frontend_assignt{
      symbol_table.lookup_ref(ti).symbol_expr(), iterable});
    // Keep literal provenance across the materialization: the
    // tuple-literal unroll below (and any other literal consumer)
    // resolves symbols through the literal-tracking maps.
    if(is_python_tuple_type(iterable.type()) && iterable.id() == ID_struct)
      tuple_literals[ti] = iterable;
    iterable = symbol_table.lookup_ref(ti).symbol_expr();
  }
  // PLR §4.1: \`for x in xs\` where xs is python_value (e.g. an
  // unannotated function parameter that received a list at the
  // call site). Deref __list_ptr as list[python_value] so the
  // standard list-iteration path below handles it precisely.
  if(is_python_value_type(iterable.type()))
  {
    // Shared pv-iterable lowering (iterability obligation + identity-refined
    // __iter__ dispatch); also used by the comprehension path. See the
    // helper's comment block.
    code_blockt pv_header;
    iterable = lower_pv_iterable(iterable, pv_header, loc);
    for(auto &st : pv_header.statements())
      pre_loop.add(std::move(st));
  }

  // PLR §6.3.4: 'for c in "literal":' — when iter is a known
  // constant string AND the loop target is a single Name, unroll
  // the body once per character with the loop variable bound to
  // the i'th 1-char string literal. The general string-iter path
  // emits a struct {1, address_of(temp_array[0])} per iteration,
  // which CBMC's symex layer treats as opaque per-iteration
  // scratch — the underlying char value doesn't propagate to
  // ord(c) / c.isalpha() / c == 'x' / etc. Unrolling at
  // conversion time avoids this entirely (each c is a literal
  // string the rest of the converter can fold).
  //
  // The else clause (orelse) is preserved by appending its
  // statements after the unrolled body when the loop wasn't
  // broken; we conservatively assume the loop always exits
  // normally via fall-through (the standard semantics).
  //
  // Variant: 'for i, c in enumerate("literal"):' — same idea
  // but the target is a Tuple(i, c). Both names are bound per
  // unrolled iteration: i to the integer literal and c to the
  // 1-char string literal.
  bool is_enumerate_const_str = false;
  std::string enum_const_sv;
  mp_integer enum_start_val{0};
  std::string enum_idx_name, enum_char_name;
  if(
    is_node_type(target, "Tuple") && is_node_type(iter, "Call") &&
    is_node_type(json_member(iter, "func"), "Name") &&
    json_string(json_member(json_member(iter, "func"), "id")) == "enumerate")
  {
    const jsont &elts = json_member(target, "elts");
    const jsont &eargs = json_member(iter, "args");
    if(
      elts.is_array() && as_array(elts).size() == 2 && eargs.is_array() &&
      !as_array(eargs).empty())
    {
      auto eit = as_array(elts).begin();
      auto e0 = eit;
      ++eit;
      auto e1 = eit;
      if(is_node_type(*e0, "Name") && is_node_type(*e1, "Name"))
      {
        enum_idx_name = json_string(json_member(*e0, "id"));
        enum_char_name = json_string(json_member(*e1, "id"));
        exprt s_arg = convert_expression(*as_array(eargs).begin());
        auto pit = as_array(eargs).begin();
        ++pit;
        if(pit != as_array(eargs).end())
        {
          exprt s = convert_expression(*pit);
          if(s.is_constant())
          {
            mp_integer v;
            if(!to_integer(to_constant_expr(s), v))
              enum_start_val = v;
          }
        }
        const jsont &enum_kw = json_member(iter, "keywords");
        if(enum_kw.is_array())
        {
          for(const auto &k : as_array(enum_kw))
          {
            if(json_string(json_member(k, "arg")) == "start")
            {
              exprt s = convert_expression(json_member(k, "value"));
              if(s.is_constant())
              {
                mp_integer v;
                if(!to_integer(to_constant_expr(s), v))
                  enum_start_val = v;
              }
            }
          }
        }
        if(is_python_string_type(s_arg.type()))
        {
          auto sv2 = extract_string_value(s_arg);
          if(!sv2.has_value() && s_arg.id() == ID_symbol)
          {
            auto it =
              string_constants.find(to_symbol_expr(s_arg).get_identifier());
            if(it != string_constants.end())
              sv2 = it->second;
          }
          if(sv2.has_value())
          {
            enum_const_sv = sv2.value();
            is_enumerate_const_str = true;
          }
        }
      }
    }
  }
  if(is_enumerate_const_str)
  {
    const jsont &body_e = json_member(stmt, "body");
    std::function<bool(const jsont &)> has_bc_e = [&](const jsont &n) -> bool
    {
      if(n.is_array())
      {
        for(const auto &e : as_array(n))
          if(has_bc_e(e))
            return true;
        return false;
      }
      if(!n.is_object())
        return false;
      if(is_node_type(n, "Break") || is_node_type(n, "Continue"))
        return true;
      if(
        is_node_type(n, "For") || is_node_type(n, "While") ||
        is_node_type(n, "AsyncFor"))
        return false;
      const auto &obj = static_cast<const json_objectt &>(n);
      for(const auto &kv : obj)
      {
        if(
          kv.first == "_type" || kv.first == "lineno" ||
          kv.first == "col_offset" || kv.first == "end_lineno" ||
          kv.first == "end_col_offset")
          continue;
        if(has_bc_e(kv.second))
          return true;
      }
      return false;
    };
    if(body_e.is_array() && !has_bc_e(body_e))
    {
      std::string iq = qualify_name(enum_idx_name);
      irep_idt iid{iq};
      if(symbol_table.lookup(iid) == nullptr)
      {
        symbolt is{iid, int_type, "python"};
        is.base_name = enum_idx_name;
        is.location = loc;
        is.is_lvalue = true;
        is.is_state_var = true;
        symbol_table.add(is);
      }
      std::string cq = qualify_name(enum_char_name);
      irep_idt cid{cq};
      if(symbol_table.lookup(cid) == nullptr)
      {
        symbolt cs{cid, python_string_type(), "python"};
        cs.base_name = enum_char_name;
        cs.location = loc;
        cs.is_lvalue = true;
        cs.is_state_var = true;
        symbol_table.add(cs);
      }
      else if(symbol_table.lookup_ref(cid).type != python_string_type())
      {
        symbol_table.get_writeable_ref(cid).type = python_string_type();
      }
      symbol_exprt iv = symbol_table.lookup_ref(iid).symbol_expr();
      symbol_exprt cv = symbol_table.lookup_ref(cid).symbol_expr();
      code_blockt unrolled;
      for(auto &pc : pre_loop.statements())
        unrolled.add(std::move(pc));
      pre_loop = code_blockt{};
      mp_integer i_val{0};
      for(char ch : enum_const_sv)
      {
        std::string ch_str(1, ch);
        string_constants[cid] = ch_str;
        unrolled.add(code_frontend_assignt{
          iv, from_integer(enum_start_val + i_val, int_type)});
        unrolled.add(code_frontend_assignt{cv, python_string_literal(ch_str)});
        loop_depth++;
        for(const auto &s : as_array(body_e))
          unrolled.add(convert_statement(s));
        loop_depth--;
        i_val += 1;
      }
      string_constants.erase(cid);
      const jsont &orelse_e = json_member(stmt, "orelse");
      if(orelse_e.is_array())
        for(const auto &s : as_array(orelse_e))
          unrolled.add(convert_statement(s));
      return finalize_for(std::move(unrolled));
    }
  }

  if(is_python_string_type(iterable.type()) && is_node_type(target, "Name"))
  {
    auto sv = extract_string_value(iterable);
    if(!sv.has_value() && iterable.id() == ID_symbol)
    {
      auto it =
        string_constants.find(to_symbol_expr(iterable).get_identifier());
      if(it != string_constants.end())
        sv = it->second;
    }
    if(sv.has_value())
    {
      // Skip the unroll if the body contains break/continue
      // we'd need to translate to a flag-based form. For now,
      // fall through to the generic loop in that case.
      const jsont &body_for_scan = json_member(stmt, "body");
      std::function<bool(const jsont &)> has_break_or_continue =
        [&](const jsont &n) -> bool
      {
        if(n.is_array())
        {
          for(const auto &e : as_array(n))
            if(has_break_or_continue(e))
              return true;
          return false;
        }
        if(!n.is_object())
          return false;
        if(is_node_type(n, "Break") || is_node_type(n, "Continue"))
          return true;
        // Don't recurse into nested for / while bodies — their
        // own break/continue stay scoped to the inner loop.
        if(
          is_node_type(n, "For") || is_node_type(n, "While") ||
          is_node_type(n, "AsyncFor"))
          return false;
        // Recurse into the rest.
        if(n.is_object())
        {
          const auto &obj = static_cast<const json_objectt &>(n);
          for(const auto &kv : obj)
          {
            if(
              kv.first == "_type" || kv.first == "lineno" ||
              kv.first == "col_offset" || kv.first == "end_lineno" ||
              kv.first == "end_col_offset")
              continue;
            if(has_break_or_continue(kv.second))
              return true;
          }
        }
        return false;
      };
      bool body_has_break =
        body_for_scan.is_array() && has_break_or_continue(body_for_scan);
      if(body_has_break)
        goto skip_string_unroll;
      // Materialise the loop variable so the body's reads see
      // the right type (single-char python_string).
      irep_idt var_id_unroll{qualified_name};
      if(symbol_table.lookup(var_id_unroll) == nullptr)
      {
        symbolt new_sym{var_id_unroll, python_string_type(), "python"};
        new_sym.base_name = var_name;
        new_sym.location = loc;
        new_sym.is_lvalue = true;
        new_sym.is_state_var = true;
        symbol_table.add(new_sym);
      }
      else if(
        symbol_table.lookup_ref(var_id_unroll).type != python_string_type())
      {
        symbol_table.get_writeable_ref(var_id_unroll).type =
          python_string_type();
      }
      symbol_exprt loop_var_unroll =
        symbol_table.lookup_ref(var_id_unroll).symbol_expr();
      const jsont &body = json_member(stmt, "body");
      code_blockt unrolled;
      // Pre-flush any pending checks built while computing
      // 'iterable' (e.g. wrap_value side effects).
      for(auto &pc : pre_loop.statements())
        unrolled.add(std::move(pc));
      pre_loop = code_blockt{};
      for(char ch : sv.value())
      {
        std::string ch_str(1, ch);
        // Record the binding so convert_expression can fold
        // ord(c) / c.isalpha() / c == 'x' / etc. through
        // extract_string_value.
        string_constants[var_id_unroll] = ch_str;
        unrolled.add(code_frontend_assignt{
          loop_var_unroll, python_string_literal(ch_str)});
        if(body.is_array())
        {
          loop_depth++;
          for(const auto &s : as_array(body))
            unrolled.add(convert_statement(s));
          loop_depth--;
        }
      }
      // Clear the per-loop binding so the rest of the function
      // doesn't see a stale value.
      string_constants.erase(var_id_unroll);
      // Append orelse (always-taken since we don't break in the
      // unrolled form — the string-iter unroll never breaks).
      const jsont &orelse_for_unroll = json_member(stmt, "orelse");
      if(orelse_for_unroll.is_array())
      {
        for(const auto &s : as_array(orelse_for_unroll))
          unrolled.add(convert_statement(s));
      }
      return finalize_for(std::move(unrolled));
    }
  }
skip_string_unroll:;

  // PLR 8.3: `for x in (a, b, c)` -- iteration over a CONSTANT-ARITY
  // tuple literal unrolls the body once per field (mirroring the
  // string unroll above). Previously tuple iterables fell to the
  // nondet fallback: the loop variable was a single nondet
  // assignment, so every derived count/sum false-alarmed
  // (pyhard's expected_retries derivation). Elements may be
  // heterogeneous: the loop variable is retyped per iteration.
  // Bodies with break/continue keep the fallback (loud).
  if(is_python_tuple_type(iterable.type()) && !is_node_type(target, "Tuple"))
  {
    const exprt *tuple_val = nullptr;
    if(iterable.id() == ID_struct)
      tuple_val = &iterable;
    else if(iterable.id() == ID_symbol)
    {
      auto tl = tuple_literals.find(to_symbol_expr(iterable).get_identifier());
      if(tl != tuple_literals.end())
        tuple_val = &tl->second;
    }
    std::function<bool(const jsont &)> has_bc = [&](const jsont &n) -> bool
    {
      if(n.is_array())
      {
        for(const auto &e : as_array(n))
          if(has_bc(e))
            return true;
        return false;
      }
      if(!n.is_object())
        return false;
      if(is_node_type(n, "Break") || is_node_type(n, "Continue"))
        return true;
      if(
        is_node_type(n, "For") || is_node_type(n, "While") ||
        is_node_type(n, "AsyncFor"))
        return false;
      const auto &obj = static_cast<const json_objectt &>(n);
      for(const auto &kv : obj)
      {
        if(
          kv.first == "_type" || kv.first == "lineno" ||
          kv.first == "col_offset" || kv.first == "end_lineno" ||
          kv.first == "end_col_offset")
          continue;
        if(has_bc(kv.second))
          return true;
      }
      return false;
    };
    const jsont &tb = json_member(stmt, "body");
    if(
      tuple_val != nullptr && tuple_val->id() == ID_struct &&
      !(tb.is_array() && has_bc(tb)))
    {
      irep_idt var_id_t{qualified_name};
      code_blockt unrolled;
      for(auto &pc : pre_loop.statements())
        unrolled.add(std::move(pc));
      pre_loop = code_blockt{};
      for(const exprt &field : tuple_val->operands())
      {
        if(symbol_table.lookup(var_id_t) == nullptr)
        {
          symbolt new_sym{var_id_t, field.type(), "python"};
          new_sym.base_name = var_name;
          new_sym.location = loc;
          new_sym.is_lvalue = true;
          new_sym.is_state_var = true;
          symbol_table.add(new_sym);
        }
        else
          symbol_table.get_writeable_ref(var_id_t).type = field.type();
        symbol_exprt lv = symbol_table.lookup_ref(var_id_t).symbol_expr();
        unrolled.add(code_frontend_assignt{lv, field});
        if(tb.is_array())
        {
          loop_depth++;
          for(const auto &st : as_array(tb))
            unrolled.add(convert_statement(st));
          loop_depth--;
        }
      }
      const jsont &orelse_t = json_member(stmt, "orelse");
      if(orelse_t.is_array())
        for(const auto &st : as_array(orelse_t))
          unrolled.add(convert_statement(st));
      return finalize_for(std::move(unrolled));
    }
  }

  bool is_list = is_python_list_type(iterable.type());
  bool is_string = is_python_string_type(iterable.type());
  bool is_dict = is_python_dict_type(iterable.type());

  // PLR §8.3: "for k in dict" iterates over keys (array-based)
  if(is_dict)
  {
    const auto &dict_st = to_struct_type(iterable.type());
    const auto &keys_type = to_array_type(dict_st.components()[1].type());
    typet key_type = python_dict_logical_key_type(keys_type.element_type());

    irep_idt var_id{qualified_name};
    if(symbol_table.lookup(var_id) == nullptr)
    {
      symbolt new_sym{var_id, key_type, "python"};
      new_sym.base_name = var_name;
      new_sym.location = loc;
      new_sym.is_lvalue = true;
      new_sym.is_state_var = true;
      symbol_table.add(new_sym);
    }
    symbol_exprt loop_var = symbol_table.lookup_ref(var_id).symbol_expr();

    // Desugar to: idx=0; while(idx < d.length) { k = d.keys[idx]; body; idx++ }
    static unsigned dict_iter_ctr = 0;
    std::string idx_name = "__dict_idx_" + std::to_string(dict_iter_ctr++);
    std::string idx_qname = qualify_name(idx_name);
    irep_idt idx_id{idx_qname};
    if(symbol_table.lookup(idx_id) == nullptr)
    {
      symbolt idx_sym{idx_id, signedbv_typet{64}, "python"};
      idx_sym.base_name = idx_name;
      idx_sym.is_lvalue = true;
      idx_sym.is_state_var = true;
      symbol_table.add(idx_sym);
    }
    symbol_exprt idx_var = symbol_table.lookup_ref(idx_id).symbol_expr();
    member_exprt length{iterable, "length", signedbv_typet{64}};
    member_exprt keys{iterable, "keys", keys_type};

    code_blockt result;
    result.add(
      code_frontend_assignt{idx_var, from_integer(0, signedbv_typet{64})});
    if(cm_dict)
      result.add(cm_snapshot_stmt());

    code_blockt body_block;
    if(cm_dict)
    {
      code_blockt cm_pro = cm_body_prologue(idx_var);
      for(auto &st : cm_pro.statements())
        body_block.add(std::move(st));
    }
    exprt key_val = python_dict_unbox_key(index_exprt{keys, idx_var});
    if(key_val.type() != loop_var.type())
      key_val = safe_typecast(key_val, loop_var.type());
    body_block.add(code_frontend_assignt{loop_var, key_val});

    // PLR §8.3: `for u, v in d` iterates the KEYS; a Tuple target
    // unpacks each (tuple) key into the element names. Without this the
    // element names stayed unconverted and every body statement using
    // them was silently DROPPED -- `for u, v in d: total += u + v`
    // proved total == 0 (a vacuity false proof; ESBMC
    // dict_tuple_key_for_iter_fail, CPython-confirmed). Mirrors the
    // list-branch unpack; the key tuple is an anonymous inline struct,
    // so fields are _0/_1/... components.
    if(is_node_type(target, "Tuple"))
    {
      const jsont &t_elts = json_member(target, "elts");
      if(t_elts.is_array())
      {
        std::size_t tidx = 0;
        for(const auto &elt : as_array(t_elts))
        {
          if(is_node_type(elt, "Name"))
          {
            const std::string en = json_string(json_member(elt, "id"));
            const irep_idt eid{qualify_name(en)};
            const std::string field = "_" + std::to_string(tidx);
            typet et = python_int_type();
            if(
              loop_var.type().id() == ID_struct &&
              to_struct_type(loop_var.type()).has_component(field))
              et = to_struct_type(loop_var.type()).get_component(field).type();
            if(symbol_table.lookup(eid) == nullptr)
            {
              symbolt es{eid, et, "python"};
              es.base_name = en;
              es.is_lvalue = true;
              es.is_state_var = true;
              symbol_table.add(es);
            }
            symbol_exprt ev = symbol_table.lookup_ref(eid).symbol_expr();
            if(
              loop_var.type().id() == ID_struct &&
              to_struct_type(loop_var.type()).has_component(field))
            {
              exprt fv = member_exprt{loop_var, field, et};
              if(fv.type() != ev.type())
                fv = safe_typecast(fv, ev.type());
              body_block.add(code_frontend_assignt{ev, fv});
            }
            else
            {
              // Key shape unknown: a sound nondet binding (never drop).
              body_block.add(code_frontend_assignt{
                ev, side_effect_expr_nondett{ev.type(), loc}});
            }
          }
          tidx++;
        }
      }
    }

    const jsont &body_stmts = json_member(stmt, "body");
    if(body_stmts.is_array())
    {
      invalidate_loop_writes(body_stmts);
      loop_depth++;
      for(const auto &s : as_array(body_stmts))
        body_block.add(convert_statement(s));
      loop_depth--;
    }

    // Bound the iteration by the model's max dict size in addition to the
    // (possibly symbolic) length, so the loop is *statically* bounded even
    // when `d` is a parameter / has an unconstrained symbolic length -- a dict
    // never holds more than PYTHON_MAX_DICT_SIZE entries by construction.
    // Without the constant bound a `for k in d` over a parameter dict is
    // effectively unbounded and spuriously trips the unwinding assertion.
    // For a watched dict, cm_bound replaces this with the snapshot+1 bound.
    exprt default_bound = and_exprt{
      binary_relation_exprt{idx_var, ID_lt, length},
      binary_relation_exprt{
        idx_var,
        ID_lt,
        from_integer(PYTHON_MAX_DICT_SIZE, signedbv_typet{64})}};
    // C-style for (see the constant-range variant's note: continue must
    // reach the increment).
    code_fort while_stmt{
      nil_exprt{},
      cm_bound(idx_var, default_bound),
      side_effect_expr_assignt{
        idx_var, plus_exprt{idx_var, from_integer(1, signedbv_typet{64})}, loc},
      std::move(body_block)};
    result.add(std::move(while_stmt));
    return finalize_for(std::move(result));
  }

  if(!is_list && !is_string)
  {
    // PLR §3.3.1: custom iterator protocol — class with
    // __iter__ and __next__. Dispatch to them in a
    // bounded loop (PYTHON_MAX_LIST_LENGTH iterations).
    // StopIteration inside __next__ sets
    // __exception_active → we break out of the loop.
    std::string tag;
    if(iterable.type().id() == ID_struct)
      tag = id2string(to_struct_type(iterable.type()).get_tag());
    else if(iterable.type().id() == ID_struct_tag)
      tag = id2string(to_struct_tag_type(iterable.type()).get_identifier());
    if(tag.substr(0, 13) == "python_class_")
    {
      std::string bare = tag.substr(13);
      const symbolt *iter_sym = nullptr;
      const symbolt *next_sym = nullptr;
      for(const std::string &prefix :
          {std::string{"python::"} + tag + "::__iter__",
           std::string{"python::"} + bare + "::__iter__"})
      {
        const symbolt *s = symbol_table.lookup(irep_idt{prefix});
        if(s != nullptr)
        {
          iter_sym = s;
          break;
        }
      }
      for(const std::string &prefix :
          {std::string{"python::"} + tag + "::__next__",
           std::string{"python::"} + bare + "::__next__"})
      {
        const symbolt *s = symbol_table.lookup(irep_idt{prefix});
        if(s != nullptr)
        {
          next_sym = s;
          break;
        }
      }

      if(next_sym != nullptr)
      {
        // Determine return type of __next__ for the loop var.
        typet next_ret = python_int_type();
        if(next_sym->type.id() == ID_code)
          next_ret = to_code_type(next_sym->type).return_type();

        // Create loop variable.
        std::string vname = json_string(json_member(target, "id"));
        std::string vq = qualify_name(vname);
        irep_idt vid{vq};
        if(symbol_table.lookup(vid) == nullptr)
        {
          symbolt ns{vid, next_ret, "python"};
          ns.base_name = vname;
          ns.is_lvalue = true;
          ns.is_state_var = true;
          ns.is_static_lifetime = current_function.empty();
          symbol_table.add(ns);
        }
        symbol_exprt loop_var = symbol_table.lookup_ref(vid).symbol_expr();

        code_blockt result;
        // Optionally call __iter__ once for its side effects.
        if(iter_sym != nullptr)
        {
          side_effect_expr_function_callt iter_call{
            iter_sym->symbol_expr(),
            {address_of_exprt{iterable}},
            iter_sym->type.id() == ID_code
              ? to_code_type(iter_sym->type).return_type()
              : python_int_type(),
            loc};
          result.add(code_expressiont{std::move(iter_call)});
        }

        code_blockt body_block;
        const jsont &body_j = json_member(stmt, "body");
        if(body_j.is_array())
        {
          invalidate_loop_writes(body_j);
          loop_depth++;
          for(const auto &s : as_array(body_j))
            body_block.add(convert_statement(s));
          loop_depth--;
        }

        // Loop: for N iterations, call __next__, check
        // exception flag, assign to loop var, run body.
        code_blockt loop_body;
        side_effect_expr_function_callt nc{
          next_sym->symbol_expr(), {address_of_exprt{iterable}}, next_ret, loc};
        loop_body.add(code_frontend_assignt{loop_var, std::move(nc)});

        // Check __exception_active and break if set.
        irep_idt exc_id{"python::__exception_active"};
        const symbolt *exc_sym = symbol_table.lookup(exc_id);
        if(exc_sym != nullptr)
        {
          loop_body.add(
            code_ifthenelset{exc_sym->symbol_expr(), code_breakt{}});
        }

        for(const auto &s : body_block.statements())
          loop_body.add(s);

        // Bounded loop: use PYTHON_MAX_LIST_LENGTH iterations
        // via an explicit counter.
        std::string ctr_name =
          "__iter_ctr_" + std::to_string(symbol_table.symbols.size());
        std::string ctr_q = qualify_name(ctr_name);
        irep_idt ctr_id{ctr_q};
        if(symbol_table.lookup(ctr_id) == nullptr)
        {
          symbolt cs{ctr_id, int_type, "python"};
          cs.base_name = ctr_name;
          cs.is_lvalue = true;
          cs.is_state_var = true;
          cs.is_static_lifetime = current_function.empty();
          symbol_table.add(cs);
        }
        symbol_exprt ctr_var = symbol_table.lookup_ref(ctr_id).symbol_expr();
        result.add(code_frontend_assignt{ctr_var, from_integer(0, int_type)});
        // Prepend counter increment to loop body.
        code_blockt loop_body_with_ctr;
        loop_body_with_ctr.add(code_frontend_assignt{
          ctr_var, plus_exprt{ctr_var, from_integer(1, int_type)}});
        for(const auto &s : loop_body.statements())
          loop_body_with_ctr.add(s);
        code_whilet while_stmt{
          binary_relation_exprt{
            ctr_var, ID_lt, from_integer(PYTHON_MAX_LIST_LENGTH, int_type)},
          std::move(loop_body_with_ctr)};
        while_stmt.add_source_location() = loc;
        result.add(std::move(while_stmt));

        // Clear the StopIteration exception after the loop.
        if(exc_sym != nullptr)
        {
          result.add(
            code_frontend_assignt{exc_sym->symbol_expr(), false_exprt{}});
        }

        return finalize_for(std::move(result));
      }
      // PLR §3.3.1: iterating an instance requires __iter__ (or the old
      // sequence protocol via __getitem__). A class whose MRO defines neither
      // is not iterable -> `for x in C()` raises TypeError. Reached only when
      // there is no self-iterator __next__ above; gated so a __getitem__-only
      // class (sequence protocol) or an __iter__ returning a separate iterator
      // object is NOT flagged (those fall through, modelled imprecisely).
      const bool iter_invalid =
        iter_protocol_of(bare) == iter_protocol_kindt::INVALID;
      if(
        (!class_mro_defines(bare, "__iter__") &&
         !class_mro_defines(bare, "__getitem__")) ||
        // PLR §3.3.1 iterator protocol: an __iter__ provably returning a
        // non-iterator raises "TypeError: iter() returned non-iterator"
        // and SHADOWS the legacy __getitem__ protocol (CPython-verified).
        // Without this the loop silently iterated the returned list value
        // (a false proof).
        iter_invalid)
      {
        source_locationt tloc = loc;
        tloc.set_property_class("type-error");
        tloc.set_comment(
          iter_invalid ? "iter() returned non-iterator (TypeError)"
                       : "object is not iterable");
        // PLR §8.4: catchable when an enclosing handler covers TypeError.
        if(exception_is_caught("TypeError"))
        {
          emit_conditional_exception(true_exprt{}, "TypeError");
          flush_header_checks();
          return finalize_for(code_skipt{});
        }
        code_assertt te{false_exprt{}};
        te.add_source_location() = tloc;
        return finalize_for(std::move(te));
      }
    }

    // PLR §6.2.4 / §6.4.6: 'for x in s' over a python_set
    // (bitmap representation). Iterate over each of the 64
    // bitmap bits; when set, bind x to (offset + bit_index)
    // and run the body.
    if(is_python_set_type(iterable.type()))
    {
      code_blockt result;
      for(auto &pc : pending_checks)
        result.add(std::move(pc));
      pending_checks.clear();
      // The loop variable x is python_int (sets currently only
      // hold ints precisely). Materialise its symbol.
      irep_idt symbol_id{qualified_name};
      if(symbol_table.lookup(symbol_id) == nullptr)
      {
        symbolt new_symbol{symbol_id, python_int_type(), "python"};
        new_symbol.base_name = var_name;
        new_symbol.location = loc;
        new_symbol.is_lvalue = true;
        new_symbol.is_state_var = true;
        symbol_table.add(new_symbol);
      }
      symbol_exprt loop_sym = symbol_table.lookup_ref(symbol_id).symbol_expr();
      member_exprt bm{iterable, "bitmap", unsignedbv_typet{64}};
      member_exprt off{iterable, "offset", signedbv_typet{64}};
      const jsont &body = json_member(stmt, "body");
      for(int k = 0; k < 64; ++k)
      {
        // bit k set?
        exprt bit_set = notequal_exprt{
          bitand_exprt{
            lshr_exprt{bm, from_integer(k, unsignedbv_typet{64})},
            from_integer(1, unsignedbv_typet{64})},
          from_integer(0, unsignedbv_typet{64})};
        // x = k + offset
        exprt val = plus_exprt{from_integer(k, signedbv_typet{64}), off};
        code_blockt iter_body;
        iter_body.add(code_frontend_assignt{loop_sym, val});
        if(body.is_array())
        {
          for(const auto &s : as_array(body))
          {
            codet c = convert_statement(s);
            for(auto &pc : pending_checks)
              iter_body.add(std::move(pc));
            pending_checks.clear();
            iter_body.add(std::move(c));
          }
        }
        code_ifthenelset cond_iter{bit_set, std::move(iter_body)};
        cond_iter.add_source_location() = loc;
        result.add(std::move(cond_iter));
      }
      return finalize_for(std::move(result));
    }

    log_overapprox(
      "for-in iteration: unsupported iterable type, body executed once "
      "with loop variable nondet (so contained call sites are still "
      "type-checked)");
    {
      code_blockt pre_loop_unsup;
      for(auto &pc : pending_checks)
        pre_loop_unsup.add(std::move(pc));
      pending_checks.clear();
      // Make the loop variable a nondet value of an Any-shaped
      // type so the body still type-checks.
      irep_idt symbol_id{qualified_name};
      if(symbol_table.lookup(symbol_id) == nullptr)
      {
        symbolt new_symbol{symbol_id, python_value_type(), "python"};
        new_symbol.base_name = var_name;
        new_symbol.location = loc;
        new_symbol.is_lvalue = true;
        new_symbol.is_state_var = true;
        symbol_table.add(new_symbol);
      }
      symbol_exprt loop_sym = symbol_table.lookup_ref(symbol_id).symbol_expr();
      // Use a one-shot while loop so any 'continue' or 'break'
      // inside the body has a valid target. We wrap the body
      // in: while(__once && __skip) { body; __once = false; }
      // The condition is nondet only on the first iteration;
      // subsequent iterations exit via the __once flag. This
      // ensures the body is processed exactly once (when the
      // verifier non-deterministically chooses 'loop ran') or
      // not at all (when 'loop did not run').
      static unsigned for_skip_ctr = 0;
      std::string skip_name = "__for_skip_" + std::to_string(for_skip_ctr);
      std::string once_name = "__for_once_" + std::to_string(for_skip_ctr++);
      irep_idt skip_id{qualify_name(skip_name)};
      irep_idt once_id{qualify_name(once_name)};
      if(symbol_table.lookup(skip_id) == nullptr)
      {
        symbolt ss{skip_id, bool_typet{}, "python"};
        ss.base_name = skip_name;
        ss.is_lvalue = true;
        ss.is_state_var = true;
        ss.is_static_lifetime = current_function.empty();
        symbol_table.add(ss);
      }
      if(symbol_table.lookup(once_id) == nullptr)
      {
        symbolt os{once_id, bool_typet{}, "python"};
        os.base_name = once_name;
        os.is_lvalue = true;
        os.is_state_var = true;
        os.is_static_lifetime = current_function.empty();
        symbol_table.add(os);
      }
      symbol_exprt skip_sym = symbol_table.lookup_ref(skip_id).symbol_expr();
      symbol_exprt once_sym = symbol_table.lookup_ref(once_id).symbol_expr();
      code_blockt body_once;
      body_once.add(code_frontend_assignt{
        loop_sym, side_effect_expr_nondett{loop_sym.type(), loc}});
      const jsont &body = json_member(stmt, "body");
      if(body.is_array())
      {
        invalidate_loop_writes(body);
        loop_depth++;
        for(const auto &s : as_array(body))
          body_once.add(convert_statement(s));
        loop_depth--;
      }
      // After body runs once, set once=false so the next
      // iteration exits.
      body_once.add(code_frontend_assignt{once_sym, false_exprt{}});
      code_blockt result;
      for(auto &s : pre_loop_unsup.statements())
        result.add(s);
      result.add(code_frontend_assignt{
        skip_sym, side_effect_expr_nondett{bool_typet{}, loc}});
      result.add(code_frontend_assignt{once_sym, true_exprt{}});
      code_whilet wh{and_exprt{once_sym, skip_sym}, std::move(body_once)};
      result.add(std::move(wh));
      return finalize_for(std::move(result));
    }
  }

  // Determine element type
  typet elem_type;
  if(is_list)
  {
    const auto &data_type =
      to_array_type(to_struct_type(iterable.type()).components()[1].type());
    elem_type = data_type.element_type();
    // Native string-id handles: the loop VARIABLE holds the string
    // DENOTATION (the bind unwraps strtab(h)), so its type is smt_string
    // even though the slot type is the bv64 handle.
    if(
      python_smt_string_native_flag() &&
      is_python_string_handle_type(elem_type))
      elem_type = string_typet{};
  }
  else
    elem_type =
      python_string_type(); // string iteration yields single-char strings

  // Create loop variable
  irep_idt var_id{qualified_name};
  if(symbol_table.lookup(var_id) == nullptr)
  {
    symbolt new_sym{var_id, elem_type, "python"};
    new_sym.base_name = var_name;
    new_sym.location = loc;
    new_sym.is_lvalue = true;
    new_sym.is_state_var = true;
    symbol_table.add(new_sym);
  }
  else if(symbol_table.lookup_ref(var_id).type != elem_type)
  {
    // Update type to match iterable element type (e.g., tuple from enumerate)
    symbol_table.get_writeable_ref(var_id).type = elem_type;
  }
  symbol_exprt loop_var = symbol_table.lookup_ref(var_id).symbol_expr();

  // Create index variable
  std::string idx_name = "__for_idx_" + var_name;
  std::string idx_qname = qualify_name(idx_name);
  irep_idt idx_id{idx_qname};
  if(symbol_table.lookup(idx_id) == nullptr)
  {
    symbolt idx_sym{idx_id, int_type, "python"};
    idx_sym.base_name = idx_name;
    idx_sym.location = loc;
    idx_sym.is_lvalue = true;
    idx_sym.is_state_var = true;
    symbol_table.add(idx_sym);
  }
  symbol_exprt idx_var = symbol_table.lookup_ref(idx_id).symbol_expr();

  const bool native_str_iter =
    use_smt_string_native && is_string && iterable.type().id() == ID_string;
  // The length MEMBER is the representation's signedbv[64]; under
  // --python-unbounded-ints int_type is integer_typet and reading the
  // member AS integer built an ill-typed member_exprt (crashed
  // simplify_member via field sensitivity). Read with the member's
  // real type, then coerce to the loop-index domain.
  exprt length =
    native_str_iter
      ? safe_typecast(native_or_member_string_length(iterable), int_type)
      : exprt(member_exprt{iterable, "length", signedbv_typet{64}});
  if(length.type() != int_type)
    length = safe_typecast(length, int_type);
  typet data_field_type;
  if(is_python_string_type(iterable.type()))
    data_field_type = pointer_typet(unsignedbv_typet{8}, 64);
  else if(iterable.type().id() == ID_struct)
    data_field_type = to_struct_type(iterable.type()).components()[1].type();
  else
    data_field_type = signedbv_typet{64}; // fallback
  // Native SMT-String iteration has no `data` array; the element is produced
  // by str.substr below. Avoid a member_exprt on an smt_string operand.
  exprt data = native_str_iter
                 ? exprt{nil_exprt{}}
                 : exprt{member_exprt{iterable, "data", data_field_type}};

  code_blockt result;

  // __idx = 0  (or the generator's cursor — see below)
  // PLR §6.2.9: iterating a generator consumes from its CURRENT position. If
  // the iterable is a generator with a live cursor, start the loop at the
  // cursor (so `next(g); for x in g` resumes) and exhaust it afterwards.
  irep_idt for_gen_cursor;
  if(is_node_type(iter, "Name"))
  {
    auto git = generator_cursors.find(
      qualify_name(json_string(json_member(iter, "id"))));
    if(
      git != generator_cursors.end() &&
      symbol_table.lookup(git->second) != nullptr)
      for_gen_cursor = git->second;
  }
  exprt for_start_idx = from_integer(0, int_type);
  if(!for_gen_cursor.empty())
    for_start_idx = safe_typecast(
      symbol_table.lookup_ref(for_gen_cursor).symbol_expr(), int_type);
  result.add(code_frontend_assignt{idx_var, for_start_idx});
  if(cm_dict)
    result.add(cm_snapshot_stmt());

  // while(__idx < iterable.length)
  code_blockt body_block;
  // Concurrent-modification __next__ check at the top of each iteration.
  if(cm_dict)
  {
    code_blockt cm_pro = cm_body_prologue(idx_var);
    for(auto &st : cm_pro.statements())
      body_block.add(std::move(st));
  }

  // x = iterable.data[__idx] (typecast if needed). For native string
  // iteration this placeholder is overwritten by the str.substr branch below.
  exprt elem_val =
    native_str_iter
      ? exprt{side_effect_expr_nondett{string_typet{}, get_location(stmt)}}
      : (is_string ? exprt(dereference_exprt{plus_exprt{data, idx_var}})
                   : exprt(index_exprt{data, idx_var}));
  // Native string-id handles: iterating a list[str] binds strtab(h), so
  // the loop variable is a STRING (uses of it never see the handle).
  if(
    python_smt_string_native_flag() &&
    is_python_string_handle_type(elem_val.type()))
    elem_val = string_handle_to_string(elem_val);

  // Handle tuple unpacking: for a, b in list_of_tuples
  if(is_node_type(target, "Tuple") && is_list)
  {
    const jsont &elts = json_member(target, "elts");
    if(elts.is_array())
    {
      std::size_t tidx = 0;
      for(const auto &elt : as_array(elts))
      {
        if(is_node_type(elt, "Name"))
        {
          std::string elt_name = json_string(json_member(elt, "id"));
          std::string elt_qname = qualify_name(elt_name);
          irep_idt elt_id{elt_qname};

          // Compute the loop variable's type from the tuple
          // field type so we don't lose precision when the
          // declared dict has typed keys/values (e.g. for
          // `dict[str, int]` items the unpacked k must be a
          // python_string, not the default python_int).
          typet elt_type = python_int_type();
          std::string field = "_" + std::to_string(tidx);
          if(
            elem_val.type().id() == ID_struct &&
            to_struct_type(elem_val.type()).has_component(field))
          {
            elt_type =
              to_struct_type(elem_val.type()).get_component(field).type();
          }
          if(symbol_table.lookup(elt_id) == nullptr)
          {
            symbolt elt_sym{elt_id, elt_type, "python"};
            elt_sym.base_name = elt_name;
            elt_sym.is_lvalue = true;
            elt_sym.is_state_var = true;
            symbol_table.add(elt_sym);
          }
          else
          {
            // Refresh existing symbol type when the tuple
            // shape has narrowed since the previous pass.
            symbol_table.get_writeable_ref(elt_id).type = elt_type;
          }
          symbol_exprt elt_var = symbol_table.lookup_ref(elt_id).symbol_expr();
          // Access tuple field: elem._0, elem._1, etc.
          if(
            elem_val.type().id() == ID_struct &&
            to_struct_type(elem_val.type()).has_component(field))
          {
            exprt field_val = member_exprt{
              elem_val,
              field,
              to_struct_type(elem_val.type()).get_component(field).type()};
            if(field_val.type() != elt_var.type())
              field_val = safe_typecast(field_val, elt_var.type());
            body_block.add(code_frontend_assignt{elt_var, field_val});
          }
          else
          {
            // Fallback: use nondet
            body_block.add(code_frontend_assignt{
              elt_var, side_effect_expr_nondett{elt_var.type(), loc}});
          }
        }
        tidx++;
      }
    }
  }
  else
  {
    // For string iteration, wrap the char byte in a single-char string struct
    if(is_string && is_python_string_type(loop_var.type()))
    {
      if(use_smt_string_native)
      {
        // Native SMT-String back-end (Plan A): the loop element is
        // iterable[idx] = str.substr(iterable, idx, 1), a native SMT String.
        elem_val =
          string_substr(iterable, idx_var, from_integer(1, signedbv_typet{64}));
      }
      else
      {
        // symbolic-content sources, byte-array wrap for known-byte
        // sources. Mirrors the convert_subscript two-strategy split:
        //   (a) byte-level wrap — exact for constant strings; bytes
        //       at the result struct's data pointer reflect the actual
        //       source bytes, so byte-level operations like .isalpha()
        //       work.
        //   (b) cprover_string_substring(s, idx, idx+1) — the refined-
        //       string solver registers the result as a substring of
        //       `s` so byte-level constraints from
        //       `assume(s == "abc")` propagate to the loop body's
        //       reads of c.
        bool source_has_known_bytes = false;
        {
          auto sv = extract_string_value(iterable);
          if(!sv.has_value() && iterable.id() == ID_symbol)
          {
            auto it =
              string_constants.find(to_symbol_expr(iterable).get_identifier());
            if(it != string_constants.end())
              sv = it->second;
          }
          if(sv.has_value())
            source_has_known_bytes = true;
        }
        if(source_has_known_bytes)
        {
          // Strategy (a): build pointer-based single-char string.
          exprt::operandst chars;
          chars.push_back(elem_val);
          array_typet at(
            unsignedbv_typet{8}, from_integer(1, signedbv_typet{64}));
          array_exprt arr(std::move(chars), at);
          exprt ptr = address_of_exprt(index_exprt(
            arr, from_integer(0, signedbv_typet{64}), unsignedbv_typet{8}));
          exprt len_one = from_integer(1, signedbv_typet{64});
          elem_val = struct_exprt{{len_one, ptr}, python_string_type()};
        }
        else
        {
          // Strategy (b): cprover_string_substring intrinsic. We're
          // inside a while-loop body (loop_depth > 0 once the outer
          // for-loop translation steps in below), so emit_string_function
          // havocs the result symbols per iteration.
          exprt src_struct =
            (iterable.id() == ID_struct && iterable.operands().size() == 2)
              ? iterable
              : exprt(struct_exprt{
                  {member_exprt{iterable, "length", signedbv_typet{64}},
                   member_exprt{
                     iterable, "data", pointer_typet(unsignedbv_typet{8}, 64)}},
                  iterable.type()});
          exprt start64 = idx_var;
          if(start64.type() != signedbv_typet{64})
            start64 = safe_typecast(start64, signedbv_typet{64});
          exprt end64 =
            plus_exprt{start64, from_integer(1, signedbv_typet{64})};
          // emit_string_function emits its pending checks into the
          // outer pending_checks list. We need them inside the loop
          // body so they run each iteration. Capture-and-flush.
          std::size_t pre_size = pending_checks.size();
          elem_val = emit_string_function(
            ID_cprover_string_substring_func,
            {src_struct, start64, end64},
            symbol_table,
            pending_checks,
            /*in_loop=*/true,
            // global (documented): see emit_string_function scope invariant.
            std::string{});
          // Move the new pending checks into body_block so they
          // execute inside the loop, before the loop_var assign.
          for(std::size_t k = pre_size; k < pending_checks.size(); ++k)
            body_block.add(std::move(pending_checks[k]));
          pending_checks.erase(
            pending_checks.begin() + pre_size, pending_checks.end());
        }
      }
    }
    else if(elem_val.type() != loop_var.type())
      elem_val = safe_typecast(elem_val, loop_var.type());
    body_block.add(code_frontend_assignt{loop_var, elem_val});
  } // end else (non-tuple target)

  // user body
  const jsont &body = json_member(stmt, "body");
  if(body.is_array())
  {
    loop_depth++;
    for(const auto &s : as_array(body))
      body_block.add(convert_statement(s));
    loop_depth--;
  }

  // C-style for with `__idx += 1` as the ITER expression (see the
  // constant-range variant's note: continue must reach the increment).
  exprt for_cond =
    cm_bound(idx_var, binary_relation_exprt{idx_var, ID_lt, length});
  // Fail-closed truncation report (see cm_bound): emitted HERE, after
  // the iterable's materialising assignments already in `result`, so
  // the guard reads the actual length (via pending it ran before them
  // and false-alarmed on concrete list literals).
  if(failclosed_bound_len.is_not_nil())
  {
    std::vector<codet> guard_stmts;
    emit_scan_bound_guard(
      failclosed_bound_len,
      loc,
      static_cast<long>(PYTHON_MAX_LIST_LENGTH),
      &guard_stmts);
    for(auto &g : guard_stmts)
      result.add(std::move(g));
  }
  code_fort while_stmt{
    nil_exprt{},
    std::move(for_cond),
    side_effect_expr_assignt{
      idx_var, plus_exprt{idx_var, from_integer(1, int_type)}, loc},
    std::move(body_block)};
  while_stmt.add_source_location() = loc;
  result.add(std::move(while_stmt));

  // PLR §6.2.9: a `for` loop over a generator exhausts it — set the cursor to
  // the length so a subsequent next()/for re-observes exhaustion (no re-yield).
  if(!for_gen_cursor.empty())
  {
    const symbolt &cs = symbol_table.lookup_ref(for_gen_cursor);
    result.add(
      code_frontend_assignt{cs.symbol_expr(), safe_typecast(length, cs.type)});
  }

  // Prepend pre-loop setup (temp for complex iterables)
  if(!pre_loop.statements().empty())
  {
    for(auto &s : result.statements())
      pre_loop.add(std::move(s));
    return finalize_for(std::move(pre_loop));
  }
  return finalize_for(std::move(result));
}

// PLR §7.6: The return statement
// "return may only occur syntactically nested in a function definition."
codet python_convertert::convert_return(const jsont &stmt)
{
  // PLR §6.2.9: return in generator → return __gen_result
  if(!current_function.empty() && generator_functions.count(current_function))
  {
    std::string grn = "__gen_result_" + current_function;
    std::string grq = qualify_name(grn);
    const symbolt *grs = symbol_table.lookup(irep_idt{grq});
    if(grs != nullptr)
      return code_frontend_returnt{grs->symbol_expr()};
  }

  const jsont &value = json_member(stmt, "value");

  if(value.is_null())
  {
    // Bare \`return\` — PLR §7.6: returns None.
    if(!current_function.empty())
    {
      irep_idt func_id{"python::" + current_function};
      const symbolt *func_sym = symbol_table.lookup(func_id);
      if(
        func_sym != nullptr && func_sym->type.id() == ID_code &&
        to_code_type(func_sym->type).return_type().id() != ID_empty)
      {
        const typet &rt = to_code_type(func_sym->type).return_type();
        // Encode None per the function's return type:
        //   * python_value -> tagged-union NONE
        //   * int / float / etc. -> int None-sentinel typecast
        //   * empty (void) -> bare return
        if(is_python_value_type(rt))
          return code_frontend_returnt{python_none_value()};
        const mp_integer none_val = python_none_sentinel_int();
        return code_frontend_returnt{
          safe_typecast(from_integer(none_val, python_int_type()), rt)};
      }
    }
    return code_frontend_returnt{};
  }

  // Check if returning a constructor call: return Foo(args)
  if(
    is_node_type(value, "Call") &&
    is_node_type(json_member(value, "func"), "Name"))
  {
    std::string call_name =
      json_string(json_member(json_member(value, "func"), "id"));
    if(class_types.count(call_name))
    {
      // Create a temporary, call __init__, return the temporary
      const struct_typet &cls_type = class_types[call_name];
      source_locationt loc = get_location(stmt);

      std::string tmp_name = "__ret_tmp_" + call_name;
      std::string tmp_qname = qualify_name(tmp_name);
      irep_idt tmp_id{tmp_qname};

      if(symbol_table.lookup(tmp_id) == nullptr)
      {
        symbolt tmp_sym{tmp_id, cls_type, "python"};
        tmp_sym.base_name = tmp_name;
        tmp_sym.is_lvalue = true;
        tmp_sym.is_state_var = true;
        symbol_table.add(tmp_sym);
      }
      else
      {
        // Idempotent re-call: refresh the temp's type from
        // the (now fully populated) class_types entry.
        symbol_table.get_writeable_ref(tmp_id).type = cls_type;
      }

      const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);
      code_blockt block;

      // Construct: __init__ call, or @dataclass field binding.
      for(auto &s : build_class_construction(
            call_name, tmp_sym.symbol_expr(), value, loc))
        block.add(std::move(s));

      // Typecast to function's return type if needed
      exprt ret_expr = tmp_sym.symbol_expr();
      irep_idt func_id{"python::" + current_function};
      const symbolt *func_sym = symbol_table.lookup(func_id);
      if(func_sym != nullptr && func_sym->type.id() == ID_code)
      {
        typet ret_type = to_code_type(func_sym->type).return_type();
        ret_expr = coerce_return_value(ret_expr, ret_type);
      }
      block.add(code_frontend_returnt{ret_expr});
      return std::move(block);
    }
  }

  exprt ret_val = convert_expression(value);

  // PLR §3.1: if the return value is a Name that resolves to a
  // pointer-typed list/dict symbol (a parameter or an alias-promoted
  // local), return the POINTER rather than the dereferenced struct.
  // This preserves object identity through the call boundary: the
  // caller's `b = f(a)` will see a pointer-typed RHS and bind `b`
  // as an alias of the underlying storage, so mutations through `b`
  // propagate to `a`. Without this, convert_name's auto-deref
  // produces `*x` (a struct copy), and the caller gets an
  // independent value — a soundness gap.
  //
  // We also promote the function's declared return type to the
  // pointer type so the call site's side_effect_expr_function_callt
  // has the correct type and the assignment path can detect the
  // pointer-typed result and bind the LHS as an alias.
  if(is_node_type(value, "Name"))
  {
    std::string ret_name = json_string(json_member(value, "id"));
    std::string ret_qname = qualify_name(ret_name);
    auto ver_it = variable_versions.find(ret_qname);
    irep_idt lookup_id = (ver_it != variable_versions.end())
                           ? ver_it->second
                           : irep_idt{ret_qname};
    const symbolt *ret_sym = symbol_table.lookup(lookup_id);
    if(
      ret_sym != nullptr && ret_sym->type.id() == ID_pointer &&
      (is_python_list_type(to_pointer_type(ret_sym->type).base_type()) ||
       is_python_dict_type(to_pointer_type(ret_sym->type).base_type()) ||
       // reference-semantics-for-instances (Phase 1): a function returning a
       // by-reference instance (a concrete-class param / self / instance alias)
       // returns the POINTER, so the caller's `u = f(v)` aliases the same
       // object. The call site already passes address_of(v) directly for
       // instances (no temp copy, unlike containers), and `return t` otherwise
       // derefs to `*t` (a struct copy) -- a soundness gap (return-flow false
       // proof). Only fires for a pointer-typed Name (param/self/alias), so a
       // fresh `return Foo()` stays by-value (distinct identity, no over-alias).
       is_instance_pointer(ret_sym->type)))
    {
      // Promote the function's return type to pointer so the call
      // site sees a pointer-typed result.
      if(!current_function.empty())
      {
        irep_idt func_id{"python::" + current_function};
        symbolt *func_sym_w = symbol_table.get_writeable(func_id);
        if(func_sym_w != nullptr && func_sym_w->type.id() == ID_code)
        {
          code_typet &ft = to_code_type(func_sym_w->type);
          if(ft.return_type() != ret_sym->type)
            ft.return_type() = ret_sym->type;
        }
      }
      return code_frontend_returnt{ret_sym->symbol_expr()};
    }
  }

  // PLR: record dict-literal return keys for caller-side
  // dict_literals propagation. If a function returns a dict
  // literal with constant keys, callers that assign the
  // result to a local variable can trust those keys exist.
  //
  // Two patterns are captured:
  //   (A) 'return {"K": V, ...}' — direct struct literal.
  //   (B) 'return d' where d is a symbol whose dict_literals
  //       entry has known keys (built up via 'd[K] = ...').
  auto record_function_returned_keys = [&](const exprt &src)
  {
    if(current_function.empty())
      return;
    if(
      src.id() != ID_struct || src.operands().size() < 2 ||
      !src.operands()[0].is_constant())
      return;
    mp_integer len_val;
    if(to_integer(to_constant_expr(src.operands()[0]), len_val))
      return;
    const exprt &keys_arr = src.operands()[1];
    std::set<std::string> keys;
    bool all_constant = true;
    for(mp_integer i = 0; i < len_val; ++i)
    {
      std::size_t idx = i.to_ulong();
      if(idx >= keys_arr.operands().size())
        break;
      auto kv = extract_string_value(keys_arr.operands()[idx]);
      if(!kv.has_value())
      {
        all_constant = false;
        break;
      }
      keys.insert(kv.value());
    }
    if(all_constant && !keys.empty())
      function_returned_dict_keys[current_function] = std::move(keys);
  };
  if(!current_function.empty())
    ++function_return_count[current_function];
  if(
    !current_function.empty() && ret_val.id() == ID_struct &&
    is_python_dict_type(ret_val.type()))
  {
    record_function_returned_keys(ret_val);
    function_returned_literal[current_function] = ret_val;
  }
  else if(
    !current_function.empty() && ret_val.id() == ID_symbol &&
    is_python_dict_type(ret_val.type()))
  {
    auto it = dict_literals.find(to_symbol_expr(ret_val).get_identifier());
    if(it != dict_literals.end())
    {
      record_function_returned_keys(it->second);
      function_returned_literal[current_function] = it->second;
    }
  }
  else if(
    !current_function.empty() && ret_val.id() == ID_struct &&
    (is_python_list_type(ret_val.type()) ||
     is_python_tuple_type(ret_val.type())))
  {
    function_returned_literal[current_function] = ret_val;
  }
  else if(
    !current_function.empty() && ret_val.id() == ID_symbol &&
    is_python_list_type(ret_val.type()))
  {
    auto it = list_literals.find(to_symbol_expr(ret_val).get_identifier());
    if(it != list_literals.end())
      function_returned_literal[current_function] = it->second;
  }
  else if(
    !current_function.empty() && ret_val.id() == ID_symbol &&
    is_python_tuple_type(ret_val.type()))
  {
    auto it = tuple_literals.find(to_symbol_expr(ret_val).get_identifier());
    if(it != tuple_literals.end())
      function_returned_literal[current_function] = it->second;
  }

  if(ret_val.is_nil())
  {
    // Expression conversion failed — return default value
    if(!current_function.empty())
    {
      irep_idt func_id{"python::" + current_function};
      const symbolt *func_sym = symbol_table.lookup(func_id);
      if(
        func_sym != nullptr && func_sym->type.id() == ID_code &&
        to_code_type(func_sym->type).return_type().id() != ID_empty)
      {
        return code_frontend_returnt{
          safe_zero(to_code_type(func_sym->type).return_type())};
      }
    }
    return code_frontend_returnt{};
  }

  // Typecast return value to match function's return type
  if(!current_function.empty())
  {
    irep_idt func_id{"python::" + current_function};
    const symbolt *func_sym = symbol_table.lookup(func_id);
    if(
      func_sym != nullptr && func_sym->type.id() == ID_code &&
      ret_val.type() != to_code_type(func_sym->type).return_type())
    {
      typet ret_type = to_code_type(func_sym->type).return_type();

      // PLR soundness: if the returned value's type is obviously
      // incompatible with the declared return annotation (e.g.
      // 'def f() -> int: return "hello"'), emit an explicit
      // annotation-mismatch property. Without this, our silent
      // safe_typecast hides the mismatch and we'd miss downstream
      // TypeErrors that Python would raise at runtime.
      //
      // Gated behind --python-check-annotations: our frontend's
      // default nondet-int return for unresolved calls produces
      // spurious mismatches against class-typed annotations. Enable
      // only for user-code auditing, not stub-heavy code.
      //
      // Only check when the function has an explicit return
      // annotation; unannotated functions with inferred types
      // shouldn't flag their inferred type as a mismatch.
      if(
        python_check_annotations &&
        annotated_return_functions.count(current_function) > 0 &&
        annotation_types_incompatible(ret_type, ret_val.type()))
      {
        add_check(
          false_exprt{},
          "annotation-mismatch",
          "returned value's type does not match declared return "
          "annotation",
          get_location(stmt));
      }

      // Track functions that return lambdas (before type update)
      if(
        ret_val.id() == ID_symbol && ret_val.type().id() == ID_code &&
        !current_function.empty())
      {
        lambda_returning_functions[current_function] =
          to_symbol_expr(ret_val).get_identifier();
        ret_val = from_integer(0, python_int_type());
      }

      // If declared type doesn't match actual return, update function type.
      // This handles: int→float, int→string, int→list, and
      // list[X]→list[Y] (same tag, different element types).
      if(
        (ret_type == python_int_type() &&
         (ret_val.type().id() == ID_floatbv ||
          is_python_value_type(ret_val.type()) ||
          is_python_string_type(ret_val.type()) ||
          is_python_list_type(ret_val.type()))) ||
        (ret_val.type().id() == ID_struct && ret_type.id() == ID_struct &&
         ret_val.type() != ret_type &&
         to_struct_type(ret_val.type()).get_tag() ==
           to_struct_type(ret_type).get_tag()))
      {
        code_typet new_type = to_code_type(func_sym->type);
        new_type.return_type() = ret_val.type();
        symbol_table.get_writeable_ref(func_id).type = new_type;
      }
      else
      {
        // Dereference pointer returns for class reference params
        if(
          ret_val.type().id() == ID_pointer && ret_type.id() == ID_struct &&
          to_pointer_type(ret_val.type()).base_type() == ret_type)
          ret_val = dereference_exprt{ret_val};
        else if(
          ret_val.type().id() == ID_struct && ret_type.id() == ID_struct &&
          to_struct_type(ret_val.type()).get_tag() ==
            to_struct_type(ret_type).get_tag())
        {
          // Same struct tag (e.g., both python_list) but different
          // component types — treat as compatible (Python is dynamically typed)
        }
        else
        {
          // PLR §3.2 boundary: route through coerce_return_value,
          // which delegates to safe_typecast and (for
          // python_value targets) wrap_value. Centralises the
          // wrap-vs-coerce decision so any future PLR rule
          // change applies once at coerce_to_typed_slot.
          ret_val = coerce_return_value(ret_val, ret_type);
        }
      }
    }
  }

  code_frontend_returnt ret{ret_val};
  ret.add_source_location() = get_location(stmt);
  return std::move(ret);
}

exprt python_convertert::lower_pv_iterable(
  const exprt &iterable,
  code_blockt &header,
  const source_locationt &loc)
{
  // Iteration context: the PLR §6.13 iterability obligation (catchable per
  // §8.4) PLUS the dispatch view. Binding contexts (unwrap_value) use
  // pv_class_iter_view alone: a `xs: list = <Any>` binding does not raise
  // in CPython -- only iterating does -- so the obligation belongs here.
  // pv-CLASS __iter__ dispatch (per-instance provenance, phase 1 -- see
  // plan §1): a class instance boxed in a python_value iterates via ITS
  // __iter__, not the list slot (garbage for a CLASS tag: the loop element
  // was a wholly-nondet pv and every use false-alarmed -- the boto3
  // response-object family). Single user-class owner with a list-typed
  // return. The call is MATERIALISED into a temp first and the ternary
  // selects between SYMBOLS -- a side_effect function call nested in an
  // if_exprt arm is never lowered by goto-convert (the mundane bug behind
  // four earlier reverted attempts). Other tags keep the list-slot view.
  std::vector<std::string> it_owners;
  std::vector<std::string> iter_capable;
  for(const auto &p : class_types)
  {
    const bool has_iter =
      symbol_table.lookup(irep_idt{"python::" + p.first + "::__iter__"}) !=
      nullptr;
    // CPython's legacy iteration protocol: a class with __getitem__ but no
    // __iter__ IS iterable (indexed from 0 until IndexError).
    const bool has_getitem =
      symbol_table.lookup(irep_idt{"python::" + p.first + "::__getitem__"}) !=
      nullptr;
    // PLR §3.3.1 iterator protocol: an __iter__ classified INVALID
    // (provably returns a non-iterator -- a literal list, a constant,
    // `return self` without __next__) raises
    // "TypeError: iter() returned non-iterator" and SHADOWS the legacy
    // __getitem__ protocol (verified against CPython). Such a class is NOT
    // iter-capable and must not take the dispatch path. VALID/UNKNOWN keep
    // the previous behavior (UNKNOWN unflagged -- precision default).
    const auto ipk = class_iter_protocol.find(p.first);
    const bool iter_invalid = ipk != class_iter_protocol.end() &&
                              ipk->second == iter_protocol_kindt::INVALID;
    if(has_iter && !iter_invalid)
      it_owners.push_back(p.first);
    if((has_iter && !iter_invalid) || (!has_iter && has_getitem))
      iter_capable.push_back(p.first);
  }
  // PLR §6.13: iterating a value whose runtime tag is not iterable
  // (int/float/bool/complex/closure -- or a CLASS instance whose class
  // defines neither __iter__ nor __getitem__) raises TypeError
  // ("'X' object is not iterable"). The list-slot deref below assumed
  // iterability unconditionally, so `for c in r` with r bound to an int
  // behind Any silently iterated garbage (a false proof). The CLASS arm
  // dispatches on per-instance class identity (__class_tag). NONE is
  // deliberately excluded here: None-iteration stays behind the
  // established opt-in --python-check-iter-none (see iter_none_check
  // above); including it by default would flip that documented
  // precision default.
  {
    exprt iterable_ok = or_exprt{
      or_exprt{
        python_value_is(iterable, python_type_tagt::NONE),
        or_exprt{
          python_value_is(iterable, python_type_tagt::STR),
          python_value_is(iterable, python_type_tagt::LIST)}},
      or_exprt{
        or_exprt{
          python_value_is(iterable, python_type_tagt::DICT),
          python_value_is(iterable, python_type_tagt::SET)},
        or_exprt{
          python_value_is(iterable, python_type_tagt::TUPLE),
          python_value_is_class_of(iterable, iter_capable)}}};
    // PLR §8.4: catchable when an enclosing handler covers TypeError;
    // a definite property otherwise (mirrors the subscript obligation).
    if(exception_is_caught("TypeError"))
    {
      const std::size_t before = pending_checks.size();
      emit_conditional_exception(not_exprt{iterable_ok}, "TypeError");
      for(std::size_t i = before; i < pending_checks.size(); ++i)
        header.add(std::move(pending_checks[i]));
      pending_checks.erase(
        pending_checks.begin() + before, pending_checks.end());
    }
    else
    {
      source_locationt tloc = loc;
      tloc.set_property_class("type-error");
      tloc.set_comment("object is not iterable");
      code_assertt te{std::move(iterable_ok)};
      te.add_source_location() = tloc;
      header.add(std::move(te));
    }
  }
  return pv_class_iter_view(iterable, header, loc);
}

exprt python_convertert::pv_class_iter_view(
  const exprt &iterable,
  code_blockt &header,
  const source_locationt &loc)
{
  std::vector<std::string> it_owners;
  for(const auto &p : class_types)
  {
    const bool has_iter =
      symbol_table.lookup(irep_idt{"python::" + p.first + "::__iter__"}) !=
      nullptr;
    const auto ipk = class_iter_protocol.find(p.first);
    const bool iter_invalid = ipk != class_iter_protocol.end() &&
                              ipk->second == iter_protocol_kindt::INVALID;
    if(has_iter && !iter_invalid)
      it_owners.push_back(p.first);
  }
  exprt list_view = python_value_list(iterable);
  if(it_owners.size() == 1)
  {
    const symbolt &is_sym = symbol_table.lookup_ref(
      irep_idt{"python::" + it_owners[0] + "::__iter__"});
    if(is_sym.type.id() == ID_code)
    {
      const code_typet &it_t = to_code_type(is_sym.type);
      if(is_python_list_type(it_t.return_type()))
      {
        const typet &cls_t = class_types.at(it_owners[0]);
        exprt self_ptr = typecast_exprt{
          python_value_class_ptr(iterable), pointer_typet{cls_t, 64}};
        // Materialise the dispatched call.
        static unsigned it_disp_ctr = 0;
        const std::string tn = "__iter_disp_" + std::to_string(it_disp_ctr++);
        const irep_idt tid{qualify_name(tn)};
        if(symbol_table.lookup(tid) == nullptr)
        {
          symbolt ts{tid, it_t.return_type(), "python"};
          ts.base_name = tn;
          ts.is_lvalue = true;
          ts.is_state_var = true;
          ts.is_static_lifetime = current_function.empty();
          symbol_table.add(ts);
        }
        symbol_exprt tsym = symbol_table.lookup_ref(tid).symbol_expr();
        side_effect_expr_function_callt it_call{
          is_sym.symbol_expr(), {self_ptr}, it_t.return_type(), loc};
        header.add(code_frontend_assignt{tsym, it_call});
        if(it_t.return_type() == list_view.type())
          list_view = if_exprt{
            python_value_is_class_of(iterable, it_owners),
            tsym,
            std::move(list_view)};
        else
        {
          // Different element type: materialise a same-typed VIEW (length
          // from the dispatched result, elements nondet -- sound: with the
          // stub-common length 0 no element ever binds).
          const std::string vn = tn + "_pv";
          const irep_idt vid{qualify_name(vn)};
          if(symbol_table.lookup(vid) == nullptr)
          {
            symbolt vs{vid, list_view.type(), "python"};
            vs.base_name = vn;
            vs.is_lvalue = true;
            vs.is_state_var = true;
            vs.is_static_lifetime = current_function.empty();
            symbol_table.add(vs);
          }
          symbol_exprt vsym = symbol_table.lookup_ref(vid).symbol_expr();
          header.add(code_frontend_assignt{
            vsym, side_effect_expr_nondett{list_view.type(), loc}});
          header.add(code_frontend_assignt{
            member_exprt{vsym, "length", signedbv_typet{64}},
            member_exprt{tsym, "length", signedbv_typet{64}}});
          // Per-instance ELEMENT provenance (phase 2): each in-bounds slot
          // of the view is the WRAPPED dispatched element, not nondet --
          // `total += v` over `__iter__ -> iter([1, 2])` now computes 3
          // instead of nondet. Out-of-bounds slots stay nondet (never
          // read: the loop is bounded by length).
          {
            // Both types are plain python_list structs: guaranteed by the
            // is_python_list_type gate above (ID_struct only).
            const auto &ret_st = to_struct_type(it_t.return_type());
            const auto &src_arr_t =
              to_array_type(ret_st.components()[1].type());
            const auto &dst_st = to_struct_type(list_view.type());
            const auto &dst_arr_t =
              to_array_type(dst_st.components()[1].type());
            member_exprt src_data{tsym, "data", src_arr_t};
            member_exprt dst_data{vsym, "data", dst_arr_t};
            member_exprt src_len{tsym, "length", signedbv_typet{64}};
            for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; ++i)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt elem = index_exprt{src_data, idx};
              exprt wrapped = wrap_value(elem);
              code_frontend_assignt asg{
                index_exprt{dst_data, idx}, std::move(wrapped)};
              header.add(code_ifthenelset{
                binary_relation_exprt{idx, ID_lt, src_len}, std::move(asg)});
            }
          }
          list_view = if_exprt{
            python_value_is_class_of(iterable, it_owners),
            vsym,
            std::move(list_view)};
        }
      }
    }
  }
  return list_view;
}
