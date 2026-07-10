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
  if(
    current_function.empty() && try_depth == 0 && with_cleanup_depth == 0 &&
    !python_no_exception_checks)
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
  else if(try_depth > 0 || with_cleanup_depth > 0)
  {
    // PLR §8.4 / §8.5: raise inside a try body or a `with` body.
    // Do NOT return here — the enclosing convert_try / convert_with
    // guards each subsequent statement with !__exception_active so
    // the raise skips the rest of the body, and the except / finally
    // / __exit__ machinery takes over (a __exit__ may suppress the
    // exception). Emitting a return here would bypass that.
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

  // PLR §8.5: managers whose class declares __exit__, collected so the
  // __exit__ call can be emitted after the with-body (in reverse
  // order). Without this, a bug/assertion inside __exit__ is never
  // checked.
  std::vector<std::pair<exprt, std::string>> with_managers;

  const jsont &items = json_member(stmt, "items");
  if(items.is_array())
  {
    for(const auto &item : as_array(items))
    {
      const jsont &optional_vars = json_member(item, "optional_vars");
      const jsont &ctx_expr_pre = json_member(item, "context_expr");

      // PLR §8.5: 'with EXPR as v:' calls EXPR.__enter__().
      // If EXPR evaluates to None, the attribute lookup
      // for __enter__ raises AttributeError ('NoneType'
      // object has no attribute '__enter__'). Detect both
      // the literal-None form and any expression whose
      // value the recognizer identifies as None, and emit
      // the AttributeError property up front.
      bool ctx_is_none = false;
      if(is_node_type(ctx_expr_pre, "Constant"))
      {
        const jsont &v = json_member(ctx_expr_pre, "value");
        if(v.is_null())
          ctx_is_none = true;
      }
      else if(is_node_type(ctx_expr_pre, "Name"))
      {
        std::string nm = json_string(json_member(ctx_expr_pre, "id"));
        if(nm == "None")
          ctx_is_none = true;
        else
        {
          const symbolt *s = symbol_table.lookup(irep_idt{"python::" + nm});
          if(s != nullptr && is_python_none_constant(s->value))
            ctx_is_none = true;
        }
      }
      if(ctx_is_none)
      {
        const symbolt *exc_sym =
          symbol_table.lookup("python::__exception_active");
        const symbolt *exc_type_sym =
          symbol_table.lookup("python::__exception_type");
        if(exc_sym != nullptr)
        {
          block.add(
            code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
          if(exc_type_sym != nullptr)
          {
            long h = exception_type_hash("AttributeError");
            block.add(code_frontend_assignt{
              exc_type_sym->symbol_expr(),
              from_integer(h, exc_type_sym->type)});
          }
        }
        continue;
      }

      // PLR §8.5: the context-manager protocol requires BOTH __enter__ and
      // __exit__. A class that (across its MRO) lacks either does not support
      // the protocol -> TypeError. Same dunder-protocol-missing whole-group as
      // subscript / iteration / call.
      if(
        is_node_type(ctx_expr_pre, "Call") &&
        is_node_type(json_member(ctx_expr_pre, "func"), "Name"))
      {
        std::string cn =
          json_string(json_member(json_member(ctx_expr_pre, "func"), "id"));
        if(
          class_types.count(cn) && (!class_mro_defines(cn, "__enter__") ||
                                    !class_mro_defines(cn, "__exit__")))
        {
          const symbolt *exc_sym =
            symbol_table.lookup("python::__exception_active");
          const symbolt *exc_type_sym =
            symbol_table.lookup("python::__exception_type");
          if(exc_sym != nullptr)
          {
            block.add(
              code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
            if(exc_type_sym != nullptr)
              block.add(code_frontend_assignt{
                exc_type_sym->symbol_expr(),
                from_integer(
                  exception_type_hash("TypeError"), exc_type_sym->type)});
          }
          continue;
        }
      }

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

          // PLR §8.5: 'with CM() as v' is equivalent to:
          //     manager = CM()
          //     v = manager.__enter__()
          // The bound name receives __enter__'s return value, NOT
          // the manager itself. Look up __enter__ on the class and,
          // if it returns non-empty, use that return type as v's
          // type. Materialise a temp manager and call __init__,
          // then call __enter__ binding its result to v.
          irep_idt enter_id{"python::" + cls_name + "::__enter__"};
          const symbolt *enter_sym = symbol_table.lookup(enter_id);
          typet v_type = cls_type;
          if(enter_sym != nullptr)
          {
            const code_typet &et = to_code_type(enter_sym->type);
            if(et.return_type().id() != ID_empty)
              v_type = et.return_type();
          }

          // Manager temp (the context manager instance).
          static unsigned with_mgr_ctr = 0;
          std::string mgr_name = "__with_mgr_" + std::to_string(with_mgr_ctr++);
          std::string mgr_qname = qualify_name(mgr_name);
          irep_idt mgr_id{mgr_qname};
          if(symbol_table.lookup(mgr_id) == nullptr)
          {
            symbolt mgr_sym{mgr_id, cls_type, "python"};
            mgr_sym.base_name = mgr_name;
            mgr_sym.is_lvalue = true;
            mgr_sym.is_state_var = true;
            symbol_table.add(mgr_sym);
          }
          const symbolt &mgr = symbol_table.lookup_ref(mgr_id);

          // Record for the post-body __exit__ call (PLR §8.5).
          if(
            symbol_table.lookup("python::" + cls_name + "::__exit__") !=
            nullptr)
            with_managers.push_back({mgr.symbol_expr(), cls_name});

          // Bound variable v with v_type (= __enter__'s return type).
          if(symbol_table.lookup(sym_id) == nullptr)
          {
            symbolt new_sym{sym_id, v_type, "python"};
            new_sym.base_name = var_name;
            new_sym.is_lvalue = true;
            new_sym.is_state_var = true;
            symbol_table.add(new_sym);
          }

          // Construct the manager: __init__ call, or @dataclass field binding.
          for(auto &s : build_class_construction(
                cls_name, mgr.symbol_expr(), ctx_expr, loc))
            block.add(std::move(s));

          // Call __enter__ and bind result to v.
          if(enter_sym != nullptr)
          {
            const symbolt &v_sym = symbol_table.lookup_ref(sym_id);
            exprt::operandst eargs;
            eargs.push_back(address_of_exprt{mgr.symbol_expr()});
            const code_typet &et = to_code_type(enter_sym->type);
            if(et.return_type().id() == ID_empty)
            {
              // void __enter__: just call it, no binding.
              side_effect_expr_function_callt call{
                enter_sym->symbol_expr(), std::move(eargs), empty_typet{}, loc};
              block.add(code_expressiont{call});
            }
            else
            {
              side_effect_expr_function_callt call{
                enter_sym->symbol_expr(),
                std::move(eargs),
                et.return_type(),
                loc};
              exprt rhs = call;
              if(rhs.type() != v_sym.type)
                rhs = safe_typecast(rhs, v_sym.type);
              code_frontend_assignt assign{v_sym.symbol_expr(), std::move(rhs)};
              assign.add_source_location() = loc;
              block.add(std::move(assign));
            }
          }
        }
        else
        {
          // PLR §8.5: 'with EXPR as v' is equivalent to:
          //   manager = EXPR
          //   v = manager.__enter__()
          // Evaluate EXPR, then if its class has __enter__,
          // call it and bind v to the return value. Otherwise
          // fall back to v = EXPR (legacy behaviour for
          // expressions whose static type doesn't reveal a CM
          // protocol).
          exprt ctx = convert_expression(ctx_expr);
          if(!ctx.is_nil())
          {
            // Determine the class tag of ctx (if any) to look
            // up __enter__.
            std::string cls_name;
            typet ctx_class_type = ctx.type();
            if(ctx.type().id() == ID_struct)
            {
              std::string tag = id2string(to_struct_type(ctx.type()).get_tag());
              cls_name =
                tag.substr(0, 13) == "python_class_" ? tag.substr(13) : tag;
            }
            else if(ctx.type().id() == ID_struct_tag)
            {
              std::string tag =
                id2string(to_struct_tag_type(ctx.type()).get_identifier());
              if(tag.substr(0, 17) == "tag-python_class_")
                cls_name = tag.substr(17);
              else if(tag.substr(0, 4) == "tag-")
                cls_name = tag.substr(4);
            }
            irep_idt enter_id{
              cls_name.empty() ? std::string{}
                               : "python::" + cls_name + "::__enter__"};
            const symbolt *enter_sym =
              cls_name.empty() ? nullptr : symbol_table.lookup(enter_id);
            if(enter_sym != nullptr)
            {
              const code_typet &et = to_code_type(enter_sym->type);
              typet v_type = (et.return_type().id() == ID_empty)
                               ? ctx.type()
                               : et.return_type();
              if(symbol_table.lookup(sym_id) == nullptr)
              {
                symbolt new_sym{sym_id, v_type, "python"};
                new_sym.base_name = var_name;
                new_sym.is_lvalue = true;
                new_sym.is_state_var = true;
                symbol_table.add(new_sym);
              }
              // Materialise ctx into a temp so we can pass its
              // address to __enter__.
              static unsigned with_mgr_ctr2 = 0;
              std::string mgr_name =
                "__with_mgr_e_" + std::to_string(with_mgr_ctr2++);
              std::string mgr_qname = qualify_name(mgr_name);
              irep_idt mgr_id{mgr_qname};
              if(symbol_table.lookup(mgr_id) == nullptr)
              {
                symbolt mgr_sym{mgr_id, ctx.type(), "python"};
                mgr_sym.base_name = mgr_name;
                mgr_sym.is_lvalue = true;
                mgr_sym.is_state_var = true;
                symbol_table.add(mgr_sym);
              }
              const symbolt &mgr = symbol_table.lookup_ref(mgr_id);
              block.add(code_frontend_assignt{mgr.symbol_expr(), ctx});
              if(
                symbol_table.lookup("python::" + cls_name + "::__exit__") !=
                nullptr)
                with_managers.push_back({mgr.symbol_expr(), cls_name});
              const symbolt &v_sym = symbol_table.lookup_ref(sym_id);
              if(et.return_type().id() == ID_empty)
              {
                exprt::operandst eargs{address_of_exprt{mgr.symbol_expr()}};
                side_effect_expr_function_callt call{
                  enter_sym->symbol_expr(),
                  std::move(eargs),
                  empty_typet{},
                  loc};
                block.add(code_expressiont{call});
                // Without a return value, fall back to binding v
                // to the manager itself.
                block.add(code_frontend_assignt{
                  v_sym.symbol_expr(), mgr.symbol_expr()});
              }
              else
              {
                exprt::operandst eargs{address_of_exprt{mgr.symbol_expr()}};
                side_effect_expr_function_callt call{
                  enter_sym->symbol_expr(),
                  std::move(eargs),
                  et.return_type(),
                  loc};
                exprt rhs = call;
                if(rhs.type() != v_sym.type)
                  rhs = safe_typecast(rhs, v_sym.type);
                code_frontend_assignt assign{
                  v_sym.symbol_expr(), std::move(rhs)};
                assign.add_source_location() = loc;
                block.add(std::move(assign));
              }
            }
            else
            {
              // No __enter__ — fall back to the legacy
              // behaviour: v = expr.
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
      else
      {
        // PLR §8.5: no 'as' variable — `with EXPR:`. Create/evaluate
        // the manager, call __enter__ (result discarded), and collect
        // it so __exit__ runs (and may suppress) after the body.
        const jsont &ctx_expr = json_member(item, "context_expr");
        std::string cls_name;
        exprt mgr_expr = nil_exprt{};
        if(
          is_node_type(ctx_expr, "Call") &&
          is_node_type(json_member(ctx_expr, "func"), "Name") &&
          class_types.count(
            json_string(json_member(json_member(ctx_expr, "func"), "id"))))
        {
          cls_name =
            json_string(json_member(json_member(ctx_expr, "func"), "id"));
          static unsigned with_mgr_n = 0;
          std::string mn = "__with_mgr_n_" + std::to_string(with_mgr_n++);
          irep_idt mid{qualify_name(mn)};
          if(symbol_table.lookup(mid) == nullptr)
          {
            symbolt ms{mid, class_types[cls_name], "python"};
            ms.base_name = mn;
            ms.is_lvalue = true;
            ms.is_state_var = true;
            symbol_table.add(ms);
          }
          mgr_expr = symbol_table.lookup_ref(mid).symbol_expr();
          auto init_call =
            build_class_init_call(cls_name, mgr_expr, ctx_expr, loc);
          if(init_call)
            block.add(code_expressiont{*init_call});
        }
        else
        {
          exprt ctx = convert_expression(ctx_expr);
          if(ctx.type().id() == ID_struct)
          {
            std::string tag = id2string(to_struct_type(ctx.type()).get_tag());
            cls_name =
              tag.substr(0, 13) == "python_class_" ? tag.substr(13) : tag;
          }
          else if(ctx.type().id() == ID_struct_tag)
          {
            std::string tag =
              id2string(to_struct_tag_type(ctx.type()).get_identifier());
            if(tag.substr(0, 17) == "tag-python_class_")
              cls_name = tag.substr(17);
            else if(tag.substr(0, 4) == "tag-")
              cls_name = tag.substr(4);
          }
          if(!cls_name.empty() && !ctx.is_nil())
          {
            static unsigned with_mgr_ne = 0;
            std::string mn = "__with_mgr_ne_" + std::to_string(with_mgr_ne++);
            irep_idt mid{qualify_name(mn)};
            if(symbol_table.lookup(mid) == nullptr)
            {
              symbolt ms{mid, ctx.type(), "python"};
              ms.base_name = mn;
              ms.is_lvalue = true;
              ms.is_state_var = true;
              symbol_table.add(ms);
            }
            mgr_expr = symbol_table.lookup_ref(mid).symbol_expr();
            block.add(code_frontend_assignt{mgr_expr, ctx});
          }
        }
        if(!cls_name.empty() && !mgr_expr.is_nil())
        {
          const symbolt *enter_sym =
            symbol_table.lookup("python::" + cls_name + "::__enter__");
          if(enter_sym != nullptr)
          {
            const code_typet &et = to_code_type(enter_sym->type);
            side_effect_expr_function_callt call{
              enter_sym->symbol_expr(),
              {address_of_exprt{mgr_expr}},
              et.return_type().id() == ID_empty ? typet{empty_typet{}}
                                                : et.return_type(),
              loc};
            block.add(code_expressiont{std::move(call)});
          }
          if(
            symbol_table.lookup("python::" + cls_name + "::__exit__") !=
            nullptr)
            with_managers.push_back({mgr_expr, cls_name});
        }
      }
    }
  }

  // Flush any pending exception checks generated while evaluating the context
  // expression(s) / __enter__ (e.g. a ValueError from
  // `with CM(t.index(k)) as v:`) into the block BEFORE the body, so a raising
  // context expression is not silently dropped -- otherwise the body and the
  // rest of the program run unguarded and the exception is lost (a false proof
  // found by the mutation-oracle).
  for(auto &c : pending_checks)
    block.add(std::move(c));
  pending_checks.clear();

  // Convert the body. When a context manager declares __exit__ the
  // `with` behaves like try/finally: a raise/return/break/continue
  // must run __exit__ first (which may suppress an exception). Bump
  // with_cleanup_depth so a `raise` defers instead of early-returning,
  // and guard each statement after the first with !__exception_active
  // so a raise skips the rest of the body (mirrors convert_try).
  const symbolt *exc_active_sym =
    symbol_table.lookup("python::__exception_active");
  const bool has_cleanup = !with_managers.empty() && exc_active_sym != nullptr;
  const jsont &body = json_member(stmt, "body");
  if(has_cleanup)
    with_cleanup_depth++;
  if(body.is_array())
  {
    bool first = true;
    for(const auto &s : as_array(body))
    {
      codet sc = convert_statement(s);
      if(!first && has_cleanup)
        block.add(code_ifthenelset{
          not_exprt{exc_active_sym->symbol_expr()}, std::move(sc)});
      else
        block.add(std::move(sc));
      first = false;
    }
  }
  if(has_cleanup)
    with_cleanup_depth--;

  // PLR §8.5: build the __exit__(self, exc_type, exc_val, exc_tb)
  // sequence (reverse order of entry). It (a) checks any assertion /
  // bug inside __exit__ and (b) models exception suppression: if an
  // exception is active when __exit__ runs and __exit__ returns a
  // truthy value, the exception is swallowed ("If the suite was exited
  // due to an exception ... and the __exit__() return value was false,
  // the exception is reraised").
  //
  // Soundness: the exc-info arguments are nondet (not None), so an
  // __exit__ whose suppression decision depends on the exception type
  // explores BOTH the suppress and the propagate branch — the
  // propagate branch preserves any uncaught-exception failure, so no
  // bug is missed. A __exit__ that returns a constant (the common
  // case) is unaffected by the nondet args and modeled exactly.
  code_blockt exit_code;
  for(auto it = with_managers.rbegin(); it != with_managers.rend(); ++it)
  {
    const exprt &mgr_expr = it->first;
    const std::string &cls_name = it->second;
    const symbolt *exit_sym =
      symbol_table.lookup("python::" + cls_name + "::__exit__");
    if(exit_sym == nullptr)
      continue;
    const code_typet &xt = to_code_type(exit_sym->type);
    exprt::operandst xargs;
    xargs.push_back(address_of_exprt{mgr_expr});
    for(std::size_t i = xargs.size(); i < xt.parameters().size(); ++i)
      xargs.push_back(side_effect_expr_nondett{xt.parameters()[i].type(), loc});
    const bool has_ret = xt.return_type().id() != ID_empty;
    side_effect_expr_function_callt xcall{
      exit_sym->symbol_expr(),
      std::move(xargs),
      has_ret ? xt.return_type() : typet{empty_typet{}},
      loc};
    if(has_ret && exc_active_sym != nullptr)
    {
      // was_active = __exception_active (snapshot before __exit__);
      // r = __exit__(...); if(was_active && truthy(r)) clear exception.
      static unsigned exit_ret_ctr = 0;
      std::string rn = "__with_exit_ret_" + std::to_string(exit_ret_ctr);
      std::string wn = "__with_exc_was_" + std::to_string(exit_ret_ctr++);
      irep_idt rid{qualify_name(rn)};
      irep_idt wid{qualify_name(wn)};
      if(symbol_table.lookup(rid) == nullptr)
      {
        symbolt rs{rid, xt.return_type(), "python"};
        rs.base_name = rn;
        rs.is_lvalue = true;
        rs.is_state_var = true;
        symbol_table.add(rs);
      }
      if(symbol_table.lookup(wid) == nullptr)
      {
        symbolt ws{wid, bool_typet{}, "python"};
        ws.base_name = wn;
        ws.is_lvalue = true;
        ws.is_state_var = true;
        symbol_table.add(ws);
      }
      exprt ret_expr = symbol_table.lookup_ref(rid).symbol_expr();
      exprt was_expr = symbol_table.lookup_ref(wid).symbol_expr();
      exit_code.add(
        code_frontend_assignt{was_expr, exc_active_sym->symbol_expr()});
      exit_code.add(code_frontend_assignt{ret_expr, xcall});
      exit_code.add(code_ifthenelset{
        and_exprt{was_expr, python_truthiness(ret_expr)},
        code_frontend_assignt{exc_active_sym->symbol_expr(), false_exprt{}}});
    }
    else
      exit_code.add(code_expressiont{std::move(xcall)});
  }

  // PLR §8.5: __exit__ runs before any control-flow exit from the
  // body. Inline a copy before each return/break/continue (like the
  // finally lowering in convert_try), then append for the
  // normal-fallthrough and raise paths.
  std::function<void(codet &)> inline_exit = [&](codet &c) -> void
  {
    for(auto &op : c.operands())
    {
      if(op.id() != ID_code)
        continue;
      codet &inner = static_cast<codet &>(op);
      const irep_idt &st = inner.get_statement();
      if(st == ID_return || st == ID_break || st == ID_continue)
      {
        code_blockt blk;
        blk.append(exit_code);
        blk.add(static_cast<const codet &>(inner));
        op = std::move(blk);
      }
      else
        inline_exit(inner);
    }
  };
  if(!exit_code.statements().empty())
    inline_exit(block);
  block.append(exit_code);

  // If the exception was not suppressed and there is no enclosing try,
  // propagate it past the `with`: early-return inside a function, or
  // the uncaught-exception assertion at module top level (deferred
  // from the raise, which skipped it under with_cleanup_depth).
  if(has_cleanup && try_depth == 0)
  {
    code_blockt prop;
    if(current_function.empty())
    {
      if(!python_no_exception_checks)
      {
        source_locationt aloc = loc;
        aloc.set_property_class("exception");
        aloc.set_comment("uncaught exception");
        prop.add(code_assertt{false_exprt{}});
        prop.add(code_assumet{false_exprt{}});
      }
    }
    else
    {
      const symbolt *func_sym =
        symbol_table.lookup("python::" + current_function);
      if(func_sym != nullptr)
      {
        const typet &rt = to_code_type(func_sym->type).return_type();
        if(rt.id() == ID_empty)
          prop.add(code_frontend_returnt{});
        else
          prop.add(code_frontend_returnt{
            side_effect_expr_nondett{rt, source_locationt{}}});
      }
    }
    if(!prop.statements().empty())
      block.add(
        code_ifthenelset{exc_active_sym->symbol_expr(), std::move(prop)});
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
  // PLR §8.4 control-flow correctness: snapshot tracking before
  // try body, capture state after try, and for each except
  // handler restore-snapshot-then-process so handlers see the
  // pre-try state. Merge all post-states (try-success + each
  // except handler) at the end.
  tracking_snapshott pre_try_tracking = snapshot_tracking();
  std::vector<tracking_snapshott> arm_states;
  if(body.is_array())
  {
    bool first = true;
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
  arm_states.push_back(snapshot_tracking());
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
        // Restore snapshot so this handler sees pre-try state, not
        // the leaked state from the try body or previous handlers.
        restore_tracking(pre_try_tracking);
        if_else_depth++;
        for(const auto &s : as_array(handler_body))
          except_block.add(convert_statement(s));
        if_else_depth--;
        arm_states.push_back(snapshot_tracking());
      }

      exprt condition = exc_sym->symbol_expr();
      if(
        !handler_type.is_null() && is_node_type(handler_type, "Name") &&
        exc_type_sym != nullptr)
      {
        std::string htype = json_string(json_member(handler_type, "id"));
        if(htype != "Exception" && htype != "BaseException" && !htype.empty())
        {
          // PLR §8.4: handler matches the named class and any
          // of its subclasses. Build the set of matching
          // hashes from a builtin-hierarchy table plus any
          // user-defined classes whose MRO includes htype.
          static const std::map<std::string, std::vector<std::string>>
            builtin_subclasses = {
              {"OSError",
               {"OSError", "FileNotFoundError", "IOError", "EOFError"}},
              {"IOError",
               {"IOError", "OSError", "FileNotFoundError", "EOFError"}},
              {"ArithmeticError", {"ArithmeticError", "ZeroDivisionError"}},
              {"LookupError", {"LookupError", "KeyError", "IndexError"}},
              {"ValueError", {"ValueError", "UnicodeError"}},
              {"RuntimeError", {"RuntimeError", "NotImplementedError"}},
            };
          std::vector<std::string> match_types{htype};
          auto sit = builtin_subclasses.find(htype);
          if(sit != builtin_subclasses.end())
            match_types = sit->second;
          // User-defined exception classes whose MRO includes
          // htype: register them as matching too.
          for(const auto &p : class_mro)
            for(const auto &mro_cls : p.second)
              if(mro_cls == htype)
              {
                match_types.push_back(p.first);
                break;
              }
          exprt any_match = false_exprt{};
          for(const auto &mt : match_types)
          {
            long type_hash = exception_type_hash(mt);
            any_match = or_exprt{
              any_match,
              equal_exprt{
                exc_type_sym->symbol_expr(),
                from_integer(type_hash, python_int_type())}};
          }
          condition = and_exprt{condition, any_match};
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
  if(finalbody.is_array() && !as_array(finalbody).empty())
  {
    // Convert the finally body once into a reusable block.
    code_blockt finally_code;
    for(const auto &s : as_array(finalbody))
      finally_code.add(convert_statement(s));

    // PLR §8.4.2: a return / break / continue leaving the try (or
    // an except handler) must run the finally FIRST, and if the
    // finally itself performs a return / break / continue that
    // action REPLACES the pending one. Model this by inlining the
    // finally body immediately before each such control-flow exit
    // in the try/handler code built so far: `{ <finally>; return X }`.
    // If the finally returns, its return executes and the trailing
    // `return X` is dead — exactly Python's override semantics.
    // The plain append below still covers the normal-fallthrough
    // and exception-propagation paths (which the inlined copies do
    // not reach, since a return/break/continue exits first).
    std::function<void(codet &)> inline_finally = [&](codet &c) -> void
    {
      for(auto &op : c.operands())
      {
        if(op.id() != ID_code)
          continue;
        codet &inner = static_cast<codet &>(op);
        const irep_idt &st = inner.get_statement();
        if(st == ID_return || st == ID_break || st == ID_continue)
        {
          code_blockt blk;
          blk.append(finally_code); // copy
          blk.add(static_cast<const codet &>(inner));
          op = std::move(blk);
        }
        else
          inline_finally(inner);
      }
    };
    inline_finally(block);

    block.append(finally_code); // fallthrough / exception path
  }

  // Merge arm states: try-success (arm_states[0]) plus each
  // except-handler post-state (arm_states[1..]). Path-dependent
  // entries (assigned inconsistently across arms) are dropped.
  if(arm_states.empty())
  {
    // No body and no handlers — nothing to merge; restore.
    restore_tracking(pre_try_tracking);
  }
  else if(arm_states.size() == 1)
  {
    // Only the try body executed. Path-dependent entries are
    // those that differ from pre-try — merge with snapshot.
    merge_tracking(arm_states[0], pre_try_tracking);
  }
  else
  {
    restore_tracking(arm_states[0]);
    for(std::size_t i = 1; i < arm_states.size(); i++)
    {
      tracking_snapshott current = snapshot_tracking();
      merge_tracking(current, arm_states[i]);
    }
  }

  return std::move(block);
}
