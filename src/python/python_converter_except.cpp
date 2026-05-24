/// Python to GOTO converter — break (PLR §7.9), continue
/// (§7.10), pass (§7.1), raise (§7.8), with (§8.5), try
/// (§8.4) statement handlers.
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

// PLR §7.9: The break statement
// "break may only occur syntactically nested in a for or while loop."
codet python_convertert::convert_break()
{
  // PLR §8.2 / §8.3: if we are inside a for/while with an else
  // clause, set the enclosing loop's break-flag so the else
  // clause is skipped after the loop exits via break.
  if(!loop_break_flags.empty())
  {
    irep_idt flag_id = loop_break_flags.back();
    const symbolt *flag_sym = symbol_table.lookup(flag_id);
    if(flag_sym != nullptr)
    {
      code_blockt block;
      block.add(code_frontend_assignt{flag_sym->symbol_expr(), true_exprt{}});
      block.add(code_breakt{});
      return std::move(block);
    }
  }
  return code_breakt{};
}

// PLR §7.10: The continue statement
// "continue may only occur syntactically nested in a for or while loop."
codet python_convertert::convert_continue()
{
  return code_continuet{};
}

// PLR §7.1: Expression statements / pass
// "pass is a null operation — when it is executed, nothing happens."
codet python_convertert::convert_pass()
{
  return code_skipt{};
}

// PLR §7.8: The raise statement
// "raise evaluates the first expression as the exception object. It must
// be either a subclass or an instance of BaseException."
codet python_convertert::convert_raise(const jsont &stmt)
{
  source_locationt loc = get_location(stmt);

  // Extract exception type name for the error message
  std::string exc_type = "Exception";
  std::vector<std::string> group_types; // PEP 654 multi-element
  const jsont &exc = json_member(stmt, "exc");
  if(!exc.is_null())
  {
    if(is_node_type(exc, "Call"))
    {
      const jsont &func = json_member(exc, "func");
      if(is_node_type(func, "Name"))
        exc_type = json_string(json_member(func, "id"));
      // PEP 654: ExceptionGroup(msg, [e1, e2, ...]).
      // Collect all element-type names for multi-element
      // support. For a single element, treat as raising
      // that one type. For multiple, emit a nondet
      // selection: set __exception_type to any of the
      // collected hashes — symex will explore each
      // possibility, giving per-type except* coverage.
      if(exc_type == "ExceptionGroup" || exc_type == "BaseExceptionGroup")
      {
        const jsont &args = json_member(exc, "args");
        if(args.is_array() && as_array(args).size() >= 2)
        {
          auto args_it = as_array(args).begin();
          ++args_it;
          const jsont &excs = *args_it;
          if(is_node_type(excs, "List") || is_node_type(excs, "Tuple"))
          {
            const jsont &elts = json_member(excs, "elts");
            if(elts.is_array() && !as_array(elts).empty())
            {
              for(const auto &elt : as_array(elts))
              {
                std::string tn;
                if(is_node_type(elt, "Call"))
                {
                  const jsont &ef = json_member(elt, "func");
                  if(is_node_type(ef, "Name"))
                    tn = json_string(json_member(ef, "id"));
                }
                else if(is_node_type(elt, "Name"))
                {
                  tn = json_string(json_member(elt, "id"));
                }
                if(!tn.empty())
                  group_types.push_back(tn);
              }
              if(!group_types.empty())
                exc_type = group_types.front();
            }
          }
        }
      }
    }
    else if(is_node_type(exc, "Name"))
      exc_type = json_string(json_member(exc, "id"));
  }

  code_blockt block;

  // Set the exception flag and type
  irep_idt exc_id{"python::__exception_active"};
  const symbolt *exc_sym = symbol_table.lookup(exc_id);
  if(exc_sym != nullptr)
  {
    code_frontend_assignt set_flag{exc_sym->symbol_expr(), true_exprt{}};
    set_flag.add_source_location() = loc;
    block.add(std::move(set_flag));
  }

  // PLR §7.8: bare `raise` re-raises the currently active
  // exception without changing its type or payload. Only set
  // __exception_type / __exception_payload when the raise has
  // an operand.
  if(exc.is_null())
    return std::move(block);

  // Set exception type (hash of type name for matching)
  irep_idt exc_type_sym_id{"python::__exception_type"};
  const symbolt *exc_type_sym = symbol_table.lookup(exc_type_sym_id);
  if(exc_type_sym != nullptr)
  {
    if(group_types.size() > 1)
    {
      // PEP 654 multi-element: emit a nondet selector so
      // symex explores each possible raised type. Chain
      // of if-else: if(sel==0) type=hash(T0); elif(sel==1)
      // type=hash(T1); ...
      symbol_exprt etype = exc_type_sym->symbol_expr();
      side_effect_expr_nondett sel{python_int_type(), loc};
      code_blockt chain;
      for(std::size_t i = 0; i < group_types.size(); ++i)
      {
        long h = exception_type_hash(group_types[i]);
        code_frontend_assignt assign{etype, from_integer(h, etype.type())};
        if(i + 1 == group_types.size())
        {
          chain.add(std::move(assign));
        }
        else
        {
          code_ifthenelset guarded{
            equal_exprt{sel, from_integer(i, sel.type())}, std::move(assign)};
          chain.add(std::move(guarded));
        }
      }
      block.add(std::move(chain));
    }
    else
    {
      // Use a simple hash: sum of character values
      long type_hash = exception_type_hash(exc_type);
      code_frontend_assignt set_type{
        exc_type_sym->symbol_expr(),
        from_integer(type_hash, python_int_type())};
      set_type.add_source_location() = loc;
      block.add(std::move(set_type));
    }
  }

  // Exception payload: first positional arg (typically a message
  // string). Register a global '__exception_payload' string symbol
  // on first use so 'except T as e: str(e)' can read it back.
  if(is_node_type(exc, "Call"))
  {
    const jsont &exc_args = json_member(exc, "args");
    if(exc_args.is_array() && !as_array(exc_args).empty())
    {
      irep_idt payload_id{"python::__exception_payload"};
      if(symbol_table.lookup(payload_id) == nullptr)
      {
        symbolt ps{payload_id, python_string_type(), "python"};
        ps.base_name = "__exception_payload";
        ps.is_lvalue = true;
        ps.is_state_var = true;
        symbol_table.add(ps);
      }
      exprt arg0 = convert_expression(*as_array(exc_args).begin());
      if(is_python_string_type(arg0.type()))
      {
        code_frontend_assignt set_payload{
          symbol_table.lookup_ref(payload_id).symbol_expr(), arg0};
        set_payload.add_source_location() = loc;
        block.add(std::move(set_payload));
      }
    }
  }

  // Add a failing assertion for uncaught exceptions only at top level
  // outside of try blocks. Skipped when --python-no-exception-checks
  // is set: benchmark suites that judge only assertion failures
  // shouldn't see 'raise' as a property violation.
  if(current_function.empty() && try_depth == 0 && !python_no_exception_checks)
  {
    loc.set_property_class("exception");
    loc.set_comment("raise " + exc_type);
    code_assertt assertion{false_exprt{}};
    assertion.add_source_location() = loc;
    block.add(std::move(assertion));

    code_assumet assume{false_exprt{}};
    assume.add_source_location() = loc;
    block.add(std::move(assume));
  }
  else if(try_depth > 0)
  {
    // PLR §8.4: raise inside a try body. Do NOT return here —
    // the enclosing convert_try body loop guards each
    // subsequent statement with !__exception_active so the
    // raise effectively skips the rest of the try body, and
    // the except/finally machinery takes over. Emitting a
    // return here would bypass the handler.
  }
  else
  {
    // Inside a function but outside any try block: the
    // exception flag is set and we must unwind to the
    // caller. Return a value of the correct type.
    irep_idt func_id{"python::" + current_function};
    const symbolt *func_sym = symbol_table.lookup(func_id);
    if(
      func_sym != nullptr &&
      to_code_type(func_sym->type).return_type().id() == ID_empty)
    {
      block.add(code_frontend_returnt{});
    }
    else if(func_sym != nullptr)
    {
      // Return nondet of the declared type. If the type is later
      // updated by a return statement, CBMC's GOTO conversion will
      // handle the typecast.
      typet ret_type = to_code_type(func_sym->type).return_type();
      block.add(code_frontend_returnt{
        side_effect_expr_nondett{ret_type, source_locationt{}}});
    }
  }

  return std::move(block);
}

// PLR §8.5: The with statement
// "The with statement is used to wrap the execution of a block with
// methods defined by a context manager."
codet python_convertert::convert_with(const jsont &stmt)
{
  // Simplified: execute the body, ignoring __enter__/__exit__ protocol.
  // If there's an 'as' variable, assign the context_expr to it.
  code_blockt block;
  source_locationt loc = get_location(stmt);

  const jsont &items = json_member(stmt, "items");
  if(items.is_array())
  {
    for(const auto &item : as_array(items))
    {
      const jsont &optional_vars = json_member(item, "optional_vars");
      if(!optional_vars.is_null() && is_node_type(optional_vars, "Name"))
      {
        std::string var_name = json_string(json_member(optional_vars, "id"));
        std::string qname = qualify_name(var_name);
        irep_idt sym_id{qname};

        const jsont &ctx_expr = json_member(item, "context_expr");

        // Check if context_expr is a constructor call
        if(
          is_node_type(ctx_expr, "Call") &&
          is_node_type(json_member(ctx_expr, "func"), "Name") &&
          class_types.count(
            json_string(json_member(json_member(ctx_expr, "func"), "id"))))
        {
          std::string cls_name =
            json_string(json_member(json_member(ctx_expr, "func"), "id"));
          const struct_typet &cls_type = class_types[cls_name];

          if(symbol_table.lookup(sym_id) == nullptr)
          {
            symbolt new_sym{sym_id, cls_type, "python"};
            new_sym.base_name = var_name;
            new_sym.is_lvalue = true;
            new_sym.is_state_var = true;
            symbol_table.add(new_sym);
          }

          // Call __init__
          irep_idt init_id{"python::" + cls_name + "::__init__"};
          const symbolt *init_sym = symbol_table.lookup(init_id);
          if(init_sym != nullptr)
          {
            const symbolt &var_sym = symbol_table.lookup_ref(sym_id);
            exprt::operandst args;
            args.push_back(address_of_exprt{var_sym.symbol_expr()});
            const jsont &call_args = json_member(ctx_expr, "args");
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
        }
        else
        {
          // Non-constructor: with expr as x → x = expr
          exprt ctx = convert_expression(ctx_expr);
          if(!ctx.is_nil())
          {
            if(symbol_table.lookup(sym_id) == nullptr)
            {
              symbolt new_sym{sym_id, ctx.type(), "python"};
              new_sym.base_name = var_name;
              new_sym.is_lvalue = true;
              new_sym.is_state_var = true;
              symbol_table.add(new_sym);
            }
            const symbolt &sym = symbol_table.lookup_ref(sym_id);
            code_frontend_assignt assign{sym.symbol_expr(), ctx};
            assign.add_source_location() = loc;
            block.add(std::move(assign));
          }
        }
      }
    }
  }

  // Convert the body
  const jsont &body = json_member(stmt, "body");
  if(body.is_array())
  {
    for(const auto &s : as_array(body))
      block.add(convert_statement(s));
  }

  return std::move(block);
}

// --- Module body conversion ---

// PLR §8.4: The try statement
// PLR §3.2: "Exceptions are identified by class instances."
// We model exceptions via __exception_active (bool) and
// __exception_type (hash of exception class name).
// "The try statement specifies exception handlers and/or cleanup code
// for a group of statements."
codet python_convertert::convert_try(const jsont &stmt)
{
  code_blockt block;
  source_locationt loc = get_location(stmt);

  irep_idt exc_id{"python::__exception_active"};
  const symbolt *exc_sym = symbol_table.lookup(exc_id);

  // PLR §8.4: Execute the try body.
  // After each statement, if an exception was raised, skip remaining
  // statements (they are guarded by !__exception_active).
  const jsont &body = json_member(stmt, "body");
  try_depth++;
  // Record this try's handler types for definitively-unhandled-
  // exception detection during conversion of the body. Each
  // handler's 'type' field can be:
  //   - a single Name (e.g. except ClientError:)
  //   - a Tuple of Names (e.g. except (A, B):)
  //   - missing / null (bare 'except:' → catch-all).
  {
    std::set<std::string> caught;
    const jsont &hs = json_member(stmt, "handlers");
    if(hs.is_array())
    {
      for(const auto &h : as_array(hs))
      {
        const jsont &ht = json_member(h, "type");
        if(ht.is_null())
        {
          caught.insert(""); // bare except:
          continue;
        }
        auto add_name = [&](const jsont &n)
        {
          if(is_node_type(n, "Name"))
            caught.insert(json_string(json_member(n, "id")));
          else if(is_node_type(n, "Attribute"))
            caught.insert(json_string(json_member(n, "attr")));
        };
        if(is_node_type(ht, "Tuple"))
        {
          const jsont &elts = json_member(ht, "elts");
          if(elts.is_array())
            for(const auto &e : as_array(elts))
              add_name(e);
        }
        else
          add_name(ht);
      }
    }
    active_exception_handlers.push_back(std::move(caught));
  }
  if(body.is_array())
  {
    bool first = true;
    // PLR §8.4: statements in a try body after the first are
    // guarded by ¬exception_active — they form branches whose
    // execution depends on the exception state, so path-
    // insensitive tracking should be invalidated.
    if_else_depth++;
    for(const auto &s : as_array(body))
    {
      codet stmt_code = convert_statement(s);
      // First statement runs unconditionally; subsequent ones are
      // guarded so that a raise in an earlier statement skips them.
      if(!first && exc_sym != nullptr)
      {
        code_ifthenelset guarded{
          not_exprt{exc_sym->symbol_expr()}, std::move(stmt_code)};
        block.add(std::move(guarded));
      }
      else
        block.add(std::move(stmt_code));
      first = false;
    }
    if_else_depth--;
  }
  try_depth--;
  active_exception_handlers.pop_back();

  // Check for except handlers
  const jsont &handlers = json_member(stmt, "handlers");

  // PLR §8.4: the else clause runs iff no exception was raised
  // in the try body. Snapshot __exception_active BEFORE the
  // handler_chain runs (the handler clears it when catching),
  // so we can test the pre-handler state.
  const jsont &orelse = json_member(stmt, "orelse");
  bool has_else = orelse.is_array() && !as_array(orelse).empty();
  exprt exc_before =
    exc_sym != nullptr ? exprt{exc_sym->symbol_expr()} : exprt{};
  if(has_else && exc_sym != nullptr)
  {
    static unsigned try_else_ctr = 0;
    std::string tn = "__try_exc_before_" + std::to_string(try_else_ctr++);
    std::string tq = qualify_name(tn);
    irep_idt tid{tq};
    if(symbol_table.lookup(tid) == nullptr)
    {
      symbolt ts{tid, bool_typet{}, "python"};
      ts.base_name = tn;
      ts.is_lvalue = true;
      ts.is_state_var = true;
      symbol_table.add(ts);
    }
    exc_before = symbol_table.lookup_ref(tid).symbol_expr();
    block.add(code_frontend_assignt{exc_before, exc_sym->symbol_expr()});
  }

  if(handlers.is_array() && !as_array(handlers).empty() && exc_sym != nullptr)
  {
    // PLR §8.4: iterate all except handlers sequentially
    const symbolt *exc_type_sym =
      symbol_table.lookup("python::__exception_type");

    // Build chained if-elif for each handler
    codet handler_chain = code_skipt{};

    // Process handlers in reverse to build the chain from inside out
    std::vector<const jsont *> handler_list;
    for(const auto &h : as_array(handlers))
      handler_list.push_back(&h);

    for(auto it = handler_list.rbegin(); it != handler_list.rend(); ++it)
    {
      const jsont &handler = **it;
      const jsont &handler_type = json_member(handler, "type");

      code_blockt except_block;
      except_block.add(
        code_frontend_assignt{exc_sym->symbol_expr(), false_exprt{}});

      // Handle "except Type as name:" — create the name variable
      const jsont &handler_name = json_member(handler, "name");
      if(
        !handler_name.is_null() && handler_name.is_string() &&
        !handler_name.value.empty())
      {
        std::string ename = handler_name.value;
        std::string eqname = qualify_name(ename);
        irep_idt eid{eqname};
        // Exception-bound name is typed as python_string so
        // 'except T as e: str(e)' can return the payload
        // registered by raise (or an empty string if no
        // payload was set).
        if(symbol_table.lookup(eid) == nullptr)
        {
          symbolt esym{eid, python_string_type(), "python"};
          esym.base_name = ename;
          esym.is_lvalue = true;
          esym.is_state_var = true;
          symbol_table.add(esym);
        }
        // If __exception_payload exists, copy it; otherwise
        // assign an empty string.
        irep_idt payload_id{"python::__exception_payload"};
        const symbolt *payload_sym = symbol_table.lookup(payload_id);
        if(payload_sym != nullptr)
        {
          except_block.add(code_frontend_assignt{
            symbol_table.lookup_ref(eid).symbol_expr(),
            payload_sym->symbol_expr()});
        }
        else
        {
          except_block.add(code_frontend_assignt{
            symbol_table.lookup_ref(eid).symbol_expr(),
            python_string_literal("")});
        }
      }

      const jsont &handler_body = json_member(handler, "body");
      if(handler_body.is_array())
      {
        // PLR §8.4: each except handler is a branch — increment
        // if_else_depth so path-insensitive tracking
        // (string_constants, etc.) is invalidated for variables
        // assigned inside, the same way as if/else and match
        // case bodies.
        if_else_depth++;
        for(const auto &s : as_array(handler_body))
          except_block.add(convert_statement(s));
        if_else_depth--;
      }

      exprt condition = exc_sym->symbol_expr();
      if(
        !handler_type.is_null() && is_node_type(handler_type, "Name") &&
        exc_type_sym != nullptr)
      {
        std::string htype = json_string(json_member(handler_type, "id"));
        if(htype != "Exception" && !htype.empty())
        {
          long type_hash = exception_type_hash(htype);
          condition = and_exprt{
            condition,
            equal_exprt{
              exc_type_sym->symbol_expr(),
              from_integer(type_hash, python_int_type())}};
        }
      }
      else if(
        !handler_type.is_null() && is_node_type(handler_type, "Tuple") &&
        exc_type_sym != nullptr)
      {
        // except (A, B, C): — match any of the named exception
        // types. Build the OR over their hashes; 'Exception' in
        // the tuple is a catch-all and falls back to the plain
        // active-flag condition.
        const jsont &elts = json_member(handler_type, "elts");
        exprt any_match = false_exprt{};
        bool has_catch_all = false;
        if(elts.is_array())
        {
          for(const auto &elt : as_array(elts))
          {
            if(!is_node_type(elt, "Name"))
              continue;
            std::string htype = json_string(json_member(elt, "id"));
            if(
              htype.empty() || htype == "Exception" || htype == "BaseException")
            {
              has_catch_all = true;
              break;
            }
            long type_hash = exception_type_hash(htype);
            any_match = or_exprt{
              any_match,
              equal_exprt{
                exc_type_sym->symbol_expr(),
                from_integer(type_hash, python_int_type())}};
          }
        }
        if(!has_catch_all)
          condition = and_exprt{condition, any_match};
      }

      handler_chain = code_ifthenelset{
        condition, std::move(except_block), std::move(handler_chain)};
    }

    block.add(std::move(handler_chain));

    // PLR §8.4: else runs iff no exception was raised in try
    // body. Use the snapshot captured before handler_chain.
    if(has_else)
    {
      code_blockt else_block;
      for(const auto &s : as_array(orelse))
        else_block.add(convert_statement(s));
      block.add(code_ifthenelset{not_exprt{exc_before}, std::move(else_block)});
    }
  }
  else
  {
    // No handlers — just execute the else block (guarded by
    // no-exception; in a try/else/finally without except, a
    // raise in try propagates past and else is skipped).
    if(has_else)
    {
      code_blockt else_block;
      for(const auto &s : as_array(orelse))
        else_block.add(convert_statement(s));
      if(exc_sym != nullptr)
        block.add(code_ifthenelset{
          not_exprt{exc_sym->symbol_expr()}, std::move(else_block)});
      else
        block.add(std::move(else_block));
    }
  }

  // Execute finally block (always runs)
  const jsont &finalbody = json_member(stmt, "finalbody");
  if(finalbody.is_array())
  {
    for(const auto &s : as_array(finalbody))
      block.add(convert_statement(s));
  }

  return std::move(block);
}
