/*******************************************************************\

Module: Symbolic Execution of ANSI-C

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

/// \file
/// Symbolic Execution of ANSI-C

#include "goto_symex.h"

#include <util/arith_tools.h>
#include <util/c_types.h>
#include <util/expr_initializer.h>
#include <util/expr_util.h>
#include <util/fresh_symbol.h>
#include <util/invariant_utils.h>
#include <util/optional.h>
#include <util/pointer_offset_size.h>
#include <util/simplify_expr.h>
#include <util/string2int.h>
#include <util/string_constant.h>

inline static optionalt<typet> c_sizeof_type_rec(const exprt &expr)
{
  const irept &sizeof_type=expr.find(ID_C_c_sizeof_type);

  if(!sizeof_type.is_nil())
  {
    return static_cast<const typet &>(sizeof_type);
  }
  else if(expr.id()==ID_mult)
  {
    forall_operands(it, expr)
    {
      const auto t = c_sizeof_type_rec(*it);
      if(t.has_value())
        return t;
    }
  }

  return {};
}

void goto_symext::symex_allocate(
  statet &state,
  const exprt &lhs,
  const side_effect_exprt &code)
{
  PRECONDITION(code.operands().size() == 2);

  if(lhs.is_nil())
    return; // ignore

  dynamic_counter++;

  exprt size=code.op0();
  optionalt<typet> object_type;
  auto function_symbol = outer_symbol_table.lookup(state.source.function_id);
  INVARIANT(function_symbol, "function associated with allocation not found");
  const irep_idt &mode = function_symbol->mode;

  // is the type given?
  if(
    code.type().id() == ID_pointer &&
    to_pointer_type(code.type()).subtype().id() != ID_empty)
  {
    object_type = to_pointer_type(code.type()).subtype();
  }
  else
  {
    exprt tmp_size=size;
    state.rename(tmp_size, ns, field_sensitivity); // do constant propagation
    simplify(tmp_size, ns);

    // special treatment for sizeof(T)*x
    {
      const auto tmp_type = c_sizeof_type_rec(tmp_size);

      if(tmp_type.has_value())
      {
        // Did the size get multiplied?
        auto elem_size = pointer_offset_size(*tmp_type, ns);
        const auto alloc_size = numeric_cast<mp_integer>(tmp_size);

        if(!elem_size.has_value() || *elem_size==0)
        {
        }
        else if(
          !alloc_size.has_value() && tmp_size.id() == ID_mult &&
          tmp_size.operands().size() == 2 &&
          (tmp_size.op0().is_constant() || tmp_size.op1().is_constant()))
        {
          exprt s=tmp_size.op0();
          if(s.is_constant())
          {
            s=tmp_size.op1();
            PRECONDITION(*c_sizeof_type_rec(tmp_size.op0()) == *tmp_type);
          }
          else
            PRECONDITION(*c_sizeof_type_rec(tmp_size.op1()) == *tmp_type);

          object_type = array_typet(*tmp_type, s);
        }
        else
        {
          if(*alloc_size == *elem_size)
            object_type = *tmp_type;
          else
          {
            mp_integer elements = *alloc_size / (*elem_size);

            if(elements * (*elem_size) == *alloc_size)
              object_type =
                array_typet(*tmp_type, from_integer(elements, tmp_size.type()));
          }
        }
      }
    }

    if(!object_type.has_value())
      object_type=array_typet(unsigned_char_type(), tmp_size);

    // we introduce a fresh symbol for the size
    // to prevent any issues of the size getting ever changed

    if(
      object_type->id() == ID_array &&
      !to_array_type(*object_type).size().is_constant())
    {
      exprt &array_size = to_array_type(*object_type).size();

      auxiliary_symbolt size_symbol;

      size_symbol.base_name=
        "dynamic_object_size"+std::to_string(dynamic_counter);
      size_symbol.name="symex_dynamic::"+id2string(size_symbol.base_name);
      size_symbol.type=tmp_size.type();
      size_symbol.mode = mode;
      size_symbol.type.set(ID_C_constant, true);
      size_symbol.value = array_size;

      state.symbol_table.add(size_symbol);

      code_assignt assignment(size_symbol.symbol_expr(), array_size);
      array_size = assignment.lhs();

      symex_assign(state, assignment);
    }
  }

  // value
  symbolt value_symbol;

  value_symbol.base_name="dynamic_object"+std::to_string(dynamic_counter);
  value_symbol.name="symex_dynamic::"+id2string(value_symbol.base_name);
  value_symbol.is_lvalue=true;
  value_symbol.type = *object_type;
  value_symbol.type.set(ID_C_dynamic, true);
  value_symbol.mode = mode;

  state.symbol_table.add(value_symbol);

  exprt zero_init=code.op1();
  state.rename(zero_init, ns, field_sensitivity); // do constant propagation
  simplify(zero_init, ns);

  INVARIANT(
    zero_init.is_constant(), "allocate expects constant as second argument");

  if(!zero_init.is_zero() && !zero_init.is_false())
  {
    const auto zero_value =
      zero_initializer(*object_type, code.source_location(), ns);
    CHECK_RETURN(zero_value.has_value());
    code_assignt assignment(value_symbol.symbol_expr(), *zero_value);
    symex_assign(state, assignment);
  }
  else
  {
    const exprt nondet = path_storage.build_symex_nondet(*object_type);
    const code_assignt assignment(value_symbol.symbol_expr(), nondet);
    symex_assign(state, assignment);
  }

  exprt rhs;

  if(object_type->id() == ID_array)
  {
    const auto &array_type = to_array_type(*object_type);
    index_exprt index_expr(
      value_symbol.symbol_expr(), from_integer(0, index_type()));
    rhs = address_of_exprt(index_expr, pointer_type(array_type.subtype()));
  }
  else
  {
    rhs=address_of_exprt(
      value_symbol.symbol_expr(), pointer_type(value_symbol.type));
  }

  symex_assign(
    state,
    code_assignt(lhs, typecast_exprt::conditional_cast(rhs, lhs.type())));
}

void goto_symext::symex_gcc_builtin_va_start(
  statet &state,
  const exprt &lhs,
  const side_effect_exprt &code)
{
  PRECONDITION(code.operands().size() == 1);

  if(lhs.is_nil())
    return; // ignore

  // create an array holding pointers to the parameters, starting with the
  // parameter that the operand points to
  const exprt &op = skip_typecast(code.op0());
  // this must be the address of a symbol
  const irep_idt &start_parameter =
    to_symbol_expr(to_address_of_expr(op).object()).get_identifier();

  exprt::operandst va_arg_operands;
  bool parameter_id_found = false;
  for(auto &parameter : state.top().parameter_names)
  {
    if(!parameter_id_found && parameter == start_parameter)
      parameter_id_found = true;

    va_arg_operands.push_back(typecast_exprt::conditional_cast(
      address_of_exprt{ns.lookup(parameter).symbol_expr()},
      pointer_type(empty_typet{})));
  }
  const std::size_t va_arg_size = va_arg_operands.size();
  array_exprt array{std::move(va_arg_operands),
                    array_typet{pointer_type(empty_typet{}),
                                from_integer(va_arg_size, size_type())}};

  symbolt &va_array = get_fresh_aux_symbol(
    array.type(),
    id2string(state.source.function_id),
    "va_args",
    state.source.pc->source_location,
    ns.lookup(state.source.function_id).mode,
    state.symbol_table);
  va_array.value = array;

  state.rename(array, ns, field_sensitivity);
  do_simplify(array);
  symex_assign(state, code_assignt{va_array.symbol_expr(), std::move(array)});

  address_of_exprt rhs{index_exprt{va_array.symbol_expr(), from_integer(0, index_type())}};
  state.rename(rhs, ns, field_sensitivity);
  symex_assign(state, code_assignt{lhs, typecast_exprt::conditional_cast(rhs, lhs.type())});
}

irep_idt get_string_argument_rec(const exprt &src)
{
  if(src.id()==ID_typecast)
  {
    PRECONDITION(src.operands().size() == 1);
    return get_string_argument_rec(src.op0());
  }
  else if(src.id()==ID_address_of)
  {
    PRECONDITION(src.operands().size() == 1);
    if(src.op0().id()==ID_index)
    {
      const auto &index_expr = to_index_expr(src.op0());

      if(
        index_expr.array().id() == ID_string_constant &&
        index_expr.index().is_zero())
      {
        const exprt &fmt_str = index_expr.array();
        return to_string_constant(fmt_str).get_value();
      }
    }
  }

  return "";
}

irep_idt get_string_argument(const exprt &src, const namespacet &ns)
{
  exprt tmp=src;
  simplify(tmp, ns);
  return get_string_argument_rec(tmp);
}

void goto_symext::symex_printf(
  statet &state,
  const exprt &rhs)
{
  PRECONDITION(!rhs.operands().empty());

  exprt tmp_rhs=rhs;
  state.rename(tmp_rhs, ns, field_sensitivity);
  do_simplify(tmp_rhs);

  const exprt::operandst &operands=tmp_rhs.operands();
  std::list<exprt> args;

  for(std::size_t i=1; i<operands.size(); i++)
    args.push_back(operands[i]);

  const irep_idt format_string=
    get_string_argument(operands[0], ns);

  if(format_string!="")
    target.output_fmt(
      state.guard.as_expr(),
      state.source, "printf", format_string, args);
}

void goto_symext::symex_input(
  statet &state,
  const codet &code)
{
  PRECONDITION(code.operands().size() >= 2);

  exprt id_arg=code.op0();

  state.rename(id_arg, ns, field_sensitivity);

  std::list<exprt> args;

  for(std::size_t i=1; i<code.operands().size(); i++)
  {
    args.push_back(code.operands()[i]);
    state.rename(args.back(), ns, field_sensitivity);
    do_simplify(args.back());
  }

  const irep_idt input_id=get_string_argument(id_arg, ns);

  target.input(state.guard.as_expr(), state.source, input_id, args);
}

void goto_symext::symex_output(
  statet &state,
  const codet &code)
{
  PRECONDITION(code.operands().size() >= 2);

  exprt id_arg=code.op0();

  state.rename(id_arg, ns, field_sensitivity);

  std::list<exprt> args;

  for(std::size_t i=1; i<code.operands().size(); i++)
  {
    args.push_back(code.operands()[i]);
    state.rename(args.back(), ns, field_sensitivity);
    do_simplify(args.back());
  }

  const irep_idt output_id=get_string_argument(id_arg, ns);

  target.output(state.guard.as_expr(), state.source, output_id, args);
}

/// Handles side effects of type 'new' for C++ and 'new array'
/// for C++ and Java language modes
/// \param state: Symex state
/// \param lhs: left-hand side of assignment
/// \param code: right-hand side containing side effect
void goto_symext::symex_cpp_new(
  statet &state,
  const exprt &lhs,
  const side_effect_exprt &code)
{
  bool do_array;

  PRECONDITION(code.type().id() == ID_pointer);

  const auto &pointer_type = to_pointer_type(code.type());

  do_array =
    (code.get(ID_statement) == ID_cpp_new_array ||
     code.get(ID_statement) == ID_java_new_array_data);

  dynamic_counter++;

  const std::string count_string(std::to_string(dynamic_counter));

  // value
  symbolt symbol;
  symbol.base_name=
    do_array?"dynamic_"+count_string+"_array":
             "dynamic_"+count_string+"_value";
  symbol.name="symex_dynamic::"+id2string(symbol.base_name);
  symbol.is_lvalue=true;
  if(code.get(ID_statement)==ID_cpp_new_array ||
     code.get(ID_statement)==ID_cpp_new)
    symbol.mode=ID_cpp;
  else if(code.get(ID_statement) == ID_java_new_array_data)
    symbol.mode=ID_java;
  else
    INVARIANT_WITH_IREP(false, "Unexpected side effect expression", code);

  if(do_array)
  {
    exprt size_arg = static_cast<const exprt &>(code.find(ID_size));
    clean_expr(size_arg, state, false);
    symbol.type = array_typet(pointer_type.subtype(), size_arg);
  }
  else
    symbol.type = pointer_type.subtype();

  symbol.type.set(ID_C_dynamic, true);

  state.symbol_table.add(symbol);

  // make symbol expression

  exprt rhs(ID_address_of, pointer_type);

  if(do_array)
  {
    rhs.add_to_operands(
      index_exprt(symbol.symbol_expr(), from_integer(0, index_type())));
  }
  else
    rhs.copy_to_operands(symbol.symbol_expr());

  symex_assign(state, code_assignt(lhs, rhs));
}

void goto_symext::symex_cpp_delete(
  statet &,
  const codet &)
{
  // TODO
  #if 0
  bool do_array=code.get(ID_statement)==ID_cpp_delete_array;
  #endif
}

void goto_symext::symex_trace(
  statet &state,
  const code_function_callt &code)
{
  PRECONDITION(code.arguments().size() >= 2);

  mp_integer debug_lvl;
  optionalt<mp_integer> maybe_debug =
    numeric_cast<mp_integer>(code.arguments()[0]);
  DATA_INVARIANT(
    maybe_debug.has_value(), "CBMC_trace expects constant as first argument");
  debug_lvl = maybe_debug.value();

  DATA_INVARIANT(
    code.arguments()[1].id() == "implicit_address_of" &&
      code.arguments()[1].operands().size() == 1 &&
      code.arguments()[1].op0().id() == ID_string_constant,
    "CBMC_trace expects string constant as second argument");

  if(symex_config.debug_level >= debug_lvl)
  {
    std::list<exprt> vars;

    irep_idt event = to_string_constant(code.arguments()[1].op0()).get_value();

    for(std::size_t j=2; j<code.arguments().size(); j++)
    {
      exprt var(code.arguments()[j]);
      state.rename(var, ns, field_sensitivity);
      vars.push_back(var);
    }

    target.output(state.guard.as_expr(), state.source, event, vars);
  }
}

void goto_symext::symex_fkt(
  statet &,
  const code_function_callt &)
{
  // TODO: uncomment this line when TG-4667 is done
  // UNREACHABLE;
  #if 0
  exprt new_fc(ID_function, fc.type());

  new_fc.reserve_operands(fc.operands().size()-1);

  bool first=true;

  Forall_operands(it, fc)
    if(first)
      first=false;
    else
      new_fc.add_to_operands(std::move(*it));

  new_fc.set(ID_identifier, fc.op0().get(ID_identifier));

  fc.swap(new_fc);
  #endif
}
