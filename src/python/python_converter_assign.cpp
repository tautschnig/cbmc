/// Python to GOTO converter — assignment statement
/// handlers: AnnAssign (PLR §7.2.1), Assign (§7.2),
/// AugAssign (§7.2.2).
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

// PLR §7.2.1: Annotated assignment statements
// "Annotation assignment is the combination, in a single statement, of a
// variable or attribute annotation and an optional assignment statement."
codet python_convertert::convert_ann_assign(const jsont &stmt)
{
  // PLR §7.2.1: Annotated assignment
  const jsont &target = json_member(stmt, "target");
  const jsont &annotation = json_member(stmt, "annotation");
  const jsont &value = json_member(stmt, "value");
  source_locationt loc = get_location(stmt);

  // Handle self.attr: Type = value (attribute target)
  if(is_node_type(target, "Attribute"))
  {
    if(value.is_null())
      return code_skipt{};
    exprt obj = convert_expression(json_member(target, "value"));
    std::string attr = json_string(json_member(target, "attr"));
    exprt rhs = convert_expression(value);
    if(obj.is_nil() || rhs.is_nil())
      return code_skipt{};

    typet obj_type = obj.type();
    if(obj_type.id() == ID_pointer)
    {
      const auto &base = to_pointer_type(obj_type).base_type();
      if(base.id() == ID_struct)
      {
        const auto &st = to_struct_type(base);
        if(st.has_component(attr))
        {
          member_exprt lhs{
            dereference_exprt{obj}, attr, st.get_component(attr).type()};
          if(rhs.type() != lhs.type())
            rhs = safe_typecast(rhs, lhs.type());
          code_frontend_assignt assign{lhs, rhs};
          assign.add_source_location() = loc;
          return std::move(assign);
        }
      }
    }
    return code_skipt{};
  }

  std::string var_name = json_string(json_member(target, "id"));
  typet var_type = convert_type_annotation(annotation);

  std::string qualified_name = qualify_name(var_name);
  irep_idt symbol_id{qualified_name};

  // Name-shadowing of imported functions. When a user's local
  // variable name happens to collide with a function the import
  // pipeline added at the same qualified scope (e.g. `match: ... =
  // re.match(...)` reusing `python::match` because re's module
  // contents are flattened into the top-level namespace), reusing
  // the existing code-typed symbol would have CBMC's symex abort
  // with "assignment to 'symbol' not handled". Instead, rename the
  // new variable to a fresh `__shadow_<name>__vN` symbol and
  // register the redirection in variable_versions so later reads
  // of `var_name` resolve to the local variable rather than the
  // imported function.
  {
    const symbolt *existing = symbol_table.lookup(symbol_id);
    if(
      existing != nullptr && existing->type.id() == ID_code &&
      var_type.id() != ID_code)
    {
      unsigned &ver = version_counters[qualified_name];
      ver++;
      std::string versioned_name =
        qualified_name + "__shadow__v" + std::to_string(ver);
      irep_idt versioned_id{versioned_name};
      if(symbol_table.lookup(versioned_id) == nullptr)
      {
        symbolt new_symbol{versioned_id, var_type, "python"};
        new_symbol.base_name = var_name + "__shadow__v" + std::to_string(ver);
        new_symbol.location = loc;
        new_symbol.is_lvalue = true;
        new_symbol.is_state_var = true;
        new_symbol.is_static_lifetime = current_function.empty();
        symbol_table.add(new_symbol);
      }
      variable_versions[qualified_name] = versioned_id;
      symbol_id = versioned_id;
      qualified_name = versioned_name;
    }
  }

  // Create symbol if it doesn't exist
  if(symbol_table.lookup(symbol_id) == nullptr)
  {
    symbolt new_symbol{symbol_id, var_type, "python"};
    new_symbol.base_name = var_name;
    new_symbol.location = loc;
    new_symbol.is_lvalue = true;
    new_symbol.is_state_var = true;
    new_symbol.is_static_lifetime = current_function.empty();
    symbol_table.add(new_symbol);
  }

  if(value.is_null())
    return code_skipt{};

  exprt rhs = convert_expression(value);
  if(rhs.is_nil())
    return code_skipt{};

  // Function alias: g: Callable = double → record alias
  if(rhs.id() == ID_symbol && rhs.type().id() == ID_code)
  {
    function_aliases[qualified_name] = to_symbol_expr(rhs).get_identifier();
    return code_skipt{};
  }

  // If annotation gave a placeholder type (e.g., dict→int) but the RHS
  // has a concrete struct type, use the RHS type instead.
  const symbolt &sym = symbol_table.lookup_ref(symbol_id);
  if(
    sym.type != rhs.type() && rhs.type().id() == ID_struct &&
    (sym.type == python_int_type() || sym.type.id() != ID_struct ||
     (is_python_list_type(sym.type) && is_python_list_type(rhs.type())) ||
     (is_python_dict_type(sym.type) && is_python_dict_type(rhs.type()))))
  {
    symbol_table.get_writeable_ref(symbol_id).type = rhs.type();
  }

  const symbolt &sym2 = symbol_table.lookup_ref(symbol_id);

  // Type cast if needed
  if(rhs.type() != sym2.type)
  {
    // PLR soundness: if the assigned value's type is obviously
    // incompatible with the declared annotation (e.g.
    // 'x: int = "hello"'), emit an annotation-mismatch property.
    // Gated by --python-check-annotations.
    if(
      python_check_annotations &&
      annotation_types_incompatible(sym2.type, rhs.type()))
    {
      add_check(
        false_exprt{},
        "annotation-mismatch",
        "assigned value's type does not match declared type of '" + var_name +
          "'",
        loc);
    }
    rhs = safe_typecast(rhs, sym2.type);
  }

  code_frontend_assignt assign{sym2.symbol_expr(), rhs};
  assign.add_source_location() = loc;
  // Track constant string values
  if(is_python_string_type(rhs.type()))
  {
    auto sv = extract_string_value(rhs);
    if(!sv.has_value() && rhs.id() == ID_symbol)
    {
      auto it = string_constants.find(to_symbol_expr(rhs).get_identifier());
      if(it != string_constants.end())
        sv = it->second;
    }
    if(sv.has_value())
      string_constants[symbol_id] = sv.value();
    else
      string_constants.erase(symbol_id);
  }
  if(is_python_dict_type(rhs.type()))
  {
    if(rhs.id() == ID_struct)
    {
      dict_literals[symbol_id] = rhs;
      // Populate per-key static category from the original
      // AST. The struct exprt's value array has had all
      // values typecast to a single uniform type via
      // safe_typecast, which loses the original category.
      // The AST is the source of truth.
      if(is_node_type(value, "Dict"))
      {
        const jsont &dkeys = json_member(value, "keys");
        const jsont &dvals = json_member(value, "values");
        if(dkeys.is_array() && dvals.is_array())
        {
          std::map<std::string, std::string> cats;
          std::map<std::string, std::string> str_consts;
          auto kit = as_array(dkeys).begin();
          auto vit = as_array(dvals).begin();
          for(; kit != as_array(dkeys).end() && vit != as_array(dvals).end();
              ++kit, ++vit)
          {
            if(!is_node_type(*kit, "Constant"))
              continue;
            const jsont &kv = json_member(*kit, "value");
            if(!kv.is_string())
              continue;
            std::string cat = ast_value_category(*vit);
            if(!cat.empty())
              cats[kv.value] = cat;
            // If the value AST is itself a Constant(str), record
            // the constant string for downstream regex-precision
            // checks (Stage 1 of the re-precision plan).
            if(is_node_type(*vit, "Constant"))
            {
              const jsont &cval = json_member(*vit, "value");
              if(cval.is_string())
                str_consts[kv.value] = cval.value;
            }
          }
          if(!cats.empty())
            dict_literal_value_categories[symbol_id] = std::move(cats);
          if(!str_consts.empty())
            dict_literal_value_string_consts[symbol_id] = std::move(str_consts);
        }
      }
    }
    else if(rhs.id() == ID_side_effect)
    {
      // Inter-procedural dict-literal propagation: if the
      // RHS is a call to a function whose return-statement
      // we recorded as a dict literal, synthesize a
      // dict-struct with the known keys so the caller's
      // subscript lookups can prove the key exists.
      const auto &se = to_side_effect_expr(rhs);
      if(
        se.get_statement() == ID_function_call && !se.operands().empty() &&
        se.operands()[0].id() == ID_symbol)
      {
        std::string callee =
          id2string(to_symbol_expr(se.operands()[0]).get_identifier());
        std::string prefix = "python::";
        if(callee.substr(0, prefix.size()) == prefix)
          callee = callee.substr(prefix.size());
        auto ki = function_returned_dict_keys.find(callee);
        if(ki != function_returned_dict_keys.end() && !ki->second.empty())
        {
          // Build a sentinel dict-struct with the known keys
          // (values nondet). Match rhs.type() so dict_literals
          // entry stays consistent with the assigned var's
          // type.
          const auto &dt = to_struct_type(rhs.type());
          const auto &keys_type = to_array_type(dt.components()[1].type());
          const auto &vals_type = to_array_type(dt.components()[2].type());
          exprt::operandst key_elems, val_elems;
          for(const auto &k : ki->second)
          {
            key_elems.push_back(python_string_literal(k));
            val_elems.push_back(safe_zero(vals_type.element_type()));
          }
          while(key_elems.size() < PYTHON_MAX_DICT_SIZE)
          {
            key_elems.push_back(safe_zero(keys_type.element_type()));
            val_elems.push_back(safe_zero(vals_type.element_type()));
          }
          exprt length = from_integer(
            static_cast<long long>(ki->second.size()), signedbv_typet{64});
          dict_literals[symbol_id] = struct_exprt{
            {length,
             array_exprt{std::move(key_elems), keys_type},
             array_exprt{std::move(val_elems), vals_type}},
            rhs.type()};
        }
        else
          dict_literals.erase(symbol_id);
      }
      else
        dict_literals.erase(symbol_id);
    }
    else
      dict_literals.erase(symbol_id);
  }
  if(is_python_list_type(rhs.type()))
  {
    if(rhs.id() == ID_struct)
      list_literals[symbol_id] = rhs;
    else
      list_literals.erase(symbol_id);
  }
  if(is_python_tuple_type(rhs.type()))
  {
    if(rhs.id() == ID_struct)
      tuple_literals[symbol_id] = rhs;
    else
      tuple_literals.erase(symbol_id);
  }
  // Track numeric constants (including expressions)
  {
    auto ev = try_eval_double(rhs);
    if(ev.has_value())
      float_constants[symbol_id] = ev.value();
    else
      float_constants.erase(symbol_id);
  }
  return std::move(assign);
}

// PLR §7.2: Assignment statements
// "Assignment statements are used to (re)bind names to values and to
// modify attributes or items of mutable objects."
codet python_convertert::convert_assign(const jsont &stmt)
{
  // x = expr  OR  a = b = expr (multiple targets)
  const jsont &targets = json_member(stmt, "targets");
  const jsont &value = json_member(stmt, "value");

  if(!targets.is_array() || as_array(targets).empty())
    return code_skipt{};

  source_locationt loc = get_location(stmt);

  // Check if RHS is a constructor call
  if(
    is_node_type(value, "Call") &&
    is_node_type(json_member(value, "func"), "Name"))
  {
    std::string call_name =
      json_string(json_member(json_member(value, "func"), "id"));
    if(class_types.count(call_name))
    {
      const jsont &first_target = *as_array(targets).begin();

      // Attribute target: self.inner = ClassName(args)
      if(is_node_type(first_target, "Attribute"))
      {
        exprt obj = convert_expression(json_member(first_target, "value"));
        std::string attr = json_string(json_member(first_target, "attr"));
        exprt lhs_obj = obj;
        if(obj.type().id() == ID_pointer)
          lhs_obj = dereference_exprt{obj};

        if(lhs_obj.type().id() == ID_struct)
        {
          const auto &st = to_struct_type(lhs_obj.type());
          if(st.has_component(attr))
          {
            member_exprt lhs{lhs_obj, attr, st.get_component(attr).type()};
            std::string tmp_name = "__ctor_tmp_" + attr;
            std::string tmp_qname = qualify_name(tmp_name);
            irep_idt tmp_id{tmp_qname};
            typet attr_type = st.get_component(attr).type();

            if(symbol_table.lookup(tmp_id) == nullptr)
            {
              symbolt tmp_sym{tmp_id, attr_type, "python"};
              tmp_sym.base_name = tmp_name;
              tmp_sym.is_lvalue = true;
              tmp_sym.is_state_var = true;
              symbol_table.add(tmp_sym);
            }

            const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);
            code_blockt result;

            irep_idt init_id{"python::" + call_name + "::__init__"};
            const symbolt *init_sym = symbol_table.lookup(init_id);
            if(init_sym != nullptr)
            {
              exprt::operandst init_args;
              init_args.push_back(address_of_exprt{tmp_sym.symbol_expr()});
              const jsont &call_args = json_member(value, "args");
              if(call_args.is_array())
              {
                for(const auto &a : as_array(call_args))
                  init_args.push_back(convert_expression(a));
              }
              // Match argument types to parameter types
              const auto &init_params =
                to_code_type(init_sym->type).parameters();
              for(std::size_t ai = 0;
                  ai < init_args.size() && ai < init_params.size();
                  ai++)
              {
                if(init_args[ai].type() != init_params[ai].type())
                  init_args[ai] =
                    safe_typecast(init_args[ai], init_params[ai].type());
              }
              side_effect_expr_function_callt call{
                init_sym->symbol_expr(),
                std::move(init_args),
                empty_typet{},
                loc};
              result.add(code_expressiont{call});
            }

            result.add(code_frontend_assignt{lhs, tmp_sym.symbol_expr()});
            return std::move(result);
          }
        }
      }

      // Name target: x = ClassName(args)
      if(is_node_type(first_target, "Name"))
      {
        const struct_typet &cls_type = class_types[call_name];
        std::string var_name = json_string(json_member(first_target, "id"));
        std::string qualified_name = qualify_name(var_name);
        irep_idt symbol_id{qualified_name};

        if(symbol_table.lookup(symbol_id) == nullptr)
        {
          symbolt new_symbol{symbol_id, cls_type, "python"};
          new_symbol.base_name = var_name;
          new_symbol.location = loc;
          new_symbol.is_lvalue = true;
          new_symbol.is_state_var = true;
          new_symbol.is_static_lifetime = current_function.empty();
          symbol_table.add(new_symbol);
        }

        const symbolt &var_sym = symbol_table.lookup_ref(symbol_id);
        code_blockt result;

        // Identify whether the destination symbol's type can actually
        // hold class-instance state. Python allows rebinding a name
        // to a different type, so a previous scalar use of the same
        // name may leave the symbol with a non-struct type. In that
        // case we skip the class-tag / default-value initialisation —
        // the subsequent __init__ call will still run, and we log a
        // warning so the user knows the state will not be tracked
        // precisely.
        const typet &resolved_var_type = var_sym.type;
        const bool var_holds_struct = resolved_var_type.id() == ID_struct ||
                                      resolved_var_type.id() == ID_struct_tag;

        // Set __class_tag to the actual class being constructed
        if(var_holds_struct && class_tag_ids.count(call_name))
        {
          result.add(code_frontend_assignt{
            member_exprt{
              var_sym.symbol_expr(), "__class_tag", signedbv_typet{32}},
            from_integer(class_tag_ids[call_name], signedbv_typet{32})});
        }

        // Copy class-level default values from the class object
        irep_idt class_obj_id{"python::" + call_name};
        const symbolt *class_obj = symbol_table.lookup(class_obj_id);
        if(
          var_holds_struct && class_obj != nullptr &&
          !class_obj->value.is_nil())
        {
          result.add(code_frontend_assignt{
            var_sym.symbol_expr(), class_obj->symbol_expr()});
        }

        if(!var_holds_struct && class_tag_ids.count(call_name))
        {
          log.warning() << "Variable '" << var_sym.base_name
                        << "' is being rebound to class instance of '"
                        << call_name << "' but already has non-struct type '"
                        << resolved_var_type.id_string()
                        << "'; class state will not be tracked precisely."
                        << messaget::eom;
        }

        // Call __init__(&var, args...)
        irep_idt init_id{"python::" + call_name + "::__init__"};
        const symbolt *init_sym = symbol_table.lookup(init_id);
        if(init_sym != nullptr)
        {
          exprt::operandst arguments;
          arguments.push_back(address_of_exprt{var_sym.symbol_expr()});

          const jsont &call_args = json_member(value, "args");
          if(call_args.is_array())
          {
            for(const auto &arg : as_array(call_args))
              arguments.push_back(convert_expression(arg));
          }

          // Handle keyword arguments
          const code_typet &init_type = to_code_type(init_sym->type);
          const jsont &kw_args = json_member(value, "keywords");
          if(kw_args.is_array())
          {
            for(const auto &kw : as_array(kw_args))
            {
              std::string kw_name = json_string(json_member(kw, "arg"));
              exprt kw_val = convert_expression(json_member(kw, "value"));
              for(std::size_t pi = 0; pi < init_type.parameters().size(); pi++)
              {
                if(
                  id2string(init_type.parameters()[pi].get_base_name()) ==
                  kw_name)
                {
                  while(arguments.size() <= pi)
                    arguments.push_back(nil_exprt{});
                  arguments[pi] = kw_val;
                  break;
                }
              }
            }
          }

          // Pad missing arguments with defaults (safe_zero for each param type)
          while(arguments.size() < init_type.parameters().size())
          {
            std::size_t idx = arguments.size();
            arguments.push_back(safe_zero(init_type.parameters()[idx].type()));
          }
          // Replace nil entries (gaps from keyword matching) with defaults
          for(std::size_t i = 0; i < arguments.size(); i++)
          {
            if(arguments[i].is_nil() && i < init_type.parameters().size())
              arguments[i] = safe_zero(init_type.parameters()[i].type());
          }

          // Typecast arguments to match parameter types
          for(std::size_t i = 0;
              i < arguments.size() && i < init_type.parameters().size();
              i++)
          {
            if(arguments[i].type() != init_type.parameters()[i].type())
              arguments[i] =
                safe_typecast(arguments[i], init_type.parameters()[i].type());
          }

          side_effect_expr_function_callt call{
            init_sym->symbol_expr(), std::move(arguments), empty_typet{}, loc};
          code_expressiont call_stmt{call};
          call_stmt.add_source_location() = loc;
          result.add(std::move(call_stmt));
        }

        if(result.statements().size() == 1)
          return result.statements().front();
        return std::move(result);
      }
    }
  }

  exprt rhs = convert_expression(value);
  if(rhs.is_nil())
    return code_skipt{};

  // Lambda/function assignment: record alias instead of creating variable
  if(rhs.id() == ID_symbol && rhs.type().id() == ID_code)
  {
    for(const auto &target : as_array(targets))
    {
      if(is_node_type(target, "Name"))
      {
        std::string var_name = json_string(json_member(target, "id"));
        function_aliases[qualify_name(var_name)] =
          to_symbol_expr(rhs).get_identifier();
      }
    }
    return code_skipt{};
  }

  // Check if RHS is a call to a lambda-returning function
  if(rhs.id() == ID_side_effect)
  {
    const auto &se = to_side_effect_expr(rhs);
    if(se.get_statement() == ID_function_call && !se.operands().empty())
    {
      const exprt &func_op = se.operands()[0];
      if(func_op.id() == ID_symbol)
      {
        std::string called =
          id2string(to_symbol_expr(func_op).get_identifier());
        // Strip "python::" prefix
        if(called.substr(0, 8) == "python::")
          called = called.substr(8);
        auto it = lambda_returning_functions.find(called);
        if(it != lambda_returning_functions.end())
        {
          code_blockt lam_block;
          // Bind call arguments to closure captures
          const jsont &call_args =
            json_member(json_member(stmt, "value"), "args");
          if(call_args.is_array())
          {
            irep_idt fid{"python::" + called};
            const symbolt *fsym = symbol_table.lookup(fid);
            if(fsym != nullptr && fsym->type.id() == ID_code)
            {
              const auto &fp = to_code_type(fsym->type).parameters();
              auto ai = as_array(call_args).begin();
              for(std::size_t i = 0;
                  i < fp.size() && ai != as_array(call_args).end();
                  i++, ++ai)
              {
                exprt av = convert_expression(*ai);
                std::string pid = id2string(fp[i].get_identifier());
                auto ci = closure_captures.find(id2string(it->second));
                if(ci != closure_captures.end())
                {
                  for(auto &cap : ci->second)
                  {
                    if(std::get<0>(cap) == pid)
                    {
                      static unsigned lb = 0;
                      std::string tn = "__lam_bind_" + std::to_string(lb++);
                      std::string tq = qualify_name(tn);
                      irep_idt ti{tq};
                      if(symbol_table.lookup(ti) == nullptr)
                      {
                        symbolt ts{ti, av.type(), "python"};
                        ts.base_name = tn;
                        ts.is_lvalue = true;
                        ts.is_state_var = true;
                        ts.is_static_lifetime = true;
                        symbol_table.add(ts);
                      }
                      lam_block.add(code_frontend_assignt{
                        symbol_table.lookup_ref(ti).symbol_expr(), av});
                      std::get<0>(cap) = id2string(ti);
                    }
                  }
                }
              }
            }
          }
          for(const auto &target : as_array(targets))
          {
            if(is_node_type(target, "Name"))
            {
              std::string var_name = json_string(json_member(target, "id"));
              function_aliases[qualify_name(var_name)] = it->second;
            }
          }
          if(lam_block.statements().empty())
            return code_skipt{};
          return std::move(lam_block);
        }
      }
    }
  }

  // Detect bound method assignment: method = obj.func
  // Exclusion: when attr is a @property, obj.attr evaluates
  // the property method and produces a value — not a bound-
  // method reference. Fall through to the normal assign path.
  {
    const jsont &val_node = json_member(stmt, "value");
    if(is_node_type(val_node, "Attribute"))
    {
      std::string attr = json_string(json_member(val_node, "attr"));
      exprt obj_expr = convert_expression(json_member(val_node, "value"));
      if(
        !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
        id2string(to_struct_type(obj_expr.type()).get_tag())
            .find("python_class_") != std::string::npos)
      {
        std::string tag = id2string(to_struct_type(obj_expr.type()).get_tag());
        std::string cls_name = tag.substr(13);
        // Skip @property — attribute access is a value, not a method alias.
        auto pit = class_property_methods.find(cls_name);
        if(pit != class_property_methods.end() && pit->second.count(attr))
        {
          // Fall through to normal assign path.
        }
        else
        {
          irep_idt method_id{"python::" + cls_name + "::" + attr};
          if(symbol_table.lookup(method_id) != nullptr)
          {
            for(const auto &target : as_array(targets))
            {
              if(is_node_type(target, "Name"))
              {
                std::string var_name = json_string(json_member(target, "id"));
                std::string qname = qualify_name(var_name);
                function_aliases[qname] = method_id;
                bound_methods[qname] = {method_id, address_of_exprt{obj_expr}};
              }
            }
            return code_skipt{};
          }
        }
      }
    }
  }

  code_blockt block;

  for(const auto &target : as_array(targets))
  {
    // Handle tuple unpacking: a, b, c = expr
    if(is_node_type(target, "Tuple") || is_node_type(target, "List"))
    {
      const jsont &elts = json_member(target, "elts");
      if(elts.is_array() && is_python_tuple_type(rhs.type()))
      {
        const auto &tuple_st = to_struct_type(rhs.type());
        // PLR §7.2.1: the assignment target list is bound
        // _after_ the expression list on the right is
        // fully evaluated, so the swap idiom
        //     a, b = b, a
        // must not read the updated a/b from its own LHS.
        // Materialise the RHS into a fresh tmp so every
        // field read references the snapshot before any
        // LHS update.
        static unsigned unpack_ctr = 0;
        std::string tmpn = "__unpack_" + std::to_string(unpack_ctr++);
        std::string tmpq = qualify_name(tmpn);
        irep_idt tmpid{tmpq};
        if(symbol_table.lookup(tmpid) == nullptr)
        {
          symbolt ts{tmpid, rhs.type(), "python"};
          ts.base_name = tmpn;
          ts.is_lvalue = true;
          ts.is_state_var = true;
          ts.is_static_lifetime = current_function.empty();
          symbol_table.add(ts);
        }
        symbol_exprt rhs_snapshot =
          symbol_table.lookup_ref(tmpid).symbol_expr();
        block.add(code_frontend_assignt{rhs_snapshot, rhs});
        exprt src = rhs_snapshot;
        std::size_t idx = 0;
        for(const auto &elt : as_array(elts))
        {
          std::string field = "_" + std::to_string(idx);
          if(!tuple_st.has_component(field))
          {
            idx++;
            continue;
          }
          typet field_type = tuple_st.get_component(field).type();
          member_exprt field_expr{src, field, field_type};

          // Recursive unpack: if elt is Tuple/List, unpack the field
          if(is_node_type(elt, "Tuple") || is_node_type(elt, "List"))
          {
            const jsont &sub_elts = json_member(elt, "elts");
            if(sub_elts.is_array() && is_python_tuple_type(field_type))
            {
              const auto &sub_st = to_struct_type(field_type);
              std::size_t sub_idx = 0;
              for(const auto &sub_elt : as_array(sub_elts))
              {
                std::string sub_field = "_" + std::to_string(sub_idx);
                if(sub_st.has_component(sub_field))
                {
                  typet sub_type = sub_st.get_component(sub_field).type();
                  member_exprt sub_expr{field_expr, sub_field, sub_type};
                  std::string name = json_string(json_member(sub_elt, "id"));
                  if(!name.empty())
                  {
                    std::string qname = qualify_name(name);
                    irep_idt sym_id{qname};
                    if(symbol_table.lookup(sym_id) == nullptr)
                    {
                      symbolt new_sym{sym_id, sub_type, "python"};
                      new_sym.base_name = name;
                      new_sym.location = loc;
                      new_sym.is_lvalue = true;
                      new_sym.is_state_var = true;
                      new_sym.is_static_lifetime = current_function.empty();
                      symbol_table.add(new_sym);
                    }
                    block.add(code_frontend_assignt{
                      symbol_table.lookup_ref(sym_id).symbol_expr(), sub_expr});
                  }
                }
                sub_idx++;
              }
            }
          }
          else
          {
            // Simple Name target
            std::string elt_name = json_string(json_member(elt, "id"));
            if(!elt_name.empty())
            {
              std::string qname = qualify_name(elt_name);
              irep_idt sym_id{qname};
              if(symbol_table.lookup(sym_id) == nullptr)
              {
                symbolt new_sym{sym_id, field_type, "python"};
                new_sym.base_name = elt_name;
                new_sym.location = loc;
                new_sym.is_lvalue = true;
                new_sym.is_state_var = true;
                new_sym.is_static_lifetime = current_function.empty();
                symbol_table.add(new_sym);
              }
              const symbolt &target_sym = symbol_table.lookup_ref(sym_id);
              exprt typed_field = field_expr;
              if(typed_field.type() != target_sym.type)
                typed_field = safe_typecast(typed_field, target_sym.type);
              block.add(
                code_frontend_assignt{target_sym.symbol_expr(), typed_field});
            }
          }
          idx++;
        }
        continue;
      }
    }

    // Handle subscript assignment: lst[i] = value
    // PLR §3.2: "Tuples are immutable sequences"
    if(is_node_type(target, "Subscript"))
    {
      // PLR §3.3.1: custom __setitem__ dunder — if the
      // subscripted value is a user-defined class instance
      // with a __setitem__ method, dispatch to it.
      bool dispatched_setitem = false;
      {
        const jsont &target_value_root = json_member(target, "value");
        exprt obj_probe = convert_expression(target_value_root);
        if(!obj_probe.is_nil())
        {
          std::string tag;
          if(obj_probe.type().id() == ID_struct)
            tag = id2string(to_struct_type(obj_probe.type()).get_tag());
          else if(obj_probe.type().id() == ID_struct_tag)
            tag =
              id2string(to_struct_tag_type(obj_probe.type()).get_identifier());
          if(tag.substr(0, 13) == "python_class_")
          {
            std::string bare = tag.substr(13);
            for(const std::string &prefix :
                {std::string{"python::"} + tag + "::__setitem__",
                 std::string{"python::"} + bare + "::__setitem__"})
            {
              const symbolt *ss = symbol_table.lookup(irep_idt{prefix});
              if(ss != nullptr)
              {
                exprt slice = convert_expression(json_member(target, "slice"));
                if(!slice.is_nil())
                {
                  side_effect_expr_function_callt call{
                    ss->symbol_expr(),
                    {address_of_exprt{obj_probe}, slice, rhs},
                    empty_typet{},
                    loc};
                  block.add(code_expressiont{std::move(call)});
                }
                dispatched_setitem = true;
                break;
              }
            }
          }
        }
      }
      if(dispatched_setitem)
        continue;
      // Nested subscript (e.g. d["a"][0] = v) — the inner
      // read returns a struct copy, so writing into it is
      // lost. Rewrite at statement level to:
      //     __nest_N = d["a"]
      //     __nest_N[0] = v
      //     d["a"] = __nest_N
      // This makes the mutation visible through the outer
      // container. Recursive: the final write back to
      // d["a"] goes through the same subscript-assign
      // path, so triple-nested targets unfold one level
      // per rewrite.
      const jsont &target_value = json_member(target, "value");
      if(is_node_type(target_value, "Subscript"))
      {
        exprt inner_read = convert_expression(target_value);
        if(
          !inner_read.is_nil() && (is_python_list_type(inner_read.type()) ||
                                   is_python_dict_type(inner_read.type())))
        {
          static unsigned nest_ctr = 0;
          std::string tn = "__nest_" + std::to_string(nest_ctr++);
          std::string tq = qualify_name(tn);
          irep_idt ti{tq};
          if(symbol_table.lookup(ti) == nullptr)
          {
            symbolt ts{ti, inner_read.type(), "python"};
            ts.base_name = tn;
            ts.is_lvalue = true;
            ts.is_state_var = true;
            ts.is_static_lifetime = current_function.empty();
            symbol_table.add(ts);
          }
          symbol_exprt tmp_sym = symbol_table.lookup_ref(ti).symbol_expr();
          // 1. tmp = d["a"] (snapshot read)
          block.add(code_frontend_assignt{tmp_sym, inner_read});
          // 2. tmp[slice] = rhs (first-level write)
          const jsont &slice_node = json_member(target, "slice");
          exprt key = convert_expression(slice_node);
          if(!key.is_nil())
          {
            if(is_python_list_type(tmp_sym.type()))
            {
              const auto &list_st = to_struct_type(tmp_sym.type());
              const auto &data_type =
                to_array_type(list_st.components()[1].type());
              member_exprt data{tmp_sym, "data", data_type};
              exprt typed_rhs = rhs;
              if(typed_rhs.type() != data_type.element_type())
                typed_rhs = safe_typecast(typed_rhs, data_type.element_type());
              block.add(code_frontend_assignt{
                index_exprt{data, key}, std::move(typed_rhs)});
            }
            else if(is_python_dict_type(tmp_sym.type()))
            {
              const auto &dict_st = to_struct_type(tmp_sym.type());
              const auto &keys_type =
                to_array_type(dict_st.components()[1].type());
              const auto &vals_type =
                to_array_type(dict_st.components()[2].type());
              member_exprt length{tmp_sym, "length", signedbv_typet{64}};
              member_exprt keys_arr{tmp_sym, "keys", keys_type};
              member_exprt vals_arr{tmp_sym, "values", vals_type};
              exprt typed_key = key;
              if(typed_key.type() != keys_type.element_type())
                typed_key = safe_typecast(typed_key, keys_type.element_type());
              exprt typed_rhs = rhs;
              if(typed_rhs.type() != vals_type.element_type())
                typed_rhs = safe_typecast(typed_rhs, vals_type.element_type());
              // scan-replace-or-append (mirrors the existing
              // dict-subscript-assign path).
              static unsigned ns_fnd = 0;
              std::string fn = "__nest_fnd_" + std::to_string(ns_fnd++);
              std::string fq = qualify_name(fn);
              irep_idt fi{fq};
              if(symbol_table.lookup(fi) == nullptr)
              {
                symbolt fs{fi, bool_typet{}, "python"};
                fs.base_name = fn;
                fs.is_lvalue = true;
                fs.is_state_var = true;
                symbol_table.add(fs);
              }
              symbol_exprt found = symbol_table.lookup_ref(fi).symbol_expr();
              block.add(code_frontend_assignt{found, false_exprt{}});
              for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
              {
                exprt idx = from_integer(i, signedbv_typet{64});
                exprt in_range = binary_relation_exprt{idx, ID_lt, length};
                exprt match =
                  equal_exprt{index_exprt{keys_arr, idx}, typed_key};
                code_blockt upd;
                upd.add(
                  code_frontend_assignt{index_exprt{vals_arr, idx}, typed_rhs});
                upd.add(code_frontend_assignt{found, true_exprt{}});
                block.add(
                  code_ifthenelset{and_exprt{in_range, match}, std::move(upd)});
              }
              code_blockt append;
              append.add(code_frontend_assignt{
                index_exprt{keys_arr, length}, typed_key});
              append.add(code_frontend_assignt{
                index_exprt{vals_arr, length}, std::move(typed_rhs)});
              append.add(code_frontend_assignt{
                length,
                plus_exprt{length, from_integer(1, signedbv_typet{64})}});
              block.add(code_ifthenelset{not_exprt{found}, std::move(append)});
            }
          }
          // 3. d["a"] = tmp (write back via outer subscript).
          // Emit a synthetic subscript-assign recursively by
          // constructing an equivalent statement through the
          // same handler. We do this by building the outer
          // target's base and slice from target_value, and
          // driving the assign inline. Since the outer is
          // itself potentially a Subscript, recursion would be
          // ideal, but we inline one level for now.
          exprt outer_base =
            convert_expression(json_member(target_value, "value"));
          const jsont &outer_slice_node = json_member(target_value, "slice");
          exprt outer_key = convert_expression(outer_slice_node);
          if(
            !outer_base.is_nil() && !outer_key.is_nil() &&
            is_python_dict_type(outer_base.type()))
          {
            // Invalidate dict_literals tracking: the outer
            // container's 'a' slot now points to a mutated
            // inner container.
            const jsont &outer_val_node = json_member(target_value, "value");
            if(is_node_type(outer_val_node, "Name"))
            {
              std::string outer_name =
                json_string(json_member(outer_val_node, "id"));
              dict_literals.erase(irep_idt{qualify_name(outer_name)});
            }
            const auto &dict_st = to_struct_type(outer_base.type());
            const auto &keys_type =
              to_array_type(dict_st.components()[1].type());
            const auto &vals_type =
              to_array_type(dict_st.components()[2].type());
            member_exprt length{outer_base, "length", signedbv_typet{64}};
            member_exprt keys_arr{outer_base, "keys", keys_type};
            member_exprt vals_arr{outer_base, "values", vals_type};
            exprt outer_typed_key = outer_key;
            if(outer_typed_key.type() != keys_type.element_type())
              outer_typed_key =
                safe_typecast(outer_typed_key, keys_type.element_type());
            exprt outer_rhs = tmp_sym;
            if(outer_rhs.type() != vals_type.element_type())
              outer_rhs = safe_typecast(outer_rhs, vals_type.element_type());
            for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt in_range = binary_relation_exprt{idx, ID_lt, length};
              exprt match =
                equal_exprt{index_exprt{keys_arr, idx}, outer_typed_key};
              block.add(code_ifthenelset{
                and_exprt{in_range, match},
                code_frontend_assignt{index_exprt{vals_arr, idx}, outer_rhs}});
            }
          }
          else if(
            !outer_base.is_nil() && !outer_key.is_nil() &&
            is_python_list_type(outer_base.type()))
          {
            const auto &list_st = to_struct_type(outer_base.type());
            const auto &data_type =
              to_array_type(list_st.components()[1].type());
            member_exprt data{outer_base, "data", data_type};
            exprt outer_rhs = tmp_sym;
            if(outer_rhs.type() != data_type.element_type())
              outer_rhs = safe_typecast(outer_rhs, data_type.element_type());
            block.add(code_frontend_assignt{
              index_exprt{data, outer_key}, std::move(outer_rhs)});
          }
          continue;
        }
      }
      exprt obj = convert_expression(json_member(target, "value"));

      // Tuple assignment → raise TypeError
      if(!obj.is_nil() && is_python_tuple_type(obj.type()))
      {
        code_blockt type_error;
        const symbolt *exc_sym =
          symbol_table.lookup("python::__exception_active");
        const symbolt *exc_type_sym =
          symbol_table.lookup("python::__exception_type");
        if(exc_sym != nullptr)
        {
          type_error.add(
            code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
        }
        if(exc_type_sym != nullptr)
        {
          // TypeError hash
          long type_hash = exception_type_hash("TypeError");
          type_error.add(code_frontend_assignt{
            exc_type_sym->symbol_expr(),
            from_integer(type_hash, python_int_type())});
        }
        block.add(std::move(type_error));
        continue;
      }

      // Dict subscript assignment: d["key"] = value
      if(!obj.is_nil() && is_python_dict_type(obj.type()))
      {
        const jsont &slice_node = json_member(target, "slice");
        exprt key = convert_expression(slice_node);
        if(!key.is_nil())
        {
          const auto &dict_st = to_struct_type(obj.type());
          const auto &keys_type = to_array_type(dict_st.components()[1].type());
          const auto &vals_type = to_array_type(dict_st.components()[2].type());
          member_exprt length{obj, "length", signedbv_typet{64}};
          member_exprt keys_arr{obj, "keys", keys_type};
          member_exprt vals_arr{obj, "values", vals_type};
          exprt typed_key = key;
          if(typed_key.type() != keys_type.element_type())
            typed_key = safe_typecast(typed_key, keys_type.element_type());
          exprt typed_val = rhs;
          if(typed_val.type() != vals_type.element_type())
            typed_val = safe_typecast(typed_val, vals_type.element_type());
          static unsigned dict_assign_ctr = 0;
          std::string fn = "__dict_found_" + std::to_string(dict_assign_ctr++);
          std::string fq = qualify_name(fn);
          irep_idt fi{fq};
          if(symbol_table.lookup(fi) == nullptr)
          {
            symbolt fs{fi, bool_typet{}, "python"};
            fs.base_name = fn;
            fs.is_lvalue = true;
            fs.is_state_var = true;
            symbol_table.add(fs);
          }
          symbol_exprt found = symbol_table.lookup_ref(fi).symbol_expr();
          block.add(code_frontend_assignt{found, false_exprt{}});
          for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, length};
            exprt match = equal_exprt{index_exprt{keys_arr, idx}, typed_key};
            code_blockt update;
            update.add(
              code_frontend_assignt{index_exprt{vals_arr, idx}, typed_val});
            update.add(code_frontend_assignt{found, true_exprt{}});
            block.add(
              code_ifthenelset{and_exprt{in_range, match}, std::move(update)});
          }
          code_blockt append;
          append.add(
            code_frontend_assignt{index_exprt{keys_arr, length}, typed_key});
          append.add(
            code_frontend_assignt{index_exprt{vals_arr, length}, typed_val});
          append.add(code_frontend_assignt{
            length, plus_exprt{length, from_integer(1, signedbv_typet{64})}});
          block.add(code_ifthenelset{not_exprt{found}, std::move(append)});

          // dict_literals update: if obj is a symbol and the
          // assigned key is a compile-time constant string,
          // add the key to the tracked key-set. Later
          // subscript reads of the same key can then prove
          // the key exists. Value-tracking uses typed_val
          // when it's a constant, otherwise safe_zero.
          if(
            obj.id() == ID_symbol &&
            json_string(json_member(slice_node, "_type")) == "Constant")
          {
            auto key_str = extract_string_value(key);
            if(key_str.has_value())
            {
              irep_idt obj_id = to_symbol_expr(obj).get_identifier();
              auto dli = dict_literals.find(obj_id);
              if(dli != dict_literals.end())
              {
                // Append key to the literal's key array; grow length.
                exprt &dlit = dli->second;
                if(
                  dlit.id() == ID_struct && dlit.operands().size() >= 3 &&
                  dlit.operands()[0].is_constant())
                {
                  mp_integer cur_len;
                  if(!to_integer(to_constant_expr(dlit.operands()[0]), cur_len))
                  {
                    std::size_t idx = cur_len.to_ulong();
                    if(idx < PYTHON_MAX_DICT_SIZE)
                    {
                      // Check if the key already exists — if so,
                      // update the value in place, otherwise
                      // append. The dict_literals entry tracks
                      // BOTH keys and values; for the
                      // required-kwarg check (which cares only
                      // about key presence), losing the value
                      // tracking on a non-constant assignment is
                      // fine, but losing the KEY tracking would
                      // be incorrect — we KNOW the key exists.
                      // For non-constant values we therefore
                      // record the key and leave the value
                      // entry at whatever placeholder the dict
                      // literal already had (typically a
                      // safe_zero of the value-array element
                      // type from the original Dict literal
                      // construction in convert_dict).
                      bool have_key = false;
                      for(std::size_t j = 0; j < idx; j++)
                      {
                        auto ex = extract_string_value(
                          dlit.operands()[1].operands()[j]);
                        if(ex.has_value() && ex.value() == key_str.value())
                        {
                          have_key = true;
                          if(typed_val.is_constant())
                            dlit.operands()[2].operands()[j] = typed_val;
                          // else: leave existing value entry
                          // alone; key tracking is preserved.
                          break;
                        }
                      }
                      if(!have_key)
                      {
                        // Typecast the key literal to match
                        // the keys-array element type (the
                        // original convert_dict pass already
                        // typecast all keys to a uniform type
                        // — typically python_string but
                        // possibly tagged-union for
                        // dict[object, ...]).
                        const auto &keys_arr_type =
                          to_array_type(dlit.operands()[1].type());
                        exprt key_lit = python_string_literal(key_str.value());
                        if(key_lit.type() != keys_arr_type.element_type())
                          key_lit = safe_typecast(
                            key_lit, keys_arr_type.element_type());
                        dlit.operands()[1].operands()[idx] = key_lit;
                        if(typed_val.is_constant())
                          dlit.operands()[2].operands()[idx] = typed_val;
                        // else: leave value-array's existing
                        // entry (a safe_zero from the dict's
                        // construction) at this index; key
                        // tracking is preserved for the
                        // required-kwarg check.
                        dlit.operands()[0] =
                          from_integer(cur_len + 1, signedbv_typet{64});
                      }
                    }
                  }
                }
              }
            }
          }
          continue;
        }
      }
      if(!obj.is_nil() && is_python_list_type(obj.type()))
      {
        const jsont &slice_node = json_member(target, "slice");
        exprt idx = convert_expression(slice_node);
        const auto &list_st = to_struct_type(obj.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        member_exprt data{obj, "data", data_type};
        index_exprt lhs{data, idx};
        exprt typed_rhs = rhs;
        if(typed_rhs.type() != data_type.element_type())
          typed_rhs = typecast_exprt{typed_rhs, data_type.element_type()};
        code_frontend_assignt assign{lhs, typed_rhs};
        assign.add_source_location() = loc;
        block.add(std::move(assign));
        continue;
      }
    }

    // Handle attribute assignment: self.x = value
    if(is_node_type(target, "Attribute"))
    {
      exprt obj = convert_expression(json_member(target, "value"));
      std::string attr = json_string(json_member(target, "attr"));
      if(!obj.is_nil())
      {
        // If obj is a pointer (self in a method), dereference it
        typet obj_type = obj.type();
        if(obj_type.id() == ID_pointer)
        {
          const auto &base = to_pointer_type(obj_type).base_type();
          if(base.id() == ID_struct)
          {
            const auto &st = to_struct_type(base);
            if(st.has_component(attr))
            {
              member_exprt lhs{
                dereference_exprt{obj}, attr, st.get_component(attr).type()};
              exprt typed_rhs = rhs;
              if(typed_rhs.type() != lhs.type())
                typed_rhs = safe_typecast(typed_rhs, lhs.type());
              code_frontend_assignt assign{lhs, typed_rhs};
              assign.add_source_location() = loc;
              block.add(std::move(assign));
              continue;
            }
          }
        }
        else if(obj_type.id() == ID_struct)
        {
          const auto &st = to_struct_type(obj_type);
          if(st.has_component(attr))
          {
            member_exprt lhs{obj, attr, st.get_component(attr).type()};
            exprt typed_rhs = rhs;
            if(typed_rhs.type() != lhs.type())
              typed_rhs = safe_typecast(typed_rhs, lhs.type());
            code_frontend_assignt assign{lhs, typed_rhs};
            assign.add_source_location() = loc;
            block.add(std::move(assign));
            continue;
          }
        }
        // Tagged-union base: route through __class_ptr. Same
        // selection logic as convert_attribute's read path.
        else if(
          obj_type.id() == ID_struct_tag &&
          id2string(to_struct_tag_type(obj_type).get_identifier()) ==
            std::string{PYTHON_VALUE_TAG})
        {
          bool done = false;
          for(const auto &[cls_name, cls_type] : class_types)
          {
            if(!cls_type.has_component(attr))
              continue;
            const typet &field_type = cls_type.get_component(attr).type();
            exprt class_ptr = python_value_class_ptr(obj);
            pointer_typet cls_ptr_type{cls_type, 64};
            dereference_exprt deref{
              typecast_exprt{class_ptr, cls_ptr_type}, cls_type};
            member_exprt lhs{std::move(deref), attr, field_type};
            exprt typed_rhs = rhs;
            if(typed_rhs.type() != lhs.type())
              typed_rhs = safe_typecast(typed_rhs, lhs.type());
            code_frontend_assignt assign{lhs, typed_rhs};
            assign.add_source_location() = loc;
            block.add(std::move(assign));
            done = true;
            break;
          }
          if(done)
            continue;
        }
      }
    }

    std::string var_name = json_string(json_member(target, "id"));
    if(var_name.empty())
      continue; // Not a Name target — already handled above
    std::string qualified_name = qualify_name(var_name);
    irep_idt symbol_id{qualified_name};

    // Check if variable exists and has a different type (type change)
    const symbolt *existing = symbol_table.lookup(symbol_id);
    // Also check the versioned symbol
    auto ver_it = variable_versions.find(qualified_name);
    if(ver_it != variable_versions.end())
      existing = symbol_table.lookup(ver_it->second);

    bool rhs_has_side_effect = rhs.id() == ID_side_effect;

    if(
      existing != nullptr && existing->type != rhs.type() &&
      rhs.type().id() != ID_empty && !rhs.is_nil())
    {
      // If both types are numeric (int/float/bool), typecast the RHS
      // to match the existing variable's type. This avoids creating
      // versioned variables that cause type mismatches at merge points
      // (e.g., exception handlers).
      bool src_numeric =
        rhs.type().id() == ID_signedbv || rhs.type().id() == ID_floatbv ||
        rhs.type().id() == ID_bool || rhs.type().id() == ID_integer;
      bool tgt_numeric = existing->type.id() == ID_signedbv ||
                         existing->type.id() == ID_floatbv ||
                         existing->type.id() == ID_bool ||
                         existing->type.id() == ID_integer;
      // Also treat python_value_type → numeric as compatible
      // (unwrap the tagged union to the target type)
      if(is_python_value_type(rhs.type()) && tgt_numeric)
      {
        rhs = unwrap_value(rhs, existing->type);
      }
      else if(src_numeric && tgt_numeric)
      {
        rhs = safe_typecast(rhs, existing->type);
        // Fall through to normal assignment below
      }
      // numeric → python_value_type: wrap
      else if(src_numeric && is_python_value_type(existing->type))
      {
        rhs = wrap_value(rhs);
      }
      // python_value_type → struct: unwrap or nondet
      else if(
        is_python_value_type(rhs.type()) && existing->type.id() == ID_struct)
      {
        rhs = safe_typecast(rhs, existing->type);
      }
      // struct → different struct: version the variable for class types
      else if(
        rhs.type().id() == ID_struct && existing->type.id() == ID_struct &&
        rhs.type() != existing->type)
      {
        // For class instances (have __class_tag), create versioned variable
        if(
          to_struct_type(rhs.type()).has_component("__class_tag") ||
          to_struct_type(existing->type).has_component("__class_tag"))
        {
          unsigned &ver = version_counters[qualified_name];
          ver++;
          std::string versioned_name =
            qualified_name + "__v" + std::to_string(ver);
          irep_idt versioned_id{versioned_name};
          symbolt new_symbol{versioned_id, rhs.type(), "python"};
          new_symbol.base_name = var_name + "__v" + std::to_string(ver);
          new_symbol.location = loc;
          new_symbol.is_lvalue = true;
          new_symbol.is_state_var = true;
          symbol_table.add(new_symbol);
          variable_versions[qualified_name] = versioned_id;
          const symbolt &new_sym = symbol_table.lookup_ref(versioned_id);
          code_frontend_assignt assign{new_sym.symbol_expr(), rhs};
          assign.add_source_location() = loc;
          block.add(std::move(assign));
          continue;
        }
        rhs = safe_typecast(rhs, existing->type);
      }
      else if(if_else_depth > 0)
      {
        // Inside if/else: type change at a branch point.
        // Use python_value_type for the variable.
        // Create a tagged-union variable and wrap the value.
        typet val_type = python_value_type();
        unsigned &ver = version_counters[qualified_name];
        ver++;
        std::string versioned_name =
          qualified_name + "__v" + std::to_string(ver);
        irep_idt versioned_id{versioned_name};

        symbolt new_symbol{versioned_id, val_type, "python"};
        new_symbol.base_name = var_name + "__v" + std::to_string(ver);
        new_symbol.location = loc;
        new_symbol.is_lvalue = true;
        new_symbol.is_state_var = true;
        symbol_table.add(new_symbol);

        variable_versions[qualified_name] = versioned_id;

        // Wrap the value in a tagged union
        python_type_tagt tag = python_type_tagt::INT;
        if(rhs.type().id() == ID_floatbv)
          tag = python_type_tagt::FLOAT;
        else if(rhs.type().id() == ID_bool)
          tag = python_type_tagt::BOOL;

        struct_exprt wrapped = make_python_value(tag, rhs);
        const symbolt &new_sym = symbol_table.lookup_ref(versioned_id);
        code_frontend_assignt assign{new_sym.symbol_expr(), wrapped};
        assign.add_source_location() = loc;
        block.add(std::move(assign));
        continue;
      }
      else
      {
        // Straight-line code: create a fresh versioned symbol
        unsigned &ver = version_counters[qualified_name];
        ver++;
        std::string versioned_name =
          qualified_name + "__v" + std::to_string(ver);
        irep_idt versioned_id{versioned_name};

        symbolt new_symbol{versioned_id, rhs.type(), "python"};
        new_symbol.base_name = var_name + "__v" + std::to_string(ver);
        new_symbol.location = loc;
        new_symbol.is_lvalue = true;
        new_symbol.is_state_var = true;
        new_symbol.is_static_lifetime = current_function.empty();
        symbol_table.add(new_symbol);

        // Update the version mapping
        variable_versions[qualified_name] = versioned_id;

        const symbolt &new_sym = symbol_table.lookup_ref(versioned_id);
        code_frontend_assignt assign{new_sym.symbol_expr(), rhs};
        assign.add_source_location() = loc;
        block.add(std::move(assign));
        // Track string constants for versioned variables
        if(is_python_string_type(rhs.type()))
        {
          auto sv = extract_string_value(rhs);
          if(!sv.has_value() && rhs.id() == ID_symbol)
          {
            auto it =
              string_constants.find(to_symbol_expr(rhs).get_identifier());
            if(it != string_constants.end())
              sv = it->second;
          }
          if(sv.has_value())
            string_constants[versioned_id] = sv.value();
        }
        continue;
      }
    }

    if(symbol_table.lookup(symbol_id) == nullptr)
    {
      symbolt new_symbol{symbol_id, rhs.type(), "python"};
      new_symbol.base_name = var_name;
      new_symbol.location = loc;
      new_symbol.is_lvalue = true;
      new_symbol.is_state_var = true;
      new_symbol.is_static_lifetime = current_function.empty();
      symbol_table.add(new_symbol);
    }

    const symbolt &sym = symbol_table.lookup_ref(symbol_id);
    exprt typed_rhs = rhs;
    if(typed_rhs.type() != sym.type)
      typed_rhs = safe_typecast(typed_rhs, sym.type);

    // Inside a try block with a function call RHS: split into
    // call-into-temp + guarded-assign so that if the call raises,
    // the assignment is skipped.
    if(
      try_depth > 0 && (typed_rhs.id() == ID_side_effect ||
                        rhs_has_side_effect || !pending_checks.empty()))
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      if(exc_sym != nullptr)
      {
        // Evaluate the RHS (may contain function call)
        static unsigned try_tmp_counter = 0;
        std::string tmp_name = "__try_tmp_" + std::to_string(try_tmp_counter++);
        std::string tmp_qname = qualify_name(tmp_name);
        irep_idt tmp_id{tmp_qname};
        if(symbol_table.lookup(tmp_id) == nullptr)
        {
          symbolt tmp_sym{tmp_id, typed_rhs.type(), "python"};
          tmp_sym.base_name = tmp_name;
          tmp_sym.is_lvalue = true;
          tmp_sym.is_state_var = true;
          symbol_table.add(tmp_sym);
        }
        const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);

        // Flush pending_checks (e.g., TypeError from complex //)
        // BEFORE the temp assignment so exception flag is set first
        for(auto &check : pending_checks)
          block.add(std::move(check));
        pending_checks.clear();

        code_frontend_assignt eval{tmp_sym.symbol_expr(), typed_rhs};
        eval.add_source_location() = loc;
        block.add(std::move(eval));

        // Guard the actual assignment (typecast if needed)
        exprt assign_rhs = tmp_sym.symbol_expr();
        if(assign_rhs.type() != sym.type)
          assign_rhs = safe_typecast(assign_rhs, sym.type);
        code_frontend_assignt assign{sym.symbol_expr(), assign_rhs};
        assign.add_source_location() = loc;
        code_ifthenelset guarded{
          not_exprt{exc_sym->symbol_expr()}, std::move(assign)};
        block.add(std::move(guarded));
        continue;
      }
    }

    code_frontend_assignt assign{sym.symbol_expr(), typed_rhs};
    assign.add_source_location() = loc;
    // Track constant string values (invalidate if non-constant)
    if(is_python_string_type(typed_rhs.type()))
    {
      auto sv = extract_string_value(typed_rhs);
      if(!sv.has_value() && typed_rhs.id() == ID_symbol)
      {
        auto it =
          string_constants.find(to_symbol_expr(typed_rhs).get_identifier());
        if(it != string_constants.end())
          sv = it->second;
      }
      if(sv.has_value())
        string_constants[sym.name] = sv.value();
      else
        string_constants.erase(sym.name);
    }
    if(is_python_dict_type(typed_rhs.type()))
    {
      if(typed_rhs.id() == ID_struct)
      {
        dict_literals[sym.name] = typed_rhs;
        // Populate per-key static category from the original
        // AST. The struct exprt's value array has had all
        // values typecast to a single uniform type via
        // safe_typecast, which loses the original category.
        // The AST is the source of truth.
        if(is_node_type(value, "Dict"))
        {
          const jsont &keys = json_member(value, "keys");
          const jsont &vals = json_member(value, "values");
          if(keys.is_array() && vals.is_array())
          {
            std::map<std::string, std::string> cats;
            std::map<std::string, std::string> str_consts;
            auto kit = as_array(keys).begin();
            auto vit = as_array(vals).begin();
            for(; kit != as_array(keys).end() && vit != as_array(vals).end();
                ++kit, ++vit)
            {
              if(!is_node_type(*kit, "Constant"))
                continue;
              const jsont &kv = json_member(*kit, "value");
              if(!kv.is_string())
                continue;
              std::string cat = ast_value_category(*vit);
              if(!cat.empty())
                cats[kv.value] = cat;
              if(is_node_type(*vit, "Constant"))
              {
                const jsont &cval = json_member(*vit, "value");
                if(cval.is_string())
                  str_consts[kv.value] = cval.value;
              }
            }
            if(!cats.empty())
              dict_literal_value_categories[sym.name] = std::move(cats);
            if(!str_consts.empty())
              dict_literal_value_string_consts[sym.name] =
                std::move(str_consts);
          }
        }
      }
      else if(typed_rhs.id() == ID_side_effect)
      {
        // Inter-procedural dict-literal propagation: if the
        // RHS is a call to a function whose return-statement
        // we recorded as a dict literal, synthesize a
        // dict-struct with the known keys so the caller's
        // subscript / 'in' / len() operations can prove the
        // key exists.
        const auto &se = to_side_effect_expr(typed_rhs);
        if(
          se.get_statement() == ID_function_call && !se.operands().empty() &&
          se.operands()[0].id() == ID_symbol)
        {
          std::string callee =
            id2string(to_symbol_expr(se.operands()[0]).get_identifier());
          std::string p = "python::";
          if(callee.substr(0, p.size()) == p)
            callee = callee.substr(p.size());
          auto ki = function_returned_dict_keys.find(callee);
          if(ki != function_returned_dict_keys.end() && !ki->second.empty())
          {
            const auto &dt = to_struct_type(typed_rhs.type());
            const auto &keys_type = to_array_type(dt.components()[1].type());
            const auto &vals_type = to_array_type(dt.components()[2].type());
            exprt::operandst key_elems, val_elems;
            for(const auto &k : ki->second)
            {
              key_elems.push_back(python_string_literal(k));
              val_elems.push_back(safe_zero(vals_type.element_type()));
            }
            while(key_elems.size() < PYTHON_MAX_DICT_SIZE)
            {
              key_elems.push_back(safe_zero(keys_type.element_type()));
              val_elems.push_back(safe_zero(vals_type.element_type()));
            }
            exprt length = from_integer(
              static_cast<long long>(ki->second.size()), signedbv_typet{64});
            dict_literals[sym.name] = struct_exprt{
              {length,
               array_exprt{std::move(key_elems), keys_type},
               array_exprt{std::move(val_elems), vals_type}},
              typed_rhs.type()};
          }
          else
            dict_literals.erase(sym.name);
        }
        else
          dict_literals.erase(sym.name);
      }
      else
        dict_literals.erase(sym.name);
    }
    if(is_python_list_type(typed_rhs.type()))
    {
      if(typed_rhs.id() == ID_struct)
        list_literals[sym.name] = typed_rhs;
      else
        list_literals.erase(sym.name);
    }
    if(is_python_tuple_type(typed_rhs.type()))
    {
      if(typed_rhs.id() == ID_struct)
        tuple_literals[sym.name] = typed_rhs;
      else
        tuple_literals.erase(sym.name);
    }
    {
      auto ev = try_eval_double(typed_rhs);
      if(ev.has_value())
        float_constants[sym.name] = ev.value();
      else
        float_constants.erase(sym.name);
    }
    block.add(std::move(assign));
  }

  if(block.statements().size() == 1)
    return block.statements().front();
  return std::move(block);
}

// PLR §7.2.1: Augmented assignment statements
// "An augmented assignment evaluates the target and the expression list,
// performs the binary operation, and assigns the result to the target."
codet python_convertert::convert_aug_assign(const jsont &stmt)
{
  // x += expr  →  x = x + expr
  // Also handles: lst[i] += expr, self.attr += expr
  const jsont &target = json_member(stmt, "target");
  const jsont &op_node = json_member(stmt, "op");
  const jsont &value = json_member(stmt, "value");
  source_locationt loc = get_location(stmt);

  // PLR §7.2.2: augmented assignment evaluates the LHS
  // expression exactly once. For a[idx()] += ... the
  // index must be snapshotted so the read and write
  // share the same index value (rather than calling
  // idx() twice). If the index is a plain Name or
  // constant, no rewrite is needed.
  //
  // Strategy for the a[side_effect()] case: materialise
  // the index value into a temp via pending_checks and
  // construct an index_exprt from the target's container
  // expression and the temp, building both lhs (read) and
  // an assignment target referencing the same temp.
  exprt subscript_lhs;
  exprt subscript_write_lhs;
  bool have_subscript_rewrite = false;
  if(is_node_type(target, "Subscript"))
  {
    const jsont &slice = json_member(target, "slice");
    if(is_node_type(slice, "Call"))
    {
      exprt container = convert_expression(json_member(target, "value"));
      exprt idx_expr = convert_expression(slice);
      if(!container.is_nil() && !idx_expr.is_nil())
      {
        static unsigned auglidx_ctr = 0;
        std::string tmpn = "__auglidx_" + std::to_string(auglidx_ctr++);
        std::string tmpq = qualify_name(tmpn);
        irep_idt tmpid{tmpq};
        if(symbol_table.lookup(tmpid) == nullptr)
        {
          symbolt ts{tmpid, idx_expr.type(), "python"};
          ts.base_name = tmpn;
          ts.is_lvalue = true;
          ts.is_state_var = true;
          ts.is_static_lifetime = current_function.empty();
          symbol_table.add(ts);
        }
        symbol_exprt snap = symbol_table.lookup_ref(tmpid).symbol_expr();
        pending_checks.push_back(code_frontend_assignt{snap, idx_expr});
        // Build the indexed reads/writes using the snapshot.
        if(is_python_list_type(container.type()))
        {
          const auto &st = to_struct_type(container.type());
          typet elem_type =
            to_array_type(st.get_component("data").type()).element_type();
          member_exprt data_member{
            container, "data", to_array_type(st.get_component("data").type())};
          subscript_lhs = index_exprt{data_member, snap, elem_type};
          subscript_write_lhs = subscript_lhs;
          have_subscript_rewrite = true;
        }
      }
    }
  }

  // Determine the LHS expression based on target type.
  //
  // For dict-subscript targets we deliberately bypass
  // convert_subscript: that helper emits a KeyError check via
  // pending_checks for missing keys, which is correct for plain
  // d[k] reads but wrong for d[k] += rhs against a defaultdict
  // (where the read is allowed to return the factory's default).
  // We build the same chained `keys[i]==k ? values[i] : ... :
  // safe_zero` read inline so the KeyError check stays out of the
  // emitted GOTO. The follow-up store at the end of this function
  // performs an append for the missing-key case, matching
  // defaultdict's auto-insert semantics.
  exprt lhs;
  bool dict_subscript_aug = false;
  exprt dict_aug_container;
  exprt dict_aug_key;
  if(have_subscript_rewrite)
    lhs = subscript_lhs;
  else if(is_node_type(target, "Name"))
    lhs = convert_name(target);
  else if(is_node_type(target, "Subscript"))
  {
    exprt container_check = convert_expression(json_member(target, "value"));
    if(!container_check.is_nil() && is_python_dict_type(container_check.type()))
    {
      const auto &dict_st = to_struct_type(container_check.type());
      const auto &keys_type = to_array_type(dict_st.components()[1].type());
      const auto &vals_type = to_array_type(dict_st.components()[2].type());
      member_exprt length{container_check, "length", signedbv_typet{64}};
      member_exprt keys_arr{container_check, "keys", keys_type};
      member_exprt vals_arr{container_check, "values", vals_type};
      exprt key_expr = convert_expression(json_member(target, "slice"));
      if(key_expr.type() != keys_type.element_type())
        key_expr = safe_typecast(key_expr, keys_type.element_type());
      exprt result = safe_zero(vals_type.element_type());
      for(int i = PYTHON_MAX_DICT_SIZE - 1; i >= 0; i--)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        exprt in_range = binary_relation_exprt{idx, ID_lt, length};
        exprt key_i = index_exprt{keys_arr, idx};
        if(key_i.type() != key_expr.type())
          key_i = safe_typecast(key_i, key_expr.type());
        exprt match = equal_exprt{key_i, key_expr};
        result = if_exprt{
          and_exprt{in_range, match}, index_exprt{vals_arr, idx}, result};
      }
      lhs = std::move(result);
      dict_subscript_aug = true;
      dict_aug_container = std::move(container_check);
      dict_aug_key = std::move(key_expr);
    }
    else
      lhs = convert_subscript(target);
  }
  else if(is_node_type(target, "Attribute"))
    lhs = convert_attribute(target);
  else
  {
    log.error() << "Unsupported augmented assignment target" << messaget::eom;
    return code_skipt{};
  }

  exprt rhs = convert_expression(value);
  if(lhs.is_nil() || rhs.is_nil())
    return code_skipt{};

  std::string op = json_string(json_member(op_node, "_type"));

  // PLR §7.2.1: For string +=, use content-tracking concat
  if(
    op == "Add" && is_python_string_type(lhs.type()) &&
    is_python_string_type(rhs.type()))
  {
    // Constant-string optimization for +=
    {
      auto lv = extract_string_value(lhs);
      if(!lv.has_value() && lhs.id() == ID_symbol)
      {
        auto it = string_constants.find(to_symbol_expr(lhs).get_identifier());
        if(it != string_constants.end())
          lv = it->second;
      }
      auto rv = extract_string_value(rhs);
      if(!rv.has_value() && rhs.id() == ID_symbol)
      {
        auto it = string_constants.find(to_symbol_expr(rhs).get_identifier());
        if(it != string_constants.end())
          rv = it->second;
      }
      if(lv.has_value() && rv.has_value())
      {
        std::string result = lv.value() + rv.value();
        // Update tracking
        if(lhs.id() == ID_symbol)
          string_constants[to_symbol_expr(lhs).get_identifier()] = result;
        return code_frontend_assignt{lhs, python_string_literal(result)};
      }
    }
    // Non-constant: assign nondet
    return code_frontend_assignt{
      lhs, side_effect_expr_nondett{python_string_type(), source_locationt{}}};
    typet str_type = python_string_type();
    const auto &data_type = array_typet(
      unsignedbv_typet{8},
      from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
    member_exprt left_len{lhs, "length", signedbv_typet{64}};
    member_exprt right_len{rhs, "length", signedbv_typet{64}};
    member_exprt left_data{lhs, "data", data_type};
    member_exprt right_data{rhs, "data", data_type};

    static unsigned str_aug_counter = 0;
    std::string tmp_name = "__str_aug_" + std::to_string(str_aug_counter++);
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

    code_blockt block;
    block.add(code_frontend_assignt{
      member_exprt{tmp, "length", signedbv_typet{64}},
      plus_exprt{left_len, right_len}});

    member_exprt tmp_data{tmp, "data", data_type};
    for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      exprt val = if_exprt{
        binary_relation_exprt{idx, ID_lt, left_len},
        index_exprt{left_data, idx},
        index_exprt{right_data, minus_exprt{idx, left_len}}};
      block.add(code_frontend_assignt{index_exprt{tmp_data, idx}, val});
    }

    // Assign result back to target
    if(is_node_type(target, "Name"))
    {
      std::string var_name = json_string(json_member(target, "id"));
      std::string qname = qualify_name(var_name);
      const symbolt *sym = symbol_table.lookup(irep_idt{qname});
      if(sym != nullptr)
      {
        string_constants.erase(sym->name);
        block.add(code_frontend_assignt{sym->symbol_expr(), tmp});
        return std::move(block);
      }
    }
  }

  // Type promotion
  if(lhs.type() != rhs.type())
  {
    rhs = safe_typecast(rhs, lhs.type());
  }

  // Unwrap tagged unions for arithmetic
  exprt arith_lhs = lhs;
  if(is_python_value_type(arith_lhs.type()))
    arith_lhs = unwrap_value(
      arith_lhs, rhs.type().id() != ID_struct ? rhs.type() : python_int_type());
  if(is_python_value_type(rhs.type()))
    rhs = unwrap_value(rhs, arith_lhs.type());

  exprt new_rhs;
  // For string/list operations, build a synthetic BinOp JSON and use
  // convert_bin_op which handles concatenation and repetition
  if(
    (op == "Add" || op == "Mult") &&
    (is_python_string_type(lhs.type()) || is_python_list_type(lhs.type())))
  {
    // Create a temporary BinOp expression through convert_bin_op
    // by directly constructing the result
    if(
      op == "Add" && is_python_string_type(lhs.type()) &&
      is_python_string_type(rhs.type()))
    {
      // String concatenation
      typet str_type = python_string_type();
      const auto &data_type = array_typet(
        unsignedbv_typet{8},
        from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
      // Pointer-based string: return nondet for non-constant
      return code_skipt{};
      member_exprt left_len{lhs, "length", signedbv_typet{64}};
      member_exprt right_len{rhs, "length", signedbv_typet{64}};
      member_exprt left_data{lhs, "data", data_type};
      member_exprt right_data{rhs, "data", data_type};
      exprt new_len = plus_exprt{left_len, right_len};
      exprt::operandst chars;
      for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        chars.push_back(if_exprt{
          binary_relation_exprt{idx, ID_lt, left_len},
          index_exprt{left_data, idx},
          index_exprt{right_data, minus_exprt{idx, left_len}}});
      }
      new_rhs = struct_exprt{
        {new_len, array_exprt{std::move(chars), data_type}}, str_type};
    }
    else if(op == "Add" && is_python_list_type(lhs.type()))
    {
      // List concatenation
      const auto &list_st = to_struct_type(lhs.type());
      const auto &data_type = to_array_type(list_st.components()[1].type());
      member_exprt left_len{lhs, "length", signedbv_typet{64}};
      member_exprt right_len{rhs, "length", signedbv_typet{64}};
      member_exprt left_data{lhs, "data", data_type};
      member_exprt right_data{rhs, "data", data_type};
      exprt new_len = plus_exprt{left_len, right_len};
      exprt::operandst elems;
      for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        elems.push_back(if_exprt{
          binary_relation_exprt{idx, ID_lt, left_len},
          index_exprt{left_data, idx},
          index_exprt{right_data, minus_exprt{idx, left_len}}});
      }
      new_rhs = struct_exprt{
        {new_len, array_exprt{std::move(elems), data_type}}, lhs.type()};
    }
    else
    {
      // String/list repetition: s *= n
      // For now, return nondet (proper repetition needs unrolling)
      new_rhs = side_effect_expr_nondett{lhs.type(), loc};
    }
  }
  else if(op == "Add")
    new_rhs = plus_exprt{arith_lhs, rhs};
  else if(op == "Sub")
    new_rhs = minus_exprt{arith_lhs, rhs};
  else if(op == "Mult")
    new_rhs = mult_exprt{arith_lhs, rhs};
  else if(op == "FloorDiv")
    new_rhs = div_exprt{arith_lhs, rhs};
  else if(op == "Div")
  {
    // True division: result is float
    exprt fl = arith_lhs, fr = rhs;
    if(fl.type().id() != ID_floatbv)
      fl = typecast_exprt{fl, double_type()};
    if(fr.type().id() != ID_floatbv)
      fr = typecast_exprt{fr, double_type()};
    new_rhs = div_exprt{fl, fr};
  }
  else if(op == "Mod")
    new_rhs = mod_exprt{arith_lhs, rhs};
  else if(op == "BitOr")
    new_rhs = bitor_exprt{arith_lhs, rhs};
  else if(op == "BitAnd")
    new_rhs = bitand_exprt{arith_lhs, rhs};
  else if(op == "BitXor")
    new_rhs = bitxor_exprt{arith_lhs, rhs};
  else if(op == "LShift")
    new_rhs = shl_exprt{arith_lhs, rhs};
  else if(op == "RShift")
    new_rhs = ashr_exprt{arith_lhs, rhs};
  else if(op == "MatMult")
  {
    // See BinOp/MatMult note: model '@=' as scalar '*='; sound
    // over-approximation for non-scalar operands.
    if(arith_lhs.type() == rhs.type() && arith_lhs.type().id() != ID_struct)
      new_rhs = mult_exprt{arith_lhs, rhs};
    else
      new_rhs = side_effect_expr_nondett{arith_lhs.type(), loc};
  }
  else if(op == "Pow")
  {
    // x **= y — use constant evaluation if possible
    auto base_ev = try_eval_double(arith_lhs);
    auto exp_ev = try_eval_double(rhs);
    if(base_ev.has_value() && exp_ev.has_value())
    {
      double result_d = std::pow(base_ev.value(), exp_ev.value());
      if(arith_lhs.type().id() == ID_floatbv)
        new_rhs = double_to_floatbv(result_d);
      else
        new_rhs =
          from_integer(static_cast<long long>(result_d), arith_lhs.type());
    }
    else
    {
      // Fallback: if-then-else chain for small exponents
      new_rhs = arith_lhs; // x**1 as default
      for(int i = 16; i >= 1; i--)
      {
        exprt power = arith_lhs;
        for(int j = 1; j < i; j++)
          power = mult_exprt{power, arith_lhs};
        new_rhs = if_exprt{
          equal_exprt{rhs, from_integer(i, rhs.type())}, power, new_rhs};
      }
      new_rhs = if_exprt{
        equal_exprt{rhs, from_integer(0, rhs.type())},
        from_integer(1, arith_lhs.type()),
        new_rhs};
    }
  }
  else
  {
    log.warning() << "Unsupported augmented assignment operator: " << op
                  << messaget::eom;
    return code_skipt{};
  }

  // Wrap result back into tagged union if needed
  if(is_python_value_type(lhs.type()) && !is_python_value_type(new_rhs.type()))
    new_rhs = wrap_value(new_rhs);
  if(new_rhs.type() != lhs.type())
    new_rhs = safe_typecast(new_rhs, lhs.type());
  // Invalidate constant tracking for modified variables
  if(lhs.id() == ID_symbol)
  {
    irep_idt sid = to_symbol_expr(lhs).get_identifier();
    string_constants.erase(sid);
    dict_literals.erase(sid);
    float_constants.erase(sid);
    list_literals.erase(sid);
  }
  // Div-by-zero check for /= and //= and %=
  if(op == "Div" || op == "FloorDiv" || op == "Mod")
  {
    const symbolt *exc_sym = symbol_table.lookup("python::__exception_active");
    if(exc_sym != nullptr)
    {
      exprt divisor = rhs;
      exprt is_zero = equal_exprt{divisor, safe_zero(divisor.type())};
      pending_checks.push_back(code_ifthenelset{
        is_zero, code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}}});
    }
  }
  // Negative shift check for <<= and >>=
  if(op == "LShift" || op == "RShift")
  {
    const symbolt *exc_sym = symbol_table.lookup("python::__exception_active");
    if(exc_sym != nullptr)
    {
      exprt neg =
        binary_relation_exprt{rhs, ID_lt, from_integer(0, rhs.type())};
      pending_checks.push_back(code_ifthenelset{
        neg, code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}}});
    }
  }

  // Dict-subscript augmented assignment: d[key] += rhs.
  //
  // The lhs we built above is the read form: a chain of nested
  // (key_match[i] ? d.values[i] : ...) ending in a struct constant
  // default. Using that as a code_frontend_assignt LHS would have
  // CBMC's symex hit "l2_rename_rvalues case `struct' not handled"
  // on the leaf struct constant, which is not a writable location.
  //
  // Instead, follow the same pattern as the regular dict-subscript
  // assign in convert_assign: snapshot the new value, then iterate
  // over the dict's slots emitting `if(in_range && keys[i]==key)
  // values[i] = new_val` and append a new (key, new_val) entry if
  // the key wasn't found. This matches Python's defaultdict
  // semantics — d[k] += v on a missing key inserts (k, factory()+v).
  if(dict_subscript_aug)
  {
    code_blockt block;
    const auto &dict_st = to_struct_type(dict_aug_container.type());
    const auto &keys_type = to_array_type(dict_st.components()[1].type());
    const auto &vals_type = to_array_type(dict_st.components()[2].type());
    member_exprt length{dict_aug_container, "length", signedbv_typet{64}};
    member_exprt keys_arr{dict_aug_container, "keys", keys_type};
    member_exprt vals_arr{dict_aug_container, "values", vals_type};
    exprt typed_new_val = new_rhs;
    if(typed_new_val.type() != vals_type.element_type())
      typed_new_val = safe_typecast(typed_new_val, vals_type.element_type());

    static unsigned dict_aug_ctr = 0;
    std::string fn = "__dict_aug_found_" + std::to_string(dict_aug_ctr++);
    std::string fq = qualify_name(fn);
    irep_idt fi{fq};
    if(symbol_table.lookup(fi) == nullptr)
    {
      symbolt fs{fi, bool_typet{}, "python"};
      fs.base_name = fn;
      fs.is_lvalue = true;
      fs.is_state_var = true;
      symbol_table.add(fs);
    }
    symbol_exprt found = symbol_table.lookup_ref(fi).symbol_expr();
    block.add(code_frontend_assignt{found, false_exprt{}});
    for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      exprt in_range = binary_relation_exprt{idx, ID_lt, length};
      exprt match = equal_exprt{index_exprt{keys_arr, idx}, dict_aug_key};
      code_blockt update;
      update.add(
        code_frontend_assignt{index_exprt{vals_arr, idx}, typed_new_val});
      update.add(code_frontend_assignt{found, true_exprt{}});
      block.add(
        code_ifthenelset{and_exprt{in_range, match}, std::move(update)});
    }
    // defaultdict semantics: append (key, new_val) when missing.
    code_blockt append;
    append.add(
      code_frontend_assignt{index_exprt{keys_arr, length}, dict_aug_key});
    append.add(
      code_frontend_assignt{index_exprt{vals_arr, length}, typed_new_val});
    append.add(code_frontend_assignt{
      length, plus_exprt{length, from_integer(1, signedbv_typet{64})}});
    block.add(code_ifthenelset{not_exprt{found}, std::move(append)});
    block.add_source_location() = loc;
    return std::move(block);
  }

  code_frontend_assignt assign{lhs, new_rhs};
  assign.add_source_location() = loc;
  return std::move(assign);
}
