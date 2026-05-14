/// Python to GOTO converter — control-flow statement
/// handlers: assert (PLR §7.3), if (§8.1), while (§8.2),
/// for (§8.3), return (§7.6).
///
/// Extracted from python_converter.cpp to reduce the main file size.
/// All logic and class-member state remains unchanged — this file is
/// a pure source-split.

#include <util/arith_tools.h>
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
    return code_skipt{};

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
  if_else_depth++;

  // Convert body
  code_blockt then_block;
  const jsont &body = json_member(stmt, "body");
  if(body.is_array())
  {
    for(const auto &s : as_array(body))
      then_block.add(convert_statement(s));
  }

  // Save then-branch versions, restore for else branch
  auto then_versions = variable_versions;
  variable_versions = saved_versions;

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
    for(const auto &s : as_array(body))
      body_block.add(convert_statement(s));
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
  auto finalize_for = [&](codet &&c) -> codet
  {
    if(!have_orelse_for)
      return std::move(c);
    loop_break_flags.pop_back();
    code_blockt outer;
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

  std::string var_name = json_string(json_member(target, "id"));
  // For tuple targets (for a, b in ...), use a synthetic name
  if(var_name.empty() && is_node_type(target, "Tuple"))
  {
    static unsigned tuple_iter_ctr = 0;
    var_name = "__tuple_iter_" + std::to_string(tuple_iter_ctr++);
  }
  std::string qualified_name = qualify_name(var_name);

  // Check for range() call
  if(
    is_node_type(iter, "Call") &&
    json_string(json_member(json_member(iter, "func"), "id")) == "range")
  {
    const jsont &range_args = json_member(iter, "args");
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
      for(const auto &s : as_array(body))
        body_block.add(convert_statement(s));
    }
    body_block.add(code_frontend_assignt{loop_sym, plus_exprt{loop_sym, step}});

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

    code_whilet while_stmt{cond, std::move(body_block)};
    while_stmt.add_source_location() = loc;

    code_blockt result;
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

  // If the iterable is a complex expression (e.g., enumerate() result),
  // store it in a temp symbol so it doesn't get simplified away.
  code_blockt pre_loop;
  if(iterable.id() != ID_symbol && iterable.type().id() == ID_struct)
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
    iterable = symbol_table.lookup_ref(ti).symbol_expr();
  }

  bool is_list = is_python_list_type(iterable.type());
  bool is_string = is_python_string_type(iterable.type());
  bool is_dict = is_python_dict_type(iterable.type());

  // PLR §8.3: "for k in dict" iterates over keys (array-based)
  if(is_dict)
  {
    const auto &dict_st = to_struct_type(iterable.type());
    const auto &keys_type = to_array_type(dict_st.components()[1].type());
    typet key_type = keys_type.element_type();

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

    code_blockt body_block;
    exprt key_val = index_exprt{keys, idx_var};
    if(key_val.type() != loop_var.type())
      key_val = safe_typecast(key_val, loop_var.type());
    body_block.add(code_frontend_assignt{loop_var, key_val});

    const jsont &body_stmts = json_member(stmt, "body");
    if(body_stmts.is_array())
    {
      for(const auto &s : as_array(body_stmts))
        body_block.add(convert_statement(s));
    }
    body_block.add(code_frontend_assignt{
      idx_var, plus_exprt{idx_var, from_integer(1, signedbv_typet{64})}});

    code_whilet while_stmt{
      binary_relation_exprt{idx_var, ID_lt, length}, std::move(body_block)};
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
          for(const auto &s : as_array(body_j))
            body_block.add(convert_statement(s));
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
    }

    log_overapprox(
      "for-in iteration: unsupported iterable type, body executed once "
      "with loop variable nondet (so contained call sites are still "
      "type-checked)");
    {
      // Flush any pending checks that the iter-expression
      // conversion accumulated (e.g. attribute-error
      // properties from .items() on a class without that
      // method). These belong BEFORE the loop, not inside
      // the conditionally-executed body.
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
      symbol_exprt loop_sym =
        symbol_table.lookup_ref(symbol_id).symbol_expr();
      // Use a one-shot while loop so any 'continue' or 'break'
      // inside the body has a valid target. We wrap the body
      // in: while(__once && __skip) { body; __once = false; }
      // The condition is nondet only on the first iteration;
      // subsequent iterations exit via the __once flag. This
      // ensures the body is processed exactly once (when the
      // verifier non-deterministically chooses 'loop ran') or
      // not at all (when 'loop did not run').
      static unsigned for_skip_ctr = 0;
      std::string skip_name =
        "__for_skip_" + std::to_string(for_skip_ctr);
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
        for(const auto &s : as_array(body))
          body_once.add(convert_statement(s));
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

  member_exprt length{iterable, "length", int_type};
  typet data_field_type;
  if(is_python_string_type(iterable.type()))
    data_field_type = pointer_typet(unsignedbv_typet{8}, 64);
  else if(iterable.type().id() == ID_struct)
    data_field_type = to_struct_type(iterable.type()).components()[1].type();
  else
    data_field_type = signedbv_typet{64}; // fallback
  member_exprt data{iterable, "data", data_field_type};

  code_blockt result;

  // __idx = 0
  result.add(code_frontend_assignt{idx_var, from_integer(0, int_type)});

  // while(__idx < iterable.length)
  code_blockt body_block;

  // x = iterable.data[__idx] (typecast if needed)
  exprt elem_val = is_string
                     ? exprt(dereference_exprt{plus_exprt{data, idx_var}})
                     : exprt(index_exprt{data, idx_var});

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
          if(symbol_table.lookup(elt_id) == nullptr)
          {
            symbolt elt_sym{elt_id, python_int_type(), "python"};
            elt_sym.base_name = elt_name;
            elt_sym.is_lvalue = true;
            elt_sym.is_state_var = true;
            symbol_table.add(elt_sym);
          }
          symbol_exprt elt_var = symbol_table.lookup_ref(elt_id).symbol_expr();
          // Access tuple field: elem._0, elem._1, etc.
          std::string field = "_" + std::to_string(tidx);
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
      // Build pointer-based single-char string: {length=1, data=&[char]}
      exprt::operandst chars;
      chars.push_back(elem_val);
      array_typet at(unsignedbv_typet{8}, from_integer(1, signedbv_typet{64}));
      array_exprt arr(std::move(chars), at);
      exprt ptr = address_of_exprt(index_exprt(
        arr, from_integer(0, signedbv_typet{64}), unsignedbv_typet{8}));
      exprt len_one = from_integer(1, signedbv_typet{64});
      elem_val = struct_exprt{{len_one, ptr}, python_string_type()};
    }
    else if(elem_val.type() != loop_var.type())
      elem_val = safe_typecast(elem_val, loop_var.type());
    body_block.add(code_frontend_assignt{loop_var, elem_val});
  } // end else (non-tuple target)

  // user body
  const jsont &body = json_member(stmt, "body");
  if(body.is_array())
  {
    for(const auto &s : as_array(body))
      body_block.add(convert_statement(s));
  }

  // __idx += 1
  body_block.add(code_frontend_assignt{
    idx_var, plus_exprt{idx_var, from_integer(1, int_type)}});

  code_whilet while_stmt{
    binary_relation_exprt{idx_var, ID_lt, length}, std::move(body_block)};
  while_stmt.add_source_location() = loc;
  result.add(std::move(while_stmt));

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
    // Bare return — check if function expects a return value
    if(!current_function.empty())
    {
      irep_idt func_id{"python::" + current_function};
      const symbolt *func_sym = symbol_table.lookup(func_id);
      if(
        func_sym != nullptr && func_sym->type.id() == ID_code &&
        to_code_type(func_sym->type).return_type().id() != ID_empty)
      {
        return code_frontend_returnt{
          from_integer(0, to_code_type(func_sym->type).return_type())};
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

      const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);
      code_blockt block;

      irep_idt init_id{"python::" + call_name + "::__init__"};
      const symbolt *init_sym = symbol_table.lookup(init_id);
      if(init_sym != nullptr)
      {
        exprt::operandst args;
        args.push_back(address_of_exprt{tmp_sym.symbol_expr()});
        const jsont &call_args = json_member(value, "args");
        if(call_args.is_array())
        {
          for(const auto &a : as_array(call_args))
            args.push_back(convert_expression(a));
        }
        // Match argument types to parameter types
        const auto &init_params = to_code_type(init_sym->type).parameters();
        for(std::size_t ai = 0; ai < args.size() && ai < init_params.size();
            ai++)
        {
          if(args[ai].type() != init_params[ai].type())
            args[ai] = safe_typecast(args[ai], init_params[ai].type());
        }
        side_effect_expr_function_callt call{
          init_sym->symbol_expr(), std::move(args), empty_typet{}, loc};
        block.add(code_expressiont{call});
      }

      // Typecast to function's return type if needed
      exprt ret_expr = tmp_sym.symbol_expr();
      irep_idt func_id{"python::" + current_function};
      const symbolt *func_sym = symbol_table.lookup(func_id);
      if(func_sym != nullptr && func_sym->type.id() == ID_code)
      {
        typet ret_type = to_code_type(func_sym->type).return_type();
        if(ret_expr.type() != ret_type)
          ret_expr = safe_typecast(ret_expr, ret_type);
      }
      block.add(code_frontend_returnt{ret_expr});
      return std::move(block);
    }
  }

  exprt ret_val = convert_expression(value);

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
  if(
    !current_function.empty() && ret_val.id() == ID_struct &&
    is_python_dict_type(ret_val.type()))
  {
    record_function_returned_keys(ret_val);
  }
  else if(
    !current_function.empty() && ret_val.id() == ID_symbol &&
    is_python_dict_type(ret_val.type()))
  {
    auto it = dict_literals.find(to_symbol_expr(ret_val).get_identifier());
    if(it != dict_literals.end())
      record_function_returned_keys(it->second);
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
          from_integer(0, to_code_type(func_sym->type).return_type())};
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
          ret_val = safe_typecast(ret_val, ret_type);
      }
    }
  }

  code_frontend_returnt ret{ret_val};
  ret.add_source_location() = get_location(stmt);
  return std::move(ret);
}
