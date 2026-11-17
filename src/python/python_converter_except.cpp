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
#include <util/expr_util.h>
#include <util/json.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/symbol.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_exception_instrumentation.h"
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
  // shouldn't see 'raise' as a property violation. (Inside functions,
  // sequence_exceptions reports a raise that no handler can catch.)
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
  // Inside a function but outside any try block: nothing more. The flag is
  // set; sequence_exceptions guards the rest of the function, which falls
  // through to its end, where the exception leaves the function (an early
  // return here gave every raise site its own path to the function end).
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
        // PLR §8.5: the `as v` variable is (re)assigned to __enter__'s result,
        // so invalidate its tracking -- else `b = 1; with CM(3) as b: pass;
        // t[b]` folds t[b] on the stale b=1 (found by probing the reassignment
        // whole-group).
        invalidate_reassigned_symbol(sym_id);

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
          // By-reference manager: dereference before tag sniffing
          // (see the non-as site).
          if(
            !ctx.is_nil() && ctx.type().id() == ID_pointer &&
            to_pointer_type(ctx.type()).base_type().id() != ID_empty)
            ctx = dereference_exprt{ctx};
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
              // PLR 3.1: an LVALUE context expression binds by
              // reference (see the non-as site); only rvalues get a
              // materializing temp.
              exprt mgr_lv;
              if(
                ctx.id() == ID_symbol || ctx.id() == ID_member ||
                ctx.id() == ID_dereference || ctx.id() == ID_index)
              {
                mgr_lv = ctx;
              }
              else
              {
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
                mgr_lv = symbol_table.lookup_ref(mgr_id).symbol_expr();
                block.add(code_frontend_assignt{mgr_lv, ctx});
              }
              if(
                symbol_table.lookup("python::" + cls_name + "::__exit__") !=
                nullptr)
                with_managers.push_back({mgr_lv, cls_name});
              const symbolt &v_sym = symbol_table.lookup_ref(sym_id);
              if(et.return_type().id() == ID_empty)
              {
                exprt::operandst eargs{address_of_exprt{mgr_lv}};
                side_effect_expr_function_callt call{
                  enter_sym->symbol_expr(),
                  std::move(eargs),
                  empty_typet{},
                  loc};
                block.add(code_expressiont{call});
                // Without a return value, fall back to binding v
                // to the manager itself.
                block.add(code_frontend_assignt{v_sym.symbol_expr(), mgr_lv});
              }
              else
              {
                exprt::operandst eargs{address_of_exprt{mgr_lv}};
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
          // A by-reference manager (a function parameter, self):
          // pointer-to-struct. Dereference so the tag sniffing below
          // sees the class -- previously the protocol was SILENTLY
          // DROPPED for these (no __enter__/__exit__ at all: a
          // missed-mutation false proof, and __exit__-on-return
          // never ran).
          if(
            ctx.type().id() == ID_pointer &&
            to_pointer_type(ctx.type()).base_type().id() != ID_empty)
            ctx = dereference_exprt{ctx};
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
            // PLR 3.1 (the instance-reference-semantics plan's
            // context-manager arm): `with span:` runs __enter__/
            // __exit__ on THE OBJECT span names -- copying it into
            // a temp made the dunders mutate the copy, so
            // `span.depth == 1` inside the body false-alarmed. An
            // LVALUE context expression binds by reference (exactly
            // like a method receiver); only an RVALUE (a call
            // result already handled above, a ternary) needs the
            // materializing temp, and a fresh rvalue has no
            // aliasing observer.
            if(
              ctx.id() == ID_symbol || ctx.id() == ID_member ||
              ctx.id() == ID_dereference || ctx.id() == ID_index)
            {
              // symbol / field / by-ref parameter (a dereference) /
              // container element -- all lvalues.
              mgr_expr = ctx;
            }
            else
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
  // PLR 8.5: only a truthy __exit__ result suppresses the exception. If
  // no manager's __exit__ can return a truthy value, an exception leaving
  // the body behaves as if uncaught by this with statement.
  bool may_suppress = false;
  for(const auto &m : with_managers)
    if(exit_may_suppress(m.second))
      may_suppress = true;
  if(has_cleanup)
    with_cleanup_depth++;
  if(has_cleanup && may_suppress)
    with_suppress_depth++;
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
  if(has_cleanup && may_suppress)
    with_suppress_depth--;

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
  // PLR 8.5: __exit__ runs when control LEAVES THE WITH SUITE. A
  // `return` leaves it from any depth; a `break`/`continue` leaves
  // it only when its binding loop encloses the `with` -- one nested
  // inside a loop WITHIN the with body transfers control inside the
  // suite and must NOT run __exit__ (prepending it there
  // over-decremented `with span:` depth counters on every loop
  // continue/break: span.depth == 0 after the block false-alarmed,
  // pyhard's reconciliation asserts). Track relative loop depth and
  // prepend for break/continue only at depth 0.
  std::function<void(codet &, unsigned)> inline_exit =
    [&](codet &c, unsigned loop_depth) -> void
  {
    for(auto &op : c.operands())
    {
      if(op.id() != ID_code)
        continue;
      codet &inner = static_cast<codet &>(op);
      const irep_idt &st = inner.get_statement();
      if(
        st == ID_return ||
        ((st == ID_break || st == ID_continue) && loop_depth == 0))
      {
        code_blockt blk;
        blk.append(exit_code);
        blk.add(static_cast<const codet &>(inner));
        op = std::move(blk);
      }
      else if(st == ID_while || st == ID_for || st == ID_dowhile)
        inline_exit(inner, loop_depth + 1);
      else
        inline_exit(inner, loop_depth);
    }
  };
  if(!exit_code.statements().empty())
    inline_exit(block, 0);
  // __exit__ runs whether or not an exception is active (see
  // sequence_exceptions).
  exit_code.set(ID_python_exc_opaque, true);
  block.add(std::move(exit_code));

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

  // PLR 8.5: an exception leaving the body runs __exit__ and, unless it is
  // suppressed, propagates. When no manager can suppress it and no
  // __exit__ contains a check or a raise, running __exit__ cannot change
  // any verdict once the exception is uncaught, so the with statement is
  // transparent for uncaught-exception lowering (see
  // lower_uncatchable_raise).
  bool transparent = !may_suppress;
  for(const auto &m : with_managers)
  {
    const symbolt *xs =
      symbol_table.lookup("python::" + m.second + "::__exit__");
    if(
      xs == nullptr || xs->value.id() != ID_code ||
      has_subexpr(
        xs->value,
        [](const exprt &e)
        {
          if(e.id() != ID_code)
            return false;
          const irep_idt &st = to_code(e).get_statement();
          if(st == ID_assert || st == ID_function_call)
            return true;
          return st == ID_assign && e.operands()[0].id() == ID_symbol &&
                 to_symbol_expr(e.operands()[0]).get_identifier() ==
                   "python::__exception_active";
        }) ||
      has_subexpr(
        xs->value,
        [](const exprt &e)
        {
          return e.id() == ID_side_effect &&
                 e.get(ID_statement) == ID_function_call;
        }))
      transparent = false;
  }
  if(!transparent)
    block.set(ID_python_exc_try, true);
  return std::move(block);
}

// --- Module body conversion ---

// PLR §8.4: The try statement
// PLR §3.2: "Exceptions are identified by class instances."
// We model exceptions via __exception_active (bool) and
// __exception_type (hash of exception class name).
// "The try statement specifies exception handlers and/or cleanup code
// for a group of statements."
bool python_convertert::exit_may_suppress(const std::string &cls_name) const
{
  // Look for the class's (or an ancestor's) converted __exit__ body. An
  // unknown manager may suppress.
  std::vector<std::string> work{cls_name};
  std::set<std::string> seen;
  while(!work.empty())
  {
    const std::string c = work.back();
    work.pop_back();
    if(!seen.insert(c).second)
      continue;
    const symbolt *fs = symbol_table.lookup("python::" + c + "::__exit__");
    if(fs != nullptr)
    {
      if(fs->value.id() != ID_code)
        return true;
      bool truthy_possible = false;
      std::function<void(const exprt &)> walk = [&](const exprt &e)
      {
        if(truthy_possible)
          return;
        if(e.id() == ID_code && to_code(e).get_statement() == ID_return)
        {
          const auto &r = to_code_frontend_return(to_code(e));
          if(!r.has_return_value())
            return;
          const exprt &v = r.return_value();
          // A constant falsy value (False, 0, None sentinel) cannot suppress.
          if(v.is_constant())
          {
            if(v.is_false() || v.is_zero())
              return;
            if(
              v.type().id() == ID_signedbv &&
              numeric_cast<mp_integer>(v) ==
                mp_integer{python_none_sentinel_int()})
              return;
          }
          truthy_possible = true;
          return;
        }
        for(const auto &op : e.operands())
          walk(op);
      };
      walk(fs->value);
      return truthy_possible;
    }
    auto bit = class_bases.find(c);
    if(bit == class_bases.end())
      return true;
    for(const auto &b : bit->second)
      work.push_back(b);
  }
  return true;
}

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

    // PLR §8.4: else runs iff no exception was raised in the try
    // body. Encode it as the ELSE ARM of the handler dispatch --
    // `if exc: handler_chain else: else_body` -- rather than
    // snapshotting the pre-handler flag into a separate boolean:
    // the snapshot copy kept two exception-state lineages alive per
    // loop iteration and defeated symex constant propagation (a
    // measured 6x SSA blowup and a solver-phase wall on the
    // retry-loop benchmark). A raise inside the else body is NOT
    // caught by this try's handlers in either encoding (the body
    // sets __exception_active and the per-statement weave
    // propagates it).
    if(has_else && exc_sym != nullptr)
    {
      code_blockt else_block;
      for(const auto &s : as_array(orelse))
        else_block.add(convert_statement(s));
      code_ifthenelset dispatch{
        exc_sym->symbol_expr(),
        std::move(handler_chain),
        std::move(else_block)};
      // Handler dispatch runs while the exception is active (see
      // sequence_exceptions); its bodies are sequenced normally.
      dispatch.set(ID_python_exc_observer, true);
      block.add(std::move(dispatch));
    }
    else
    {
      handler_chain.set(ID_python_exc_observer, true);
      block.add(std::move(handler_chain));
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

    // Fallthrough / exception path. The finally body runs whether or not an
    // exception is active, so sequence_exceptions neither guards nor enters
    // it.
    code_blockt fin = finally_code;
    fin.set(ID_python_exc_opaque, true);
    block.add(std::move(fin));
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

  // PLR §8.4 / §8.4.2: both the finally body (runs on ALL paths, LAST) and the
  // else clause (runs on the no-exception path) apply tracking updates that the
  // arm-merge above DISCARDS -- the merge reflects only the try/except
  // post-states. So a stale constant survived (`b=4; try: ... finally: b=len(s);
  // range(b)` folded range() on b=4; `b=1; try: ... else: b=9; t[b]` folded t[b]
  // on b=1 -- both false proofs found by the mutation-oracle). Clear the
  // constant/literal tracking for every name they (re)assign, so later reads use
  // the runtime value.
  {
    std::function<void(const jsont &)> clear_assigned = [&](const jsont &node)
    {
      if(!node.is_object() && !node.is_array())
        return;
      if(node.is_array())
      {
        for(const auto &e : as_array(node))
          clear_assigned(e);
        return;
      }
      auto clear_target = [&](const jsont &tgt)
      {
        std::function<void(const jsont &)> ct = [&](const jsont &t)
        {
          if(is_node_type(t, "Name"))
          {
            const irep_idt id{qualify_name(json_string(json_member(t, "id")))};
            invalidate_reassigned_symbol(id);
          }
          else if(
            (is_node_type(t, "Tuple") || is_node_type(t, "List")) &&
            json_member(t, "elts").is_array())
            for(const auto &e : as_array(json_member(t, "elts")))
              ct(e);
          else if(is_node_type(t, "Starred"))
            ct(json_member(t, "value"));
        };
        ct(tgt);
      };
      if(
        is_node_type(node, "Assign") && json_member(node, "targets").is_array())
        for(const auto &tg : as_array(json_member(node, "targets")))
          clear_target(tg);
      else if(
        is_node_type(node, "AnnAssign") || is_node_type(node, "AugAssign"))
        clear_target(json_member(node, "target"));
      // Recurse into nested compound statements (if/for/while/with/try).
      for(const char *child : {"body", "orelse", "finalbody", "handlers"})
      {
        const jsont &c = json_member(node, child);
        if(c.is_array())
          for(const auto &e : as_array(c))
            clear_assigned(e);
      }
    };
    if(orelse.is_array())
      for(const auto &s : as_array(orelse))
        clear_assigned(s);
    if(finalbody.is_array())
      for(const auto &s : as_array(finalbody))
        clear_assigned(s);
  }

  block.set(ID_python_exc_try, true);
  return std::move(block);
}

bool python_convertert::code_may_raise(const exprt &e) const
{
  auto callee_may_raise = [this](const exprt &f)
  {
    // Only Python code sets the flag; a callee whose body cannot set it
    // (directly or through its callees) leaves it unchanged.
    if(f.id() != ID_symbol)
      return true;
    const irep_idt &id = to_symbol_expr(f).get_identifier();
    const symbolt *fs = symbol_table.lookup(id);
    if(fs == nullptr || fs->value.id() != ID_code)
      return false; // no body: nondet result, flag untouched
    return !raising_functions_known || raising_functions.count(id) > 0;
  };
  if(e.id() == ID_side_effect && e.get(ID_statement) == ID_function_call)
    return callee_may_raise(to_side_effect_expr_function_call(e).function());
  if(e.id() == ID_code)
  {
    const codet &c = to_code(e);
    const irep_idt &st = c.get_statement();
    if(st == ID_function_call)
      return c.operands().size() >= 2 ? callee_may_raise(c.operands()[1])
                                      : true;
    if(st == ID_assign && c.operands().size() == 2)
    {
      const exprt &lhs = c.op0();
      if(
        lhs.id() == ID_symbol &&
        to_symbol_expr(lhs).get_identifier() == "python::__exception_active" &&
        !c.op1().is_false())
        return true;
    }
  }
  for(const auto &op : e.operands())
    if(code_may_raise(op))
      return true;
  return false;
}

/// True when \p c (an assignment, or an if/block of them) assigns only the
/// exception record (python::__exception_*) and calls nothing: such code is
/// part of raising, so it is never skipped as "after the raise".
static bool only_writes_exception_record(const codet &c)
{
  bool saw_assign = false;
  bool other = false;
  std::function<void(const exprt &)> walk = [&](const exprt &e)
  {
    if(other)
      return;
    if(e.id() == ID_side_effect && e.get(ID_statement) == ID_function_call)
    {
      other = true;
      return;
    }
    if(e.id() == ID_code)
    {
      const codet &k = to_code(e);
      const irep_idt &st = k.get_statement();
      if(st == ID_assign)
      {
        if(
          k.op0().id() == ID_symbol &&
          id2string(to_symbol_expr(k.op0()).get_identifier())
              .rfind("python::__exception_", 0) == 0)
        {
          saw_assign = true;
          return;
        }
        other = true;
        return;
      }
      if(st != ID_block && st != ID_ifthenelse && st != ID_skip)
      {
        other = true;
        return;
      }
    }
    for(const auto &op : e.operands())
      walk(op);
  };
  walk(c);
  return saw_assign && !other;
}

/// If \p e is `__exception_type := h` (possibly `c ? h : type`), return h.
static std::optional<mp_integer> exception_type_written(const exprt &e)
{
  if(
    e.id() != ID_code || to_code(e).get_statement() != ID_assign ||
    e.operands()[0].id() != ID_symbol ||
    to_symbol_expr(e.operands()[0]).get_identifier() !=
      "python::__exception_type")
    return {};
  const exprt *v = &e.operands()[1];
  if(v->id() == ID_if)
    v = &to_if_expr(*v).true_case();
  if(!v->is_constant())
    return {};
  return numeric_cast<mp_integer>(*v);
}

/// The exception type a raise at stmts[k] records: in the statement itself,
/// else in the record writes that follow it.
static std::optional<mp_integer>
find_raised_type(const code_blockt::code_operandst &stmts, std::size_t k)
{
  std::optional<mp_integer> found;
  std::function<void(const exprt &)> walk = [&](const exprt &e)
  {
    if(found.has_value())
      return;
    if(auto h = exception_type_written(e))
    {
      found = h;
      return;
    }
    for(const auto &op : e.operands())
      walk(op);
  };
  walk(stmts[k]);
  for(std::size_t j = k + 1;
      !found.has_value() && j < stmts.size() && j <= k + 3;
      ++j)
    walk(stmts[j]);
  return found;
}

void python_convertert::lower_uncatchable_raise(
  code_blockt::code_operandst &stmts,
  std::size_t k)
{
  const symbolt *exc_sym = symbol_table.lookup("python::__exception_active");
  if(exc_sym == nullptr)
    return;
  const symbol_exprt exc = exc_sym->symbol_expr();
  codet &s = stmts[k];
  auto is_exc = [&](const exprt &e)
  {
    return e.id() == ID_symbol &&
           to_symbol_expr(e).get_identifier() == "python::__exception_active";
  };
  // The condition under which s raises; nil when s is not a raise.
  auto assign_cond = [&](const codet &a) -> exprt
  {
    if(a.get_statement() != ID_assign || !is_exc(a.operands()[0]))
      return nil_exprt{};
    const exprt &rhs = a.operands()[1];
    if(rhs.is_true())
      return true_exprt{};
    if(
      rhs.id() == ID_or && rhs.operands().size() == 2 &&
      is_exc(rhs.operands()[0]))
      return rhs.operands()[1];
    return nil_exprt{};
  };
  exprt cond = nil_exprt{};
  if(s.get_statement() == ID_assign)
    cond = assign_cond(s);
  else if(
    s.get_statement() == ID_ifthenelse &&
    to_code_ifthenelse(s).else_case().is_nil())
  {
    const codet &t = to_code_ifthenelse(s).then_case();
    const codet *first = &t;
    if(t.get_statement() == ID_block && !to_code_block(t).statements().empty())
      first = &to_code_block(t).statements().front();
    if(assign_cond(*first).is_true())
      cond = to_code_ifthenelse(s).cond();
  }
  if(cond.is_nil())
    return;
  const auto h = find_raised_type(stmts, k);
  if(!h.has_value())
    return;
  static const std::vector<std::string> builtin_names = {
    "Exception",           "BaseException",     "ValueError",
    "TypeError",           "KeyError",          "IndexError",
    "AttributeError",      "ZeroDivisionError", "OverflowError",
    "NameError",           "UnboundLocalError", "RuntimeError",
    "NotImplementedError", "RecursionError",    "AssertionError",
    "StopIteration",       "OSError",           "IOError",
    "FileNotFoundError",   "EOFError",          "ImportError",
    "ModuleNotFoundError", "LookupError",       "ArithmeticError",
    "UnicodeError",        "MemoryError",       "FloatingPointError",
    "SystemExit",          "KeyboardInterrupt", "GeneratorExit"};
  std::vector<std::string> names;
  for(const auto &n : builtin_names)
    if(mp_integer{exception_type_hash(n)} == *h)
      names.push_back(n);
  for(const auto &p : class_types)
    if(mp_integer{exception_type_hash(p.first)} == *h)
      names.push_back(p.first);
  if(names.empty())
    return;
  for(const auto &n : names)
  {
    // Iteration protocol and generator machinery stay on the flag.
    if(n == "StopIteration" || n == "GeneratorExit")
      return;
    if(exc_seq_function.empty())
      continue; // module level, outside try: uncaught
    const std::size_t sep = exc_seq_function.rfind("::");
    const std::string leaf = sep == std::string::npos
                               ? exc_seq_function
                               : exc_seq_function.substr(sep + 2);
    const std::string root =
      exc_seq_function.substr(0, exc_seq_function.find("::"));
    for(const std::string &f : {exc_seq_function, root, leaf})
    {
      auto it = function_protected_exceptions.find(f);
      if(it != function_protected_exceptions.end())
        for(const auto &hn : it->second)
          if(handler_catches_exception(hn, n))
            return;
    }
  }
  // PLR 8.4: uncaught, the exception ends the program. Report it here and
  // end the path. Only the first exception on a path counts.
  // Without a location the property could not be attributed; keep the flag
  // (the uncaught-exception check after the module statement reports it).
  if(s.source_location().get_file().empty())
    return;
  const exprt raised = and_exprt{cond, not_exprt{exc}};
  source_locationt loc = s.source_location();
  code_blockt rep;
  if(!python_no_exception_checks && !function_entry_mode)
  {
    loc.set_property_class("exception");
    loc.set_comment("uncaught exception");
    code_assertt a{not_exprt{raised}};
    a.add_source_location() = loc;
    rep.add(std::move(a));
  }
  code_assumet as{not_exprt{raised}};
  as.add_source_location() = loc;
  rep.add(std::move(as));
  s = std::move(rep);
}

void python_convertert::tag_raising_calls(codet &s)
{
  // Calls in this statement's own expressions (not in nested statements,
  // which were tagged when their blocks were sequenced) whose callee may
  // raise get the statement's tag; a SKIP with the same tag ends the
  // statement (see instrument_python_call_exceptions).
  const irep_idt tag{std::to_string(++exc_stmt_tag_counter)};
  bool any = false;
  std::function<void(exprt &, bool)> walk = [&](exprt &e, bool in_code)
  {
    if(e.id() == ID_code && in_code)
      return; // a nested statement: handled at its own level
    if(e.id() == ID_side_effect && e.get(ID_statement) == ID_function_call)
    {
      const exprt &f = to_side_effect_expr_function_call(e).function();
      bool raises = true;
      if(f.id() == ID_symbol)
      {
        const irep_idt &id = to_symbol_expr(f).get_identifier();
        const symbolt *fs = symbol_table.lookup(id);
        raises = fs != nullptr && fs->value.id() == ID_code &&
                 (!raising_functions_known || raising_functions.count(id) > 0);
      }
      if(raises && e.source_location().get(ID_python_exc_stmt).empty())
      {
        e.add_source_location().set(ID_python_exc_stmt, tag);
        // goto-convert takes the FUNCTION_CALL's location from the callee
        // expression (do_function_call_symbol), so tag that occurrence too.
        to_side_effect_expr_function_call(e)
          .function()
          .add_source_location()
          .set(ID_python_exc_stmt, tag);
        any = true;
      }
    }
    for(auto &op : e.operands())
      walk(op, true);
  };
  const irep_idt &st = s.get_statement();
  if(st == ID_block || st == ID_decl || st == ID_dead)
    return;
  for(auto &op : s.operands())
    walk(op, op.id() == ID_code);
  if(!any)
    return;
  code_skipt end;
  source_locationt loc = s.source_location();
  loc.set(ID_python_exc_stmt, tag);
  end.add_source_location() = loc;
  code_blockt b;
  b.add(std::move(s));
  b.add(std::move(end));
  s = std::move(b);
}

void python_convertert::sequence_exceptions(codet &code)
{
  const symbolt *exc_sym = symbol_table.lookup("python::__exception_active");
  if(exc_sym == nullptr || code.get_bool(ID_python_exc_opaque))
    return;
  const symbol_exprt exc = exc_sym->symbol_expr();
  auto mentions_exc = [&](const exprt &e)
  {
    return has_subexpr(
      e,
      [](const exprt &n)
      {
        return n.id() == ID_symbol && to_symbol_expr(n).get_identifier() ==
                                        "python::__exception_active";
      });
  };
  auto as_block = [](codet &c) -> code_blockt &
  {
    if(c.get_statement() != ID_block)
    {
      code_blockt b;
      b.add(std::move(c));
      c = std::move(b);
    }
    return to_code_block(c);
  };
  const irep_idt &st = code.get_statement();
  const bool is_try = code.get_bool(ID_python_exc_try);
  if(is_try)
    ++exc_seq_trys;
  // Handler dispatch is part of the try lowering, but an exception raised
  // in a handler (or the else clause) is not caught by that try (PLR 8.4).
  const bool leaves_try =
    !is_try && exc_seq_trys > 0 && code.get_bool(ID_python_exc_observer);
  if(leaves_try)
    --exc_seq_trys;
  if(st == ID_block)
  {
    auto &stmts = to_code_block(code).statements();
    bool may_have_raised = false;
    for(std::size_t k = 0; k < stmts.size(); ++k)
    {
      if(exc_seq_trys == 0)
        lower_uncatchable_raise(stmts, k);
      tag_raising_calls(stmts[k]);
      codet &s = stmts[k];
      sequence_exceptions(s);
      const irep_idt &sst = s.get_statement();
      // Writes to the exception record itself (type, payload, ...) are part
      // of raising and are never guarded.
      const bool exc_record_write = only_writes_exception_record(s);
      const bool wrappable =
        sst != ID_decl && sst != ID_dead && sst != ID_label && sst != ID_skip &&
        !exc_record_write && !s.get_bool(ID_python_exc_observer) &&
        !s.get_bool(ID_python_exc_opaque) &&
        !((sst == ID_assert || sst == ID_assume) && mentions_exc(s));
      const bool raises = code_may_raise(s);
      if(may_have_raised && wrappable)
      {
        code_ifthenelset guarded{not_exprt{exc}, std::move(s)};
        s = std::move(guarded);
      }
      may_have_raised = may_have_raised || raises;
    }
  }
  else if(st == ID_ifthenelse)
  {
    auto &ite = to_code_ifthenelse(code);
    sequence_exceptions(ite.then_case());
    if(ite.else_case().is_not_nil())
      sequence_exceptions(ite.else_case());
  }
  else if(st == ID_for)
  {
    // A for loop over a container: its condition (index < length) has no
    // effects, so after an exception the loop runs out with its body
    // skipped. Leaving it early instead (break) would give every iteration
    // its own path to the loop exit, merged there with everything the
    // later iterations modified.
    codet &body = to_code_for(code).body();
    sequence_exceptions(body);
    if(code_may_raise(body))
    {
      code_ifthenelset guarded{not_exprt{exc}, std::move(body)};
      body = std::move(guarded);
    }
  }
  else if(st == ID_while)
  {
    // The condition may have effects or never become false (`while True`):
    // leave the loop by its condition, evaluated only without an exception.
    auto &w = to_code_while(code);
    sequence_exceptions(w.body());
    if(code_may_raise(w.body()))
      w.cond() = and_exprt{not_exprt{exc}, w.cond()};
  }
  else if(st == ID_dowhile)
  {
    auto &w = to_code_dowhile(code);
    sequence_exceptions(w.body());
    if(code_may_raise(w.body()))
    {
      code_blockt &b = as_block(w.body());
      code_ifthenelset leave{exc, code_breakt{}};
      leave.set(ID_python_exc_observer, true);
      b.add(std::move(leave));
    }
  }
  if(is_try)
    --exc_seq_trys;
  if(leaves_try)
    ++exc_seq_trys;
}

void python_convertert::compute_raising_functions(
  const std::vector<irep_idt> &bodies)
{
  raising_functions.clear();
  raising_functions_known = false;
  // Direct raisers first (with all calls treated as raising, code_may_raise
  // would make every caller a raiser); then propagate through call edges.
  std::map<irep_idt, std::set<irep_idt>> callees;
  std::set<irep_idt> unknown_callers;
  std::function<void(const exprt &, const irep_idt &)> scan =
    [&](const exprt &e, const irep_idt &fn)
  {
    const exprt *f = nullptr;
    if(e.id() == ID_side_effect && e.get(ID_statement) == ID_function_call)
      f = &to_side_effect_expr_function_call(e).function();
    else if(e.id() == ID_code && to_code(e).get_statement() == ID_function_call)
      f = e.operands().size() >= 2 ? &e.operands()[1] : nullptr;
    if(f != nullptr)
    {
      if(f->id() == ID_symbol)
        callees[fn].insert(to_symbol_expr(*f).get_identifier());
      else
        unknown_callers.insert(fn);
    }
    if(
      e.id() == ID_code && to_code(e).get_statement() == ID_assign &&
      e.operands().size() == 2 && e.operands()[0].id() == ID_symbol &&
      to_symbol_expr(e.operands()[0]).get_identifier() ==
        "python::__exception_active" &&
      !e.operands()[1].is_false())
      raising_functions.insert(fn);
    for(const auto &op : e.operands())
      scan(op, fn);
  };
  for(const auto &id : bodies)
    scan(symbol_table.lookup_ref(id).value, id);
  for(const auto &id : unknown_callers)
    raising_functions.insert(id);
  bool changed = true;
  while(changed)
  {
    changed = false;
    for(const auto &[fn, cs] : callees)
    {
      if(raising_functions.count(fn) > 0)
        continue;
      for(const auto &c : cs)
        if(raising_functions.count(c) > 0)
        {
          raising_functions.insert(fn);
          changed = true;
          break;
        }
    }
  }
  raising_functions_known = true;
}

std::vector<std::string> python_convertert::keyed_dunder_candidates(
  const std::string &tag,
  const std::string &base,
  const typet &key_type)
{
  const std::string bare =
    tag.rfind("python_class_", 0) == 0 ? tag.substr(13) : tag;
  std::string suffix;
  if(is_python_string_type(key_type))
    suffix = "_str__";
  else if(
    key_type.id() == ID_signedbv || key_type.id() == ID_integer ||
    key_type.id() == ID_bool)
    suffix = "_int__";
  std::vector<std::string> out;
  if(!suffix.empty() && base.size() > 2)
  {
    const std::string variant = base.substr(0, base.size() - 2) + suffix;
    out.push_back("python::" + tag + "::" + variant);
    out.push_back("python::" + bare + "::" + variant);
  }
  out.push_back("python::" + tag + "::" + base);
  out.push_back("python::" + bare + "::" + base);
  return out;
}

std::vector<std::string> python_convertert::keyed_dunder_candidates(
  const std::string &tag,
  const std::string &base,
  const jsont &key)
{
  // Static key type without converting (no checks emitted): literals, and
  // names whose symbol has a scalar type.
  typet kt;
  if(is_node_type(key, "Constant"))
  {
    const jsont &v = json_member(key, "value");
    if(v.is_string())
      kt = python_string_type();
    else if(v.is_number() && v.value.find_first_of(".eE") == std::string::npos)
      kt = python_int_type();
    else if(v.is_true() || v.is_false())
      kt = bool_typet{};
  }
  else if(is_node_type(key, "Name"))
  {
    const std::string nm = json_string(json_member(key, "id"));
    const symbolt *s = symbol_table.lookup(irep_idt{qualify_name(nm)});
    if(s == nullptr && !current_function.empty())
      s = symbol_table.lookup(
        irep_idt{"python::" + current_function + "::" + nm});
    if(s == nullptr)
      s = symbol_table.lookup(irep_idt{"python::" + nm});
    if(s != nullptr)
      kt = s->type;
  }
  else if(
    is_node_type(key, "UnaryOp") &&
    is_node_type(json_member(key, "operand"), "Constant"))
    return keyed_dunder_candidates(tag, base, json_member(key, "operand"));
  return keyed_dunder_candidates(tag, base, kt);
}
