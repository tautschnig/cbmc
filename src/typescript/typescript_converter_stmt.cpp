/// \\file
/// TypeScript to GOTO converter — split implementation

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/floatbv_expr.h>
#include <util/ieee_float.h>
#include <util/irep.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol.h>

#include <goto-programs/goto_functions.h>

#include "typescript_converter.h"
#include "typescript_types.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>

codet typescript_convertert::convert_statement(const jsont &node)
{
  std::string kind = json_string(json_member(node, "_kind"));

  // ES2024 sec-variable-statement / sec-let-and-const-declarations
  if(kind == "FirstStatement" || kind == "VariableStatement")
    return convert_variable_statement(node);
  if(kind == "VariableDeclarationList")
  {
    // Wrap in a pseudo-statement for the variable handler
    // The for-loop initializer is a bare VariableDeclarationList
    jsont wrapper;
    // Create a wrapper with declarationList field
    // Actually, just handle it directly
    const jsont &declarations = json_member(node, "declarations");
    if(!declarations.is_array())
      return code_skipt{};
    code_blockt block;
    for(const auto &decl : to_json_array(declarations))
    {
      std::string var_name =
        json_string(json_member(json_member(decl, "name"), "text"));
      // Check for arrow/function expression BEFORE creating variable
      const jsont &init = json_member(decl, "initializer");
      if(init.is_object())
      {
        std::string init_kind = json_string(json_member(init, "_kind"));
        if(init_kind == "ArrowFunction" || init_kind == "FunctionExpression")
        {
          convert_function_declaration_with_name(init, var_name);
          continue;
        }
      }
      std::string ts_type = json_string(json_member(decl, "_type"));
      // For generic class types (e.g., Box<number>), eagerly specialize
      // so variable type can use the specialized struct
      if(ts_type.find('<') != std::string::npos)
      {
        auto angle = ts_type.find('<');
        std::string base = ts_type.substr(0, angle);
        std::string inner =
          ts_type.substr(angle + 1, ts_type.size() - angle - 2);
        while(!inner.empty() && inner[0] == ' ')
          inner.erase(0, 1);
        while(!inner.empty() && inner.back() == ' ')
          inner.pop_back();
        if(inner == "true" || inner == "false")
          inner = "boolean";
        else if(!inner.empty() && (std::isdigit(inner[0]) || inner[0] == '-'))
          inner = "number";
        std::string spec_name = base + "__" + inner;
        auto gen_it = generic_classes.find(base);
        if(
          gen_it != generic_classes.end() &&
          class_types.find(spec_name) == class_types.end())
        {
          // Extract type parameter name
          std::string tp_name = "T";
          const jsont &tp = json_member(gen_it->second, "typeParameters");
          if(tp.is_array() && !to_json_array(tp).empty())
          {
            const jsont &first_tp = *to_json_array(tp).begin();
            std::string n = json_string(json_member(first_tp, "_type"));
            if(!n.empty())
              tp_name = n;
          }
          jsont saved_node = gen_it->second;
          generic_classes.erase(gen_it);
          std::string saved_tp = current_generic_type_param;
          std::string saved_concrete = current_generic_concrete;
          current_generic_type_param = tp_name;
          current_generic_concrete = inner;
          // Rename in copy
          jsont modified = saved_node;
          jsont &name_obj = const_cast<jsont &>(json_member(modified, "name"));
          if(name_obj.is_object())
          {
            auto &obj = const_cast<json_objectt &>(to_json_object(name_obj));
            obj["text"] = json_stringt{spec_name};
          }
          convert_statement(modified);
          current_generic_type_param = saved_tp;
          current_generic_concrete = saved_concrete;
          generic_classes[base] = saved_node;
        }
      }
      typet var_type = convert_type(ts_type);
      // Integer inference: use narrower type if variable is integer-safe
      if(ts_type == "number" && integer_inference)
      {
        std::string scope_key =
          (current_function.empty() ? "" : current_function + "::") + var_name;
        typet inferred = number_type_for(scope_key);
        if(inferred.id() != ID_floatbv)
          var_type = inferred;
      }
      std::string qualified =
        "typescript::" +
        (current_function.empty() ? "" : current_function + "::") + var_name;
      irep_idt sym_id{qualified};
      {
        symbolt new_sym{sym_id, var_type, "typescript"};
        new_sym.base_name = var_name;
        new_sym.is_lvalue = true;
        new_sym.is_state_var = true;
        symbol_table.add(new_sym);
      }
      if(init.is_object())
      {
        exprt rhs = convert_expression(init);
        if(!rhs.is_nil())
        {
          const symbolt &sym = symbol_table.lookup_ref(sym_id);
          if(rhs.type() != sym.type)
          {
            // For struct-to-struct, reorder fields instead of typecast
            if(
              rhs.id() == ID_struct && rhs.type().id() == ID_struct &&
              sym.type.id() == ID_struct)
            {
              const auto &src_st = to_struct_type(rhs.type());
              const auto &tgt_st = to_struct_type(sym.type);
              // NaN default for numbers (our undefined sentinel), and
              // recursively-NaN-filled struct for nested structs.
              std::function<exprt(const typet &)> default_value;
              default_value = [&](const typet &t) -> exprt
              {
                if(t.id() == ID_struct)
                {
                  const auto &ss = to_struct_type(t);
                  exprt::operandst fs;
                  for(const auto &c : ss.components())
                    fs.push_back(default_value(c.type()));
                  return struct_exprt{std::move(fs), ss};
                }
                if(t.id() == ID_floatbv)
                {
                  ieee_floatt nan_val{
                    ieee_float_spect::double_precision(),
                    ieee_floatt::rounding_modet::ROUND_TO_EVEN};
                  nan_val.make_NaN();
                  return nan_val.to_expr();
                }
                if(t.id() == ID_bool)
                  return false_exprt{};
                if(t.id() == ID_signedbv || t.id() == ID_unsignedbv)
                  return from_integer(0, t);
                return side_effect_expr_nondett{t, source_locationt{}};
              };
              exprt::operandst reordered;
              bool all_present_or_fillable = true;
              for(const auto &tc : tgt_st.components())
              {
                bool found = false;
                for(std::size_t i = 0; i < src_st.components().size(); ++i)
                {
                  if(
                    src_st.components()[i].get_name() == tc.get_name() &&
                    i < rhs.operands().size())
                  {
                    reordered.push_back(rhs.operands()[i]);
                    found = true;
                    break;
                  }
                }
                if(!found)
                {
                  // Missing field: fill with NaN default (treated as
                  // undefined for optional fields; see §13.3.9).
                  reordered.push_back(default_value(tc.type()));
                }
              }
              (void)all_present_or_fillable;
              if(reordered.size() == tgt_st.components().size())
                rhs = struct_exprt{std::move(reordered), sym.type};
              else
                rhs = typecast_exprt(rhs, sym.type);
            }
            else
              rhs = typecast_exprt(rhs, sym.type);
          }
          // Flush pending stmts BEFORE assignment (constructor calls etc.)
          for(auto &s : pending_stmts)
            block.add(std::move(s));
          pending_stmts.clear();
          // Async threading: if this is an async CallExpression (Promise type)
          // and the flag is set, wrap the assignment in __CPROVER_ASYNC_N
          // label so CBMC's goto-conversion spawns a thread.
          bool wrap_async = false;
          if(async_threading && init.is_object())
          {
            std::string init_kind = json_string(json_member(init, "_kind"));
            std::string init_type = json_string(json_member(init, "_type"));
            if(
              init_kind == "CallExpression" &&
              init_type.find("Promise") != std::string::npos)
              wrap_async = true;
          }
          if(wrap_async)
          {
            static unsigned async_ctr = 0;
            std::string label_name =
              "__CPROVER_ASYNC_" + std::to_string(async_ctr++);
            code_blockt thread_body;
            thread_body.add(code_frontend_assignt{sym.symbol_expr(), rhs});
            block.add(code_labelt{label_name, std::move(thread_body)});
          }
          else
          {
            block.add(code_frontend_assignt{sym.symbol_expr(), rhs});
          }
          // Track string constants
          {
            std::string sv = extract_string_value(rhs);
            if(!sv.empty())
              string_constants[sym_id] = sv.substr(2);
          }
          // Set symbol value for constants (enables spread, template literals)
          {
            const exprt &val =
              rhs.id() == ID_typecast ? to_typecast_expr(rhs).op() : rhs;
            if(
              val.id() == ID_struct || val.is_constant() ||
              (val.id() == ID_symbol && val.type().id() == ID_struct))
            {
              symbolt *ws = symbol_table.get_writeable(sym_id);
              if(ws != nullptr)
                ws->value = val;
            }
          }
          // Closure binding: if RHS is a call to a function that returns
          // a closure, record the binding for later call resolution
          if(
            rhs.id() == ID_side_effect && var_type.id() == ID_pointer &&
            to_pointer_type(var_type).base_type().id() == ID_code)
          {
            // Find which function was called
            const jsont &init_node = json_member(decl, "initializer");
            if(is_kind(init_node, "CallExpression"))
            {
              std::string called_fn = json_string(
                json_member(json_member(init_node, "expression"), "text"));
              if(!called_fn.empty())
              {
                irep_idt called_id{"typescript::" + called_fn};
                const symbolt *called_sym = symbol_table.lookup(called_id);
                if(called_sym != nullptr)
                {
                  // Find which closure function it returns
                  // by checking captured_var_map for functions
                  // created during this function's conversion
                  for(const auto &[fn_id, captures] : captured_var_map)
                  {
                    if(id2string(fn_id).find("__anon_fn_") != std::string::npos)
                    {
                      // Match captured vars to called function's params
                      to_code_type(called_sym->type).parameters();
                      exprt::operandst bound_vals;
                      const jsont &call_args =
                        json_member(init_node, "arguments");
                      if(call_args.is_array())
                      {
                        std::size_t ai = 0;
                        for(const auto &a : to_json_array(call_args))
                        {
                          bound_vals.push_back(convert_expression(a));
                          ai++;
                        }
                      }
                      closure_bindings[sym_id] = {fn_id, bound_vals};
                      break;
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
    if(block.statements().size() == 1)
      return block.statements().front();
    return std::move(block);
  }

  if(kind == "ExpressionStatement")
    return convert_expression_statement(node);

  // ES2024 sec-if-statement
  if(kind == "IfStatement")
    return convert_if_statement(node);

  // ES2024 sec-while-statement
  if(kind == "WhileStatement")
    return convert_while_statement(node);

  // ES2024 sec-do-while-statement
  if(kind == "DoStatement")
  {
    exprt cond = convert_expression(json_member(node, "expression"));
    codet body = convert_statement(json_member(node, "statement"));
    return code_dowhilet{std::move(cond), std::move(body)};
  }
  // ES2024 sec-throw-statement
  if(kind == "ThrowStatement")
  {
    // Model throw as assume(false) — makes path unreachable
    return code_assumet{false_exprt{}};
  }
  // ES2024 sec-break-statement
  if(kind == "BreakStatement")
  {
    std::string label = json_string(json_member(node, "label"));
    if(!label.empty())
      return code_gotot{irep_idt{"__ts_label_" + label + "_end"}};
    return code_breakt{};
  }
  // ES2024 sec-continue-statement
  if(kind == "ContinueStatement")
  {
    std::string label = json_string(json_member(node, "label"));
    if(!label.empty())
      return code_gotot{irep_idt{"__ts_label_" + label + "_continue"}};
    return code_continuet{};
  }
  // ES2024 sec-labelled-statements
  if(kind == "LabeledStatement")
  {
    std::string label = json_string(json_member(node, "label"));
    const jsont &stmt = json_member(node, "statement");
    code_blockt block;
    // Add continue label before the loop
    block.add(
      code_labelt{irep_idt{"__ts_label_" + label + "_continue"}, code_skipt{}});
    block.add(convert_statement(stmt));
    // Add end label after the loop
    block.add(
      code_labelt{irep_idt{"__ts_label_" + label + "_end"}, code_skipt{}});
    return std::move(block);
  }
  // ES2024 sec-for-statement
  if(kind == "ForStatement")
    return convert_for_statement(node);

  // ES2024 sec-return-statement
  if(kind == "ReturnStatement")
    return convert_return_statement(node);

  if(kind == "Block")
    return convert_block(node);

  // ES2024 sec-function-definitions
  if(kind == "FunctionDeclaration")
  {
    convert_function_declaration(node);
    return code_skipt{};
  }

  // ES2024 sec-switch-statement
  if(kind == "SwitchStatement")
  {
    exprt disc = convert_expression(json_member(node, "expression"));
    const jsont &case_block = json_member(node, "caseBlock");
    const jsont &clauses = json_member(case_block, "clauses");
    if(disc.is_nil() || !clauses.is_array())
      return code_skipt{};
    // Convert to if-else chain (process default first, then cases in reverse)
    const auto &clause_arr = to_json_array(clauses);
    // Find default clause
    codet default_body = code_skipt{};
    for(const auto &clause : clause_arr)
    {
      if(json_string(json_member(clause, "_kind")) == "DefaultClause")
      {
        code_blockt body;
        const jsont &stmts = json_member(clause, "statements");
        if(stmts.is_array())
          for(const auto &s : to_json_array(stmts))
          {
            if(json_string(json_member(s, "_kind")) == "BreakStatement")
              continue;
            body.add(convert_statement(s));
          }
        default_body = std::move(body);
        break;
      }
    }
    // Build if-else chain from last case to first
    codet result = std::move(default_body);
    std::vector<std::reference_wrapper<const jsont>> cases;
    for(const auto &clause : clause_arr)
      if(json_string(json_member(clause, "_kind")) != "DefaultClause")
        cases.push_back(std::cref(clause));
    for(auto it = cases.rbegin(); it != cases.rend(); ++it)
    {
      const jsont &clause = it->get();
      code_blockt body;
      const jsont &stmts = json_member(clause, "statements");
      if(stmts.is_array())
        for(const auto &s : to_json_array(stmts))
        {
          if(json_string(json_member(s, "_kind")) == "BreakStatement")
            continue;
          body.add(convert_statement(s));
        }
      exprt case_val = convert_expression(json_member(clause, "expression"));
      if(!case_val.is_nil())
      {
        if(case_val.type() != disc.type())
          case_val = typecast_exprt{case_val, disc.type()};
        exprt cond = disc.type().id() == ID_floatbv
                       ? exprt{ieee_float_equal_exprt{disc, case_val}}
                       : exprt{equal_exprt{disc, case_val}};
        result = code_ifthenelset{cond, std::move(body), std::move(result)};
      }
    }
    return result;
  }

  // TSH: Enums
  if(kind == "EnumDeclaration")
  {
    std::string enum_name =
      json_string(json_member(json_member(node, "name"), "text"));
    const jsont &members = json_member(node, "members");
    if(members.is_array())
    {
      int value = 0;
      for(const auto &m : to_json_array(members))
      {
        std::string mname =
          json_string(json_member(json_member(m, "name"), "text"));
        std::string qn = "typescript::" + enum_name + "." + mname;
        irep_idt sid{qn};
        // Check for explicit initializer and detect string-typed init.
        const jsont &init = json_member(m, "initializer");
        bool has_string_init = false;
        std::string string_val;
        if(init.is_object())
        {
          exprt val = convert_expression(init);
          // String initializer → string enum member.
          if(is_typescript_string_type(val.type()))
          {
            has_string_init = true;
            std::string raw = extract_string_value(val);
            if(!raw.empty())
              string_val = raw.substr(2); // strip "S:" prefix
          }
          else if(val.is_constant())
          {
            if(val.type().id() == ID_floatbv)
            {
              ieee_floatt fv{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              fv.from_expr(to_constant_expr(val));
              value = static_cast<int>(std::stod(fv.to_ansi_c_string()));
            }
            else
            {
              mp_integer iv;
              if(!to_integer(to_constant_expr(val), iv))
                value = iv.to_long();
            }
          }
        }
        // Create symbol: EnumName.MemberName = value
        if(symbol_table.lookup(sid) == nullptr)
        {
          if(has_string_init)
          {
            // String enum member: store as typescript_string.
            symbolt s{sid, typescript_string_type(), "typescript"};
            s.base_name = enum_name + "." + mname;
            s.is_lvalue = true;
            s.is_state_var = true;
            s.is_static_lifetime = true;
            s.value = convert_string_literal_from_text(string_val);
            symbol_table.add(s);
            // Track the value so member access can resolve it.
            string_constants[sid] = string_val;
          }
          else
          {
            symbolt s{sid, double_type(), "typescript"};
            s.base_name = enum_name + "." + mname;
            s.is_lvalue = true;
            s.is_state_var = true;
            s.is_static_lifetime = true;
            // Store as float constant
            uint64_t bits;
            double dval = static_cast<double>(value);
            std::memcpy(&bits, &dval, sizeof(bits));
            s.value = constant_exprt{
              integer2bvrep(mp_integer{std::to_string(bits).c_str()}, 64),
              double_type()};
            symbol_table.add(s);
          }
        }
        value++;
      }
    }
    // Generate initialization code for enum members
    code_blockt enum_init;
    for(const auto &m : to_json_array(members))
    {
      std::string mname =
        json_string(json_member(json_member(m, "name"), "text"));
      std::string qn = "typescript::" + enum_name + "." + mname;
      const symbolt *s = symbol_table.lookup(irep_idt{qn});
      if(s != nullptr && !s->value.is_nil())
        enum_init.add(code_frontend_assignt{s->symbol_expr(), s->value});
    }
    return std::move(enum_init);
  }
  // TSH: Object Types.md — interface declarations (type-only, no runtime code)
  if(kind == "InterfaceDeclaration" || kind == "TypeAliasDeclaration")
  {
    // Register interface type in class_types for convert_type lookup
    if(kind == "InterfaceDeclaration")
    {
      std::string iname =
        json_string(json_member(json_member(node, "name"), "text"));
      const jsont &imembers = json_member(node, "members");
      if(!iname.empty() && imembers.is_array())
      {
        struct_typet itype;
        for(const auto &m : to_json_array(imembers))
        {
          std::string mname =
            json_string(json_member(json_member(m, "name"), "text"));
          std::string mtype_str = json_string(json_member(m, "_type"));
          if(!mname.empty() && !mtype_str.empty())
            itype.components().push_back(
              struct_typet::componentt{mname, convert_type(mtype_str)});
        }
        if(!itype.components().empty())
          class_types[iname] = itype;
      }
    }
    // Register type alias: resolve RHS type and store under alias name
    if(kind == "TypeAliasDeclaration")
    {
      std::string aname =
        json_string(json_member(json_member(node, "name"), "text"));
      // Prefer resolvedType (Pick, Omit, etc. expanded by type checker)
      // Fall back to aliasedType (source text, preserves unions)
      std::string atype = json_string(json_member(node, "resolvedType"));
      if(atype.empty() || atype == aname)
        atype = json_string(json_member(node, "aliasedType"));
      if(atype.empty())
        atype = json_string(json_member(node, "_type"));
      if(!aname.empty() && !atype.empty() && aname != atype)
      {
        typet resolved = convert_type(atype);
        if(resolved.id() == ID_struct)
          class_types[aname] = to_struct_type(resolved);
      }
    }
    return code_skipt{};
  }

  // ES2024 sec-class-definitions
  // TSH: Classes > Class Members, Inheritance, Static Members
  if(kind == "ClassDeclaration")
  {
    std::string cls_name =
      json_string(json_member(json_member(node, "name"), "text"));
    if(cls_name.empty())
      return code_skipt{};
    // Detect generic classes — defer for monomorphization
    const jsont &type_params = json_member(node, "typeParameters");
    if(
      type_params.is_array() && !to_json_array(type_params).empty() &&
      current_generic_concrete.empty())
    {
      generic_classes[cls_name] = node;
      return code_skipt{};
    }
    // Build struct type from property declarations
    struct_typet cls_type;
    cls_type.set_tag("typescript_class_" + cls_name);

    // Check for inheritance: class Dog extends Animal
    std::string parent_name;
    const jsont &heritage = json_member(node, "heritage");
    if(heritage.is_array())
    {
      for(const auto &clause : to_json_array(heritage))
      {
        const jsont &children = json_member(clause, "_children");
        if(children.is_array())
        {
          for(const auto &child : to_json_array(children))
          {
            const jsont &ch2 = json_member(child, "_children");
            if(ch2.is_array())
            {
              for(const auto &id : to_json_array(ch2))
              {
                std::string t = json_string(json_member(id, "text"));
                if(!t.empty())
                  parent_name = t;
              }
            }
          }
        }
      }
    }
    // Copy parent fields
    if(!parent_name.empty())
    {
      parent_class[cls_name] = parent_name;
      auto pit = class_types.find(parent_name);
      if(pit != class_types.end())
      {
        for(const auto &comp : pit->second.components())
          cls_type.components().push_back(comp);
      }
    }

    const jsont &members = json_member(node, "members");
    if(members.is_array())
    {
      for(const auto &m : to_json_array(members))
      {
        std::string mk = json_string(json_member(m, "_kind"));
        if(mk == "PropertyDeclaration")
        {
          std::string pname =
            json_string(json_member(json_member(m, "name"), "text"));
          // Strip # prefix from private field names
          if(!pname.empty() && pname[0] == '#')
            pname = pname.substr(1);
          std::string ptype = json_string(json_member(m, "_type"));
          cls_type.components().push_back(
            struct_typet::componentt{pname, convert_type(ptype)});
          // Track private fields
          if(json_member(m, "isPrivate").is_true())
            private_fields[cls_name].insert(pname);
        }
        // ES2024 §15.7.13 (Class Constructors): parameter-property
        // shorthand. A Constructor parameter declared with a modifier
        // (public / private / protected / readonly) is ALSO a field
        // of the class. The TypeScript compiler normally emits an
        // implicit `this.<name> = <name>` in the constructor body
        // during lowering to JavaScript; our converter reads the AST
        // directly so we add both the field and the assignment
        // ourselves (assignment is emitted below in the Constructor
        // member branch).
        else if(mk == "Constructor")
        {
          const jsont &params = json_member(m, "parameters");
          if(params.is_array())
          {
            for(const auto &p : to_json_array(params))
            {
              const jsont &modifiers = json_member(p, "modifiers");
              if(!modifiers.is_array())
                continue;
              bool has_access_mod = false;
              bool is_priv = false;
              for(const auto &mod : to_json_array(modifiers))
              {
                std::string mk2 = json_string(json_member(mod, "_kind"));
                if(
                  mk2 == "PublicKeyword" || mk2 == "PrivateKeyword" ||
                  mk2 == "ProtectedKeyword" || mk2 == "ReadonlyKeyword")
                {
                  has_access_mod = true;
                  if(mk2 == "PrivateKeyword")
                    is_priv = true;
                }
              }
              if(!has_access_mod)
                continue;
              std::string pname =
                json_string(json_member(json_member(p, "name"), "text"));
              if(pname.empty())
                continue;
              std::string ptype = json_string(json_member(p, "_type"));
              cls_type.components().push_back(
                struct_typet::componentt{pname, convert_type(ptype)});
              if(is_priv)
                private_fields[cls_name].insert(pname);
            }
          }
        }
      }
    }
    // Register class type
    class_types[cls_name] = cls_type;
    std::string saved_class = current_class;
    current_class = cls_name;
    // Check if class has a constructor
    bool has_constructor = false;
    if(members.is_array())
    {
      for(const auto &m : to_json_array(members))
        if(json_string(json_member(m, "_kind")) == "Constructor")
          has_constructor = true;
    }
    // If no constructor, generate one that applies property initializers
    if(!has_constructor && members.is_array())
    {
      std::string ctor_name = cls_name + "::__init__";
      code_typet::parameterst ctor_params;
      code_typet::parametert this_param{pointer_typet{cls_type, 64}};
      this_param.set_identifier("typescript::" + ctor_name + "::this");
      this_param.set_base_name("this");
      ctor_params.push_back(this_param);
      code_typet ctor_type{ctor_params, empty_typet{}};
      irep_idt ctor_id{"typescript::" + ctor_name};
      symbolt ctor_sym{ctor_id, ctor_type, "typescript"};
      ctor_sym.base_name = ctor_name;
      // Create this parameter symbol
      {
        irep_idt tid{"typescript::" + ctor_name + "::this"};
        symbolt ts{tid, pointer_typet{cls_type, 64}, "typescript"};
        ts.base_name = "this";
        ts.is_parameter = true;
        ts.is_lvalue = true;
        ts.is_state_var = true;
        if(symbol_table.lookup(tid) == nullptr)
          symbol_table.add(ts);
      }
      // Body: assign initializers
      code_blockt body;
      symbol_exprt this_sym{
        "typescript::" + ctor_name + "::this", pointer_typet{cls_type, 64}};
      for(const auto &m : to_json_array(members))
      {
        if(json_string(json_member(m, "_kind")) != "PropertyDeclaration")
          continue;
        const jsont &init = json_member(m, "initializer");
        if(!init.is_object())
          continue;
        std::string pname =
          json_string(json_member(json_member(m, "name"), "text"));
        exprt val = convert_expression(init);
        if(val.is_nil())
          continue;
        if(cls_type.has_component(pname))
        {
          typet comp_type = cls_type.component_type(pname);
          if(val.type() != comp_type)
            val = typecast_exprt{val, comp_type};
          body.add(code_frontend_assignt{
            member_exprt{dereference_exprt{this_sym}, pname, comp_type}, val});
        }
      }
      ctor_sym.value = std::move(body);
      if(symbol_table.lookup(ctor_id) == nullptr)
        symbol_table.add(ctor_sym);
    }
    // Process constructor and methods
    if(members.is_array())
    {
      for(const auto &m : to_json_array(members))
      {
        std::string mk = json_string(json_member(m, "_kind"));
        if(mk == "Constructor")
        {
          // Constructor: cls_name::__init__(this_ptr, params...)
          std::string ctor_name = cls_name + "::__init__";
          code_typet::parameterst params;
          // this pointer
          code_typet::parametert this_param{pointer_typet{cls_type, 64}};
          this_param.set_identifier("typescript::" + ctor_name + "::this");
          this_param.set_base_name("this");
          params.push_back(this_param);
          // other params
          const jsont &ctor_params = json_member(m, "parameters");
          if(ctor_params.is_array())
          {
            for(const auto &p : to_json_array(ctor_params))
            {
              std::string pn =
                json_string(json_member(json_member(p, "name"), "text"));
              std::string pt = json_string(json_member(p, "_type"));
              code_typet::parametert cp{convert_type(pt)};
              cp.set_identifier("typescript::" + ctor_name + "::" + pn);
              cp.set_base_name(pn);
              params.push_back(cp);
            }
          }
          code_typet ft{params, empty_typet{}};
          irep_idt fid{"typescript::" + ctor_name};
          symbolt fs{fid, ft, "typescript"};
          fs.base_name = ctor_name;
          fs.is_lvalue = true;
          // Create param symbols
          for(const auto &p : params)
          {
            irep_idt pid = p.get_identifier();
            if(symbol_table.lookup(pid) == nullptr)
            {
              symbolt ps{pid, p.type(), "typescript"};
              ps.base_name = id2string(p.get_base_name());
              ps.is_parameter = true;
              ps.is_lvalue = true;
              ps.is_state_var = true;
              symbol_table.add(ps);
            }
          }
          // Convert body
          const jsont &body = json_member(m, "body");
          if(body.is_object())
          {
            std::string saved = current_function;
            current_function = ctor_name;
            // Prepend implicit `this.<name> = <name>` assignments
            // for each parameter-property (public/private/protected/
            // readonly) — see ES2024 §15.7.13. The TS compiler emits
            // these during lowering to JS; our direct-AST converter
            // needs to do it explicitly.
            code_blockt prologue;
            const jsont &ctor_params_for_props = json_member(m, "parameters");
            if(ctor_params_for_props.is_array())
            {
              symbol_exprt this_sym{
                "typescript::" + ctor_name + "::this",
                pointer_typet{cls_type, 64}};
              for(const auto &p : to_json_array(ctor_params_for_props))
              {
                const jsont &modifiers = json_member(p, "modifiers");
                if(!modifiers.is_array())
                  continue;
                bool has_access_mod = false;
                for(const auto &mod : to_json_array(modifiers))
                {
                  std::string mk2 = json_string(json_member(mod, "_kind"));
                  if(
                    mk2 == "PublicKeyword" || mk2 == "PrivateKeyword" ||
                    mk2 == "ProtectedKeyword" || mk2 == "ReadonlyKeyword")
                  {
                    has_access_mod = true;
                    break;
                  }
                }
                if(!has_access_mod)
                  continue;
                std::string pname =
                  json_string(json_member(json_member(p, "name"), "text"));
                if(pname.empty() || !cls_type.has_component(pname))
                  continue;
                typet ft_type = cls_type.component_type(pname);
                symbol_exprt p_sym{
                  "typescript::" + ctor_name + "::" + pname, ft_type};
                prologue.add(code_frontend_assignt{
                  member_exprt{dereference_exprt{this_sym}, pname, ft_type},
                  p_sym});
              }
            }
            codet body_code = convert_block(body);
            if(!prologue.statements().empty())
            {
              // Splice prologue in front of the body's statements.
              code_blockt combined = std::move(prologue);
              if(body_code.get_statement() == ID_block)
              {
                for(auto &s : to_code_block(body_code).statements())
                  combined.add(std::move(s));
              }
              else
              {
                combined.add(std::move(body_code));
              }
              fs.value = std::move(combined);
            }
            else
            {
              fs.value = std::move(body_code);
            }
            current_function = saved;
          }
          if(symbol_table.lookup(fid) == nullptr)
            symbol_table.add(fs);
        }
        else if(
          mk == "MethodDeclaration" || mk == "GetAccessor" ||
          mk == "SetAccessor")
        {
          std::string mname =
            json_string(json_member(json_member(m, "name"), "text"));
          bool is_setter = json_member(m, "isSetter").is_true();
          std::string full_name =
            cls_name + "::" + (is_setter ? "__set_" : "") + mname;
          std::string ret_str = json_string(json_member(m, "_returnType"));
          typet ret_type =
            ret_str.empty() ? empty_typet{} : convert_type(ret_str);
          bool is_static = json_member(m, "isStatic").is_true();
          code_typet::parameterst params;
          if(!is_static)
          {
            code_typet::parametert this_param{pointer_typet{cls_type, 64}};
            this_param.set_identifier("typescript::" + full_name + "::this");
            this_param.set_base_name("this");
            params.push_back(this_param);
          }
          const jsont &mparams = json_member(m, "parameters");
          if(mparams.is_array())
          {
            for(const auto &p : to_json_array(mparams))
            {
              std::string pn =
                json_string(json_member(json_member(p, "name"), "text"));
              std::string pt = json_string(json_member(p, "_type"));
              code_typet::parametert cp{convert_type(pt)};
              cp.set_identifier("typescript::" + full_name + "::" + pn);
              cp.set_base_name(pn);
              params.push_back(cp);
            }
          }
          code_typet ft{params, ret_type};
          irep_idt fid{"typescript::" + full_name};
          symbolt fs{fid, ft, "typescript"};
          fs.base_name = full_name;
          fs.is_lvalue = true;
          for(const auto &p : params)
          {
            irep_idt pid = p.get_identifier();
            if(symbol_table.lookup(pid) == nullptr)
            {
              symbolt ps{pid, p.type(), "typescript"};
              ps.base_name = id2string(p.get_base_name());
              ps.is_parameter = true;
              ps.is_lvalue = true;
              ps.is_state_var = true;
              symbol_table.add(ps);
            }
          }
          const jsont &body = json_member(m, "body");
          if(body.is_object())
          {
            std::string saved = current_function;
            current_function = full_name;
            fs.value = convert_block(body);
            current_function = saved;
          }
          if(symbol_table.lookup(fid) == nullptr)
            symbol_table.add(fs);
        }
      }
    }
    current_class = saved_class;
    return code_skipt{};
  }

  // ES2024 sec-for-in-and-for-of-statements
  // TSH: Iterators and Generators
  if(kind == "ForInStatement")
  {
    // for (const key in obj) - unroll for constant objects
    const jsont &expr_node = json_member(node, "expression");
    exprt obj = convert_expression(expr_node);
    if(obj.id() == ID_symbol)
    {
      const symbolt *s =
        symbol_table.lookup(to_symbol_expr(obj).get_identifier());
      if(s && !s->value.is_nil())
        obj = s->value;
    }
    const jsont &init = json_member(node, "initializer");
    std::string var_name;
    if(is_kind(init, "VariableDeclarationList"))
    {
      const jsont &decls = json_member(init, "declarations");
      if(decls.is_array() && !to_json_array(decls).empty())
        var_name = json_string(json_member(
          json_member(*to_json_array(decls).begin(), "name"), "text"));
    }
    if(var_name.empty())
      return code_skipt{};
    std::string qname =
      "typescript::" +
      (current_function.empty() ? "" : current_function + "::") + var_name;
    irep_idt var_id{qname};
    if(symbol_table.lookup(var_id) == nullptr)
    {
      symbolt vs{var_id, typescript_string_type(), "typescript"};
      vs.base_name = var_name;
      vs.is_lvalue = true;
      vs.is_state_var = true;
      symbol_table.add(vs);
    }
    code_blockt block;
    if(obj.type().id() == ID_struct)
    {
      const auto &st = to_struct_type(obj.type());
      for(const auto &comp : st.components())
      {
        std::string key = id2string(comp.get_name());
        block.add(code_frontend_assignt{
          symbol_exprt{var_id, typescript_string_type()},
          convert_string_literal_from_text(key)});
        string_constants[var_id] = key;
        block.add(convert_statement(json_member(node, "statement")));
      }
    }
    return std::move(block);
  }
  if(kind == "ForOfStatement")
  {
    // Convert: for(const x of arr) { body }
    // → let __i = 0; while(__i < arr.length) { const x = arr.data[__i]; body; __i++; }
    code_blockt block;
    // Get the array expression
    exprt arr = convert_expression(json_member(node, "expression"));
    if(arr.is_nil())
      return code_skipt{};

    // Detect Map/Set iteration
    bool is_map = false;
    bool is_set = false;
    bool is_string = false;
    if(arr.type().id() == ID_struct)
    {
      const auto &tag = to_struct_type(arr.type()).get_tag();
      is_map = (tag == "typescript_class_Map");
      is_set = (tag == "typescript_class_Set");
      is_string = (tag == "typescript_string");
    }
    // ES2024 §22.1.3.@@iterator: for-of on a string yields each
    // UTF-16 code point as a single-character string. We don't
    // currently model character-level iteration over our refined-
    // string receiver; skip the loop (emit a no-op) rather than
    // emitting a malformed body that crashes in simplify_member.
    // Documented limitation — user code can rewrite via explicit
    // indexing (`for(let i = 0; i < s.length; i++) s.charAt(i)`)
    // when string iteration is needed.
    if(is_string)
    {
      log.warning() << "for-of on a string receiver is not supported; "
                    << "use indexed iteration (`for (let i = 0; i < s.length; "
                    << "i++) s.charAt(i)`) instead. Treating as a no-op."
                    << messaget::eom;
      return code_skipt{};
    }

    // Create iterator variable
    static unsigned forit_ctr = 0;
    std::string it_name = "__forit_" + std::to_string(forit_ctr++);
    std::string it_qname =
      "typescript::" +
      (current_function.empty() ? "" : current_function + "::") + it_name;
    irep_idt it_id{it_qname};
    if(symbol_table.lookup(it_id) == nullptr)
    {
      symbolt it_sym{it_id, signedbv_typet{64}, "typescript"};
      it_sym.base_name = it_name;
      it_sym.is_lvalue = true;
      it_sym.is_state_var = true;
      symbol_table.add(it_sym);
    }
    symbol_exprt it_var = symbol_table.lookup_ref(it_id).symbol_expr();
    block.add(
      code_frontend_assignt{it_var, from_integer(0, signedbv_typet{64})});

    // Get loop variable name(s)
    const jsont &init_node = json_member(node, "initializer");
    std::string loop_var;
    std::string loop_var2; // for Map destructuring [key, value]
    if(is_kind(init_node, "VariableDeclarationList"))
    {
      const jsont &decls = json_member(init_node, "declarations");
      if(decls.is_array() && !to_json_array(decls).empty())
      {
        const jsont &decl0 = *to_json_array(decls).begin();
        const jsont &name_node = json_member(decl0, "name");
        if(is_kind(name_node, "ArrayBindingPattern") && is_map)
        {
          // [key, value] destructuring for Map
          const jsont &elems = json_member(name_node, "elements");
          if(elems.is_array())
          {
            auto it2 = to_json_array(elems).begin();
            if(it2 != to_json_array(elems).end())
            {
              loop_var =
                json_string(json_member(json_member(*it2, "name"), "text"));
              ++it2;
              if(it2 != to_json_array(elems).end())
                loop_var2 =
                  json_string(json_member(json_member(*it2, "name"), "text"));
            }
          }
        }
        else
        {
          loop_var = json_string(json_member(name_node, "text"));
        }
      }
    }

    // Determine element type and create loop variable(s)
    typet elem_type = double_type();
    typet key_type = typescript_string_type();
    typet val_type = double_type();
    if(!is_map && !is_set && arr.type().id() == ID_struct)
    {
      const auto &st = to_struct_type(arr.type());
      if(st.has_component("data"))
        elem_type =
          to_array_type(st.get_component("data").type()).element_type();
    }
    if(is_set)
      elem_type = double_type();

    // Create loop variable symbol(s)
    auto create_sym = [&](const std::string &name, const typet &type)
    {
      std::string qn =
        "typescript::" +
        (current_function.empty() ? "" : current_function + "::") + name;
      irep_idt id{qn};
      if(symbol_table.lookup(id) == nullptr)
      {
        symbolt sym{id, type, "typescript"};
        sym.base_name = name;
        sym.is_lvalue = true;
        sym.is_state_var = true;
        symbol_table.add(sym);
      }
      return id;
    };

    irep_idt lv_id{""};
    irep_idt lv2_id{""};
    if(!loop_var.empty())
    {
      if(is_map && !loop_var2.empty())
      {
        lv_id = create_sym(loop_var, key_type);
        lv2_id = create_sym(loop_var2, val_type);
      }
      else
      {
        lv_id = create_sym(loop_var, is_set ? elem_type : elem_type);
      }
    }

    // Build while loop
    exprt arr_len = member_exprt{arr, "size", signedbv_typet{64}};
    if(!is_map && !is_set)
      arr_len = member_exprt{arr, "length", signedbv_typet{64}};
    exprt cond = binary_relation_exprt{it_var, ID_lt, arr_len};
    code_blockt loop_body;

    if(!loop_var.empty())
    {
      if(is_map)
      {
        // Map: key = keys[i], value = values[i]
        const auto &mst = to_struct_type(arr.type());
        exprt keys_arr =
          member_exprt{arr, "keys", mst.get_component("keys").type()};
        exprt vals_arr =
          member_exprt{arr, "values", mst.get_component("values").type()};
        loop_body.add(code_frontend_assignt{
          symbol_table.lookup_ref(lv_id).symbol_expr(),
          index_exprt{keys_arr, it_var}});
        if(!loop_var2.empty())
          loop_body.add(code_frontend_assignt{
            symbol_table.lookup_ref(lv2_id).symbol_expr(),
            index_exprt{vals_arr, it_var}});
      }
      else if(is_set)
      {
        // Set: item = data[i]
        const auto &sst = to_struct_type(arr.type());
        exprt data_arr =
          member_exprt{arr, "data", sst.get_component("data").type()};
        loop_body.add(code_frontend_assignt{
          symbol_table.lookup_ref(lv_id).symbol_expr(),
          index_exprt{data_arr, it_var}});
      }
      else
      {
        // Array: x = data[i]
        const auto &st = to_struct_type(arr.type());
        if(st.has_component("data"))
        {
          exprt data =
            member_exprt{arr, "data", st.get_component("data").type()};
          loop_body.add(code_frontend_assignt{
            symbol_table.lookup_ref(lv_id).symbol_expr(),
            index_exprt{data, it_var}});
        }
      }
    }
    loop_body.add(convert_statement(json_member(node, "statement")));
    loop_body.add(code_frontend_assignt{
      it_var, plus_exprt{it_var, from_integer(1, signedbv_typet{64})}});
    block.add(code_whilet{cond, std::move(loop_body)});
    return std::move(block);
  }
  // ES2024 sec-try-statement
  if(kind == "TryStatement")
  {
    code_blockt block;
    // Convert try block
    const jsont &try_block = json_member(node, "tryBlock");
    if(try_block.is_object())
      block.add(convert_block(try_block));
    // Convert catch clause (simplified: always execute catch after try)
    const jsont &catch_clause = json_member(node, "catchClause");
    if(catch_clause.is_object())
    {
      const jsont &catch_block = json_member(catch_clause, "block");
      if(catch_block.is_object())
      {
        // Create catch variable if present
        const jsont &var_decl =
          json_member(catch_clause, "variableDeclaration");
        if(var_decl.is_object())
        {
          std::string vname =
            json_string(json_member(json_member(var_decl, "name"), "text"));
          if(!vname.empty())
          {
            std::string qn =
              "typescript::" +
              (current_function.empty() ? "" : current_function + "::") + vname;
            if(symbol_table.lookup(irep_idt{qn}) == nullptr)
            {
              symbolt vs{irep_idt{qn}, double_type(), "typescript"};
              vs.base_name = vname;
              vs.is_lvalue = true;
              vs.is_state_var = true;
              symbol_table.add(vs);
            }
          }
        }
        block.add(convert_block(catch_block));
      }
    }
    // Convert finally block
    const jsont &finally_block = json_member(node, "finallyBlock");
    if(finally_block.is_object())
      block.add(convert_block(finally_block));
    return std::move(block);
  }
  // ES2024 sec-throw-statement
  if(kind == "ThrowStatement")
    return code_skipt{}; // simplified: throw is a no-op for now
  // ES2024 sec-imports: ImportDeclaration — skip (handled by parser)
  if(
    kind == "ImportDeclaration" || kind == "ExportDeclaration" ||
    kind == "ExportAssignment")
    return code_skipt{};
  log.warning() << "Unsupported statement kind: " << kind
                << " (not yet implemented in TypeScript frontend); "
                << "skipping" << messaget::eom;
  return code_skipt{};
}

// ES2024 sec-let-and-const-declarations
codet typescript_convertert::convert_variable_statement(const jsont &node)
{
  const jsont &decl_list = json_member(node, "declarationList");
  const jsont &declarations = json_member(decl_list, "declarations");

  if(!declarations.is_array())
    return code_skipt{};

  code_blockt block;
  for(const auto &decl : to_json_array(declarations))
  {
    const jsont &name_node = json_member(decl, "name");
    std::string name_kind = json_string(json_member(name_node, "_kind"));
    // Handle destructuring: const { x, y } = point
    if(name_kind == "ObjectBindingPattern")
    {
      const jsont &init = json_member(decl, "initializer");
      if(!init.is_object())
        continue;
      exprt rhs = convert_expression(init);
      if(rhs.is_nil())
        continue;
      const jsont &elements = json_member(name_node, "elements");
      if(elements.is_array() && rhs.type().id() == ID_struct)
      {
        const auto &st = to_struct_type(rhs.type());
        for(const auto &elem : to_json_array(elements))
        {
          // TSH: Destructuring > Property Renaming
          // The TypeScript AST emits `name` as the binding target
          // (local variable) and optionally `propertyName` as the
          // source property when the user writes `{ a: renamed }`.
          // If `propertyName` is absent, source and target share the
          // same name (e.g. `{ a }`).
          std::string target =
            json_string(json_member(json_member(elem, "name"), "text"));
          const jsont &pn = json_member(elem, "propertyName");
          std::string source =
            pn.is_object() ? json_string(json_member(pn, "text")) : target;
          if(target.empty())
            continue;
          // Default-value initializer: `{ y: _y = 99 }` — the AST has
          // `initializer` on the BindingElement. Used when the source
          // property is absent OR its value is undefined (our model:
          // NaN).
          const jsont &elem_init = json_member(elem, "initializer");
          bool has_source = st.has_component(source);
          if(!has_source && !elem_init.is_object())
            continue;
          typet pt =
            has_source ? st.get_component(source).type() : double_type();
          exprt rhs_val;
          if(has_source)
          {
            exprt src_field = member_exprt{rhs, source, pt};
            if(elem_init.is_object())
            {
              exprt default_val = convert_expression(elem_init);
              // If field is NaN (our undefined sentinel): use default.
              if(pt.id() == ID_floatbv && default_val.type().id() == ID_floatbv)
              {
                // src_field !== src_field (IEEE NaN check) ? default : src
                exprt is_nan = ieee_float_notequal_exprt{src_field, src_field};
                rhs_val = if_exprt{is_nan, default_val, src_field};
              }
              else
              {
                rhs_val = std::move(src_field);
              }
            }
            else
            {
              rhs_val = std::move(src_field);
            }
          }
          else if(elem_init.is_object())
          {
            rhs_val = convert_expression(elem_init);
            pt = rhs_val.type();
          }
          std::string qn =
            "typescript::" +
            (current_function.empty() ? "" : current_function + "::") + target;
          irep_idt pid{qn};
          if(symbol_table.lookup(pid) == nullptr)
          {
            symbolt ps{pid, pt, "typescript"};
            ps.base_name = target;
            ps.is_lvalue = true;
            ps.is_state_var = true;
            ps.is_static_lifetime = current_function.empty();
            symbol_table.add(ps);
          }
          block.add(code_frontend_assignt{
            symbol_table.lookup_ref(pid).symbol_expr(), std::move(rhs_val)});
        }
      }
      continue;
    }
    // Handle array destructuring: const [a, b] = arr
    if(name_kind == "ArrayBindingPattern")
    {
      const jsont &init = json_member(decl, "initializer");
      if(!init.is_object())
        continue;
      exprt rhs = convert_expression(init);
      if(rhs.is_nil())
        continue;
      const jsont &elements = json_member(name_node, "elements");
      if(elements.is_array() && rhs.type().id() == ID_struct)
      {
        const auto &st = to_struct_type(rhs.type());
        if(st.has_component("data"))
        {
          exprt data =
            member_exprt{rhs, "data", st.get_component("data").type()};
          // Get source array length
          exprt src_len = member_exprt{rhs, "length", signedbv_typet{64}};
          mp_integer arr_len{0};
          if(
            rhs.id() == ID_struct && !rhs.operands().empty() &&
            rhs.operands()[0].is_constant())
            to_integer(to_constant_expr(rhs.operands()[0]), arr_len);
          std::size_t idx = 0;
          for(const auto &elem : to_json_array(elements))
          {
            std::string ename =
              json_string(json_member(json_member(elem, "name"), "text"));
            if(ename.empty())
            {
              idx++;
              continue;
            }
            bool is_rest = json_member(elem, "isRest").is_true();
            std::string qn =
              "typescript::" +
              (current_function.empty() ? "" : current_function + "::") + ename;
            irep_idt eid{qn};
            if(is_rest)
            {
              // Rest element: create sub-array from idx to end
              typet et =
                to_array_type(st.get_component("data").type()).element_type();
              std::size_t rest_len = arr_len > mp_integer(idx)
                                       ? (arr_len - mp_integer(idx)).to_ulong()
                                       : 0;
              std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
              array_typet rest_arr_type{
                et, from_integer(max_len, signedbv_typet{64})};
              struct_typet rest_type;
              rest_type.components().push_back(
                struct_typet::componentt{"length", signedbv_typet{64}});
              rest_type.components().push_back(
                struct_typet::componentt{"data", rest_arr_type});
              rest_type.set_tag("typescript_array");
              if(symbol_table.lookup(eid) == nullptr)
              {
                symbolt es{eid, rest_type, "typescript"};
                es.base_name = ename;
                es.is_lvalue = true;
                es.is_state_var = true;
                es.is_static_lifetime = current_function.empty();
                symbol_table.add(es);
              }
              // Build rest array struct
              exprt::operandst rest_elts;
              for(std::size_t ri = 0; ri < max_len; ++ri)
              {
                if(ri < rest_len)
                  rest_elts.push_back(index_exprt{
                    data, from_integer(idx + ri, signedbv_typet{64})});
                else
                  rest_elts.push_back(from_integer(0, et));
              }
              array_exprt rest_data{std::move(rest_elts), rest_arr_type};
              struct_exprt rest_val{
                {from_integer(rest_len, signedbv_typet{64}),
                 std::move(rest_data)},
                rest_type};
              block.add(code_frontend_assignt{
                symbol_table.lookup_ref(eid).symbol_expr(),
                std::move(rest_val)});
              // Store value for downstream resolution
              symbolt *ws = symbol_table.get_writeable(eid);
              if(ws)
                ws->value = symbol_table.lookup_ref(eid).symbol_expr();
            }
            else
            {
              typet et =
                to_array_type(st.get_component("data").type()).element_type();
              if(symbol_table.lookup(eid) == nullptr)
              {
                symbolt es{eid, et, "typescript"};
                es.base_name = ename;
                es.is_lvalue = true;
                es.is_state_var = true;
                es.is_static_lifetime = current_function.empty();
                symbol_table.add(es);
              }
              block.add(code_frontend_assignt{
                symbol_table.lookup_ref(eid).symbol_expr(),
                index_exprt{data, from_integer(idx, signedbv_typet{64})}});
            }
            idx++;
          }
        }
      }
      continue;
    }
    std::string var_name = json_string(json_member(name_node, "text"));
    // Check for arrow/function expression BEFORE creating variable
    const jsont &init_check = json_member(decl, "initializer");
    if(init_check.is_object())
    {
      std::string ik = json_string(json_member(init_check, "_kind"));
      if(ik == "ArrowFunction" || ik == "FunctionExpression")
      {
        convert_function_declaration_with_name(init_check, var_name);
        continue;
      }
    }
    std::string ts_type = json_string(json_member(decl, "_type"));
    // For generic class types, eagerly specialize
    if(ts_type.find('<') != std::string::npos)
    {
      auto angle = ts_type.find('<');
      std::string base = ts_type.substr(0, angle);
      std::string inner = ts_type.substr(angle + 1, ts_type.size() - angle - 2);
      while(!inner.empty() && inner[0] == ' ')
        inner.erase(0, 1);
      while(!inner.empty() && inner.back() == ' ')
        inner.pop_back();
      if(inner == "true" || inner == "false")
        inner = "boolean";
      else if(!inner.empty() && (std::isdigit(inner[0]) || inner[0] == '-'))
        inner = "number";
      std::string spec_name = base + "__" + inner;
      auto gen_it = generic_classes.find(base);
      if(
        gen_it != generic_classes.end() &&
        class_types.find(spec_name) == class_types.end())
      {
        std::string tp_name = "T";
        const jsont &tp = json_member(gen_it->second, "typeParameters");
        if(tp.is_array() && !to_json_array(tp).empty())
        {
          const jsont &first_tp = *to_json_array(tp).begin();
          std::string n = json_string(json_member(first_tp, "_type"));
          if(!n.empty())
            tp_name = n;
        }
        jsont saved_node = gen_it->second;
        generic_classes.erase(gen_it);
        std::string saved_tp = current_generic_type_param;
        std::string saved_concrete = current_generic_concrete;
        current_generic_type_param = tp_name;
        current_generic_concrete = inner;
        jsont modified = saved_node;
        jsont &name_obj = const_cast<jsont &>(json_member(modified, "name"));
        if(name_obj.is_object())
        {
          auto &obj = const_cast<json_objectt &>(to_json_object(name_obj));
          obj["text"] = json_stringt{spec_name};
        }
        convert_statement(modified);
        current_generic_type_param = saved_tp;
        current_generic_concrete = saved_concrete;
        generic_classes[base] = saved_node;
      }
    }
    typet var_type = convert_type(ts_type);
    // Integer inference
    if(ts_type == "number" && integer_inference)
    {
      std::string scope_key =
        (current_function.empty() ? "" : current_function + "::") + var_name;
      typet inferred = number_type_for(scope_key);
      if(inferred.id() != ID_floatbv)
        var_type = inferred;
    }

    std::string qualified =
      "typescript::" +
      (current_function.empty() ? "" : current_function + "::") + var_name;
    irep_idt sym_id{qualified};

    {
      symbolt new_sym{sym_id, var_type, "typescript"};
      new_sym.base_name = var_name;
      new_sym.location = get_location(decl);
      new_sym.is_lvalue = true;
      new_sym.is_state_var = true;
      new_sym.is_static_lifetime = current_function.empty();
      symbol_table.add(new_sym);
    }

    const jsont &init = json_member(decl, "initializer");
    if(!init.is_null() && !init.is_object())
    {
      // No initializer
    }
    else if(init.is_object())
    {
      exprt rhs = convert_expression(init);
      if(!rhs.is_nil())
      {
        const symbolt &sym = symbol_table.lookup_ref(sym_id);
        // Function → function-pointer: wrap in address_of, not
        // typecast. `const f = foo; f()` needs f to hold the
        // address of foo so the indirect call dispatches. A bare
        // typecast of the code-typed symbol to code* leaves the
        // value undefined from symex's perspective and the call
        // reduces to a dereference-without-call.
        if(
          rhs.type().id() == ID_code && sym.type.id() == ID_pointer &&
          to_pointer_type(sym.type).base_type().id() == ID_code)
        {
          rhs = address_of_exprt{rhs};
        }
        if(rhs.type() != sym.type)
        {
          // If target is a union and source is a scalar, construct union struct
          if(
            sym.type.id() == ID_struct &&
            to_struct_type(sym.type).get_tag() == "typescript_union")
          {
            const auto &ust = to_struct_type(sym.type);
            // Find matching variant
            int tag = -1;
            for(std::size_t c = 1; c < ust.components().size(); ++c)
            {
              if(ust.components()[c].type() == rhs.type())
              {
                tag = static_cast<int>(c - 1);
                break;
              }
            }
            if(tag >= 0)
            {
              // Build union value
              exprt::operandst ops;
              ops.push_back(from_integer(tag, signedbv_typet{32}));
              for(std::size_t c = 1; c < ust.components().size(); ++c)
              {
                if(static_cast<int>(c - 1) == tag)
                  ops.push_back(rhs);
                else
                  ops.push_back(side_effect_expr_nondett{
                    ust.components()[c].type(), source_locationt{}});
              }
              rhs = struct_exprt{std::move(ops), sym.type};
            }
          }
          if(rhs.type() != sym.type)
          {
            if(
              rhs.id() == ID_struct && rhs.type().id() == ID_struct &&
              sym.type.id() == ID_struct)
            {
              const auto &src_st = to_struct_type(rhs.type());
              const auto &tgt_st = to_struct_type(sym.type);
              std::function<exprt(const typet &)> default_value;
              default_value = [&](const typet &t) -> exprt
              {
                if(t.id() == ID_struct)
                {
                  const auto &ss = to_struct_type(t);
                  exprt::operandst fs;
                  for(const auto &c : ss.components())
                    fs.push_back(default_value(c.type()));
                  return struct_exprt{std::move(fs), ss};
                }
                if(t.id() == ID_floatbv)
                {
                  ieee_floatt nan_val{
                    ieee_float_spect::double_precision(),
                    ieee_floatt::rounding_modet::ROUND_TO_EVEN};
                  nan_val.make_NaN();
                  return nan_val.to_expr();
                }
                if(t.id() == ID_bool)
                  return false_exprt{};
                if(t.id() == ID_signedbv || t.id() == ID_unsignedbv)
                  return from_integer(0, t);
                // Unknown / unsupported: use a nondet value.
                return side_effect_expr_nondett{t, source_locationt{}};
              };
              exprt::operandst reordered;
              for(const auto &tc : tgt_st.components())
              {
                bool found = false;
                for(std::size_t i = 0; i < src_st.components().size(); ++i)
                {
                  if(
                    src_st.components()[i].get_name() == tc.get_name() &&
                    i < rhs.operands().size())
                  {
                    reordered.push_back(rhs.operands()[i]);
                    found = true;
                    break;
                  }
                }
                if(!found)
                  reordered.push_back(default_value(tc.type()));
              }
              if(reordered.size() == tgt_st.components().size())
                rhs = struct_exprt{std::move(reordered), sym.type};
              else
                rhs = typecast_exprt{rhs, sym.type};
            }
            else
              rhs = typecast_exprt{rhs, sym.type};
          }
        }
        // Flush pending stmts (constructor calls from NewExpression)
        for(auto &s : pending_stmts)
          block.add(std::move(s));
        pending_stmts.clear();

        // Closure-binding detection: if `rhs` is a call to an outer
        // function whose body creates (and returns) an inner arrow
        // function that captures outer parameters, our
        // parameter-lifting approach would have given the inner
        // function an extra parameter per captured variable. The
        // caller's `var_type` (the declared type of the lhs) still
        // has the unlifted signature, so a direct assignment
        // `lhs = rhs` fails symex's type-consistency invariant
        // (LHS is `ptr(code(x) -> r)`, RHS is `ptr(code(x, f, g) ->
        // r)`).
        //
        // Rather than force a function-pointer typecast (symex
        // doesn't handle those), we record the mapping in
        // `closure_bindings`: lhs → (inner function id, captured
        // argument values). At call sites, the call dispatcher in
        // typescript_converter_call.cpp looks up this map and
        // rewrites `lhs(args)` into
        // `inner_fn(args, captured_values)`. We also SKIP the
        // regular ASSIGN so symex never sees the inconsistent
        // types. This works because lhs is only used via call
        // syntax in the closure-return patterns we support (see
        // regression/typescript/higher-order-compose,
        // verify-closure-adder).
        bool is_closure_binding = false;
        if(
          rhs.id() == ID_side_effect && init.is_object() &&
          is_kind(init, "CallExpression"))
        {
          const jsont &call_expr = json_member(init, "expression");
          std::string called_fn = json_string(json_member(call_expr, "text"));
          if(!called_fn.empty())
          {
            irep_idt called_id{"typescript::" + called_fn};
            const symbolt *called_sym = symbol_table.lookup(called_id);
            if(called_sym != nullptr)
            {
              // Look for an inner function (in captured_var_map) that
              // appears to be returned from `called_fn`. Heuristic:
              // any __anon_fn_* whose captured vars are parameters of
              // `called_fn`. We break on the first match, which is
              // fragile for multiple nested closures but handles the
              // common single-return-arrow case.
              const auto &called_params =
                to_code_type(called_sym->type).parameters();
              for(const auto &[fn_id, captures] : captured_var_map)
              {
                if(id2string(fn_id).find("__anon_fn_") == std::string::npos)
                  continue;
                // Check that the captures reference the outer
                // function's parameters.
                bool all_captured_are_caller_params = true;
                for(const auto &[cv_name, cv_outer_id] : captures)
                {
                  bool found = false;
                  for(const auto &p : called_params)
                  {
                    if(id2string(p.get_base_name()) == cv_name)
                    {
                      found = true;
                      break;
                    }
                  }
                  if(!found)
                  {
                    all_captured_are_caller_params = false;
                    break;
                  }
                }
                if(!all_captured_are_caller_params || captures.empty())
                  continue;
                // Build the bound-values list by matching capture
                // names to the call-site argument positions in order
                // of the caller's parameters.
                const jsont &call_args = json_member(init, "arguments");
                if(!call_args.is_array())
                  break;
                exprt::operandst bound_vals;
                for(const auto &[cv_name, cv_outer_id] : captures)
                {
                  std::size_t idx = 0;
                  bool matched = false;
                  for(const auto &p : called_params)
                  {
                    if(id2string(p.get_base_name()) == cv_name)
                    {
                      matched = true;
                      break;
                    }
                    idx++;
                  }
                  if(!matched)
                  {
                    bound_vals.clear();
                    break;
                  }
                  auto arg_array = to_json_array(call_args);
                  if(idx >= arg_array.size())
                  {
                    bound_vals.clear();
                    break;
                  }
                  auto it = arg_array.begin();
                  std::advance(it, idx);
                  bound_vals.push_back(convert_expression(*it));
                }
                if(bound_vals.size() == captures.size())
                {
                  closure_bindings[sym_id] = {fn_id, bound_vals};
                  is_closure_binding = true;
                }
                break;
              }
            }
          }
        }
        if(is_closure_binding)
        {
          // Symbol exists in the table; no direct ASSIGN emitted.
          // Call sites resolve via closure_bindings above.
          continue;
        }

        // Async threading: wrap Promise-returning CallExpression in
        // __CPROVER_ASYNC_N label so CBMC spawns a thread for it.
        bool wrap_async = false;
        if(async_threading && init.is_object())
        {
          std::string init_kind = json_string(json_member(init, "_kind"));
          std::string init_type = json_string(json_member(init, "_type"));
          if(
            init_kind == "CallExpression" &&
            init_type.find("Promise") != std::string::npos)
            wrap_async = true;
        }
        code_frontend_assignt assign{sym.symbol_expr(), rhs};
        assign.add_source_location() = get_location(decl);
        if(wrap_async)
        {
          static unsigned async_ctr = 0;
          std::string label_name =
            "__CPROVER_ASYNC_" + std::to_string(async_ctr++);
          code_blockt thread_body;
          thread_body.add(std::move(assign));
          block.add(code_labelt{label_name, std::move(thread_body)});
        }
        else
        {
          block.add(std::move(assign));
        }
        // Propagate frozen status through aliases: if the RHS is a
        // symbol that's in frozen_symbols (e.g.
        //   const alias = Object.freeze(o)
        // because Object.freeze returns its argument), mark the new
        // binding as frozen too. ES2024 §20.1.2.7.
        {
          const exprt &val =
            rhs.id() == ID_typecast ? to_typecast_expr(rhs).op() : rhs;
          if(
            val.id() == ID_symbol &&
            frozen_symbols.count(to_symbol_expr(val).get_identifier()) > 0)
          {
            frozen_symbols.insert(sym_id);
          }
        }
        // Track string constants (works with refined_string_exprt)
        if(
          is_typescript_string_type(rhs.type()) && rhs.id() == ID_struct &&
          rhs.operands().size() >= 2 && rhs.operands()[0].is_constant())
        {
          mp_integer len;
          if(!to_integer(to_constant_expr(rhs.operands()[0]), len))
          {
            // refined_string: {length, address_of(arr[0])}
            const exprt &ptr = rhs.operands()[1];
            if(
              ptr.id() == ID_address_of && ptr.operands()[0].id() == ID_index &&
              ptr.operands()[0].operands()[0].id() == ID_symbol)
            {
              irep_idt aid = to_symbol_expr(ptr.operands()[0].operands()[0])
                               .get_identifier();
              const symbolt *as = symbol_table.lookup(aid);
              if(as && !as->value.is_nil())
              {
                std::string sv;
                for(mp_integer i = 0; i < len; ++i)
                {
                  auto idx = i.to_ulong();
                  if(
                    idx < as->value.operands().size() &&
                    as->value.operands()[idx].is_constant())
                  {
                    mp_integer ch;
                    if(!to_integer(
                         to_constant_expr(as->value.operands()[idx]), ch))
                      sv += static_cast<char>(ch.to_ulong());
                  }
                }
                string_constants[sym_id] = sv;
              }
            }
          }
        }
        // Set symbol value for constants (enables spread, template literals)
        {
          const exprt &val =
            rhs.id() == ID_typecast ? to_typecast_expr(rhs).op() : rhs;
          if(
            val.id() == ID_struct || val.is_constant() ||
            (val.id() == ID_symbol && val.type().id() == ID_struct))
          {
            symbolt *ws = symbol_table.get_writeable(sym_id);
            if(ws != nullptr)
              ws->value = val;
          }
        }
      }
    }
  }

  if(block.statements().size() == 1)
    return block.statements().front();
  return std::move(block);
}

codet typescript_convertert::convert_expression_statement(const jsont &node)
{
  const jsont &expr_node = json_member(node, "expression");
  std::string expr_kind = json_string(json_member(expr_node, "_kind"));
  if(expr_kind == "CallExpression")
  {
    const jsont &callee_node = json_member(expr_node, "expression");
    std::string callee_kind = json_string(json_member(callee_node, "_kind"));
    if(callee_kind == "PropertyAccessExpression")
    {
      const jsont &obj_node = json_member(callee_node, "expression");
      std::string obj_text = json_string(json_member(obj_node, "text"));
      const jsont &method_node = json_member(callee_node, "name");
      std::string method_text = json_string(json_member(method_node, "text"));
      if(obj_text == "console" && method_text == "log")
        return code_skipt{};
      if(obj_text == "console" && method_text == "assert")
      {
        const jsont &call_args = json_member(expr_node, "arguments");
        if(call_args.is_array())
        {
          const auto &arr = to_json_array(call_args);
          if(!arr.empty())
          {
            exprt cond = convert_expression(*arr.begin());
            if(!cond.is_nil())
            {
              if(cond.type().id() != ID_bool)
                cond = typecast_exprt{cond, bool_typet{}};
              code_assertt assertion{cond};
              assertion.add_source_location() = get_location(expr_node);
              if(!pending_stmts.empty())
              {
                code_blockt blk;
                for(auto &s : pending_stmts)
                  blk.add(std::move(s));
                pending_stmts.clear();
                blk.add(std::move(assertion));
                return std::move(blk);
              }
              return std::move(assertion);
            }
          }
        }
        return code_skipt{};
      }
    }
    // Handle __CPROVER_assume
    if(callee_kind == "Identifier")
    {
      std::string fn = json_string(json_member(callee_node, "text"));
      if(fn == "__CPROVER_havoc_object")
      {
        const jsont &call_args = json_member(expr_node, "arguments");
        if(call_args.is_array() && !to_json_array(call_args).empty())
        {
          exprt arg = convert_expression(*to_json_array(call_args).begin());
          if(!arg.is_nil())
            return code_frontend_assignt{
              arg,
              side_effect_expr_nondett{arg.type(), get_location(expr_node)}};
        }
        return code_skipt{};
      }
      if(fn == "__CPROVER_cover")
      {
        const jsont &call_args = json_member(expr_node, "arguments");
        if(call_args.is_array() && !to_json_array(call_args).empty())
        {
          exprt cond = convert_expression(*to_json_array(call_args).begin());
          if(!cond.is_nil())
          {
            if(cond.type().id() != ID_bool)
              cond = typecast_exprt{cond, bool_typet{}};
            // Cover goals use assert with "cover" property class
            code_assertt cover{cond};
            cover.add_source_location().set_property_class("cover");
            cover.add_source_location().set_comment("coverage goal");
            return std::move(cover);
          }
        }
        return code_skipt{};
      }
      if(fn == "__CPROVER_assert")
      {
        const jsont &call_args = json_member(expr_node, "arguments");
        if(call_args.is_array())
        {
          const auto &arr = to_json_array(call_args);
          if(!arr.empty())
          {
            exprt cond = convert_expression(*arr.begin());
            if(!cond.is_nil())
            {
              if(cond.type().id() != ID_bool)
                cond = typecast_exprt{cond, bool_typet{}};
              code_assertt assertion{cond};
              assertion.add_source_location() = get_location(expr_node);
              return std::move(assertion);
            }
          }
        }
        return code_skipt{};
      }
      if(fn == "__CPROVER_assume")
      {
        const jsont &call_args = json_member(expr_node, "arguments");
        if(call_args.is_array())
        {
          const auto &arr = to_json_array(call_args);
          if(!arr.empty())
          {
            exprt cond = convert_expression(*arr.begin());
            if(!cond.is_nil())
            {
              if(cond.type().id() != ID_bool)
                cond = typecast_exprt{cond, bool_typet{}};
              return code_assumet{cond};
            }
          }
        }
        return code_skipt{};
      }
      // CBMC verification primitive: loop invariant assertion
      if(fn == "__CPROVER_loop_invariant")
      {
        const jsont &call_args = json_member(expr_node, "arguments");
        if(call_args.is_array())
        {
          const auto &arr = to_json_array(call_args);
          if(!arr.empty())
          {
            exprt cond = convert_expression(*arr.begin());
            if(!cond.is_nil())
            {
              if(cond.type().id() != ID_bool)
                cond = typecast_exprt{cond, bool_typet{}};
              code_assertt inv{cond};
              inv.add_source_location() = get_location(expr_node);
              inv.add_source_location().set_property_class("loop-invariant");
              inv.add_source_location().set_comment("loop invariant");
              return std::move(inv);
            }
          }
        }
        return code_skipt{};
      }
      // CBMC verification primitive: precondition
      if(fn == "__CPROVER_requires")
      {
        const jsont &call_args = json_member(expr_node, "arguments");
        if(call_args.is_array())
        {
          const auto &arr = to_json_array(call_args);
          if(!arr.empty())
          {
            exprt cond = convert_expression(*arr.begin());
            if(!cond.is_nil())
            {
              if(cond.type().id() != ID_bool)
                cond = typecast_exprt{cond, bool_typet{}};
              code_assertt req{cond};
              req.add_source_location() = get_location(expr_node);
              req.add_source_location().set_property_class("precondition");
              req.add_source_location().set_comment("precondition");
              return std::move(req);
            }
          }
        }
        return code_skipt{};
      }
      // CBMC verification primitive: postcondition
      if(fn == "__CPROVER_ensures")
      {
        const jsont &call_args = json_member(expr_node, "arguments");
        if(call_args.is_array())
        {
          const auto &arr = to_json_array(call_args);
          if(!arr.empty())
          {
            exprt cond = convert_expression(*arr.begin());
            if(!cond.is_nil())
            {
              if(cond.type().id() != ID_bool)
                cond = typecast_exprt{cond, bool_typet{}};
              code_assertt ens{cond};
              ens.add_source_location() = get_location(expr_node);
              ens.add_source_location().set_property_class("postcondition");
              ens.add_source_location().set_comment("postcondition");
              return std::move(ens);
            }
          }
        }
        return code_skipt{};
      }
    }
  }
  // Handle postfix increment/decrement: i++ → i = i + 1
  if(expr_kind == "PostfixUnaryExpression")
  {
    std::string op = json_string(json_member(expr_node, "operator"));
    exprt operand = convert_expression(json_member(expr_node, "operand"));
    if(!operand.is_nil())
    {
      exprt one = from_integer(1, operand.type());
      exprt new_val = (op == "PlusPlusToken")
                        ? exprt{plus_exprt{operand, one}}
                        : exprt{minus_exprt{operand, one}};
      return code_frontend_assignt{operand, new_val};
    }
  }
  // Handle prefix increment/decrement: ++i → i = i + 1
  if(expr_kind == "PrefixUnaryExpression")
  {
    std::string op = json_string(json_member(expr_node, "operator"));
    if(op == "PlusPlusToken" || op == "MinusMinusToken")
    {
      exprt operand = convert_expression(json_member(expr_node, "operand"));
      if(!operand.is_nil())
      {
        exprt one = from_integer(1, operand.type());
        exprt new_val = (op == "PlusPlusToken")
                          ? exprt{plus_exprt{operand, one}}
                          : exprt{minus_exprt{operand, one}};
        return code_frontend_assignt{operand, new_val};
      }
    }
  }
  // Handle compound assignments: x += 1 → x = x + 1
  if(expr_kind == "BinaryExpression")
  {
    std::string op = json_string(json_member(expr_node, "operator"));
    if(
      op == "FirstCompoundAssignment" || op == "PlusEqualsToken" ||
      op == "MinusEqualsToken" || op == "AsteriskEqualsToken" ||
      op == "SlashEqualsToken" || op == "PercentEqualsToken" ||
      op == "AmpersandAmpersandEqualsToken" || op == "BarBarEqualsToken" ||
      op == "QuestionQuestionEqualsToken" || op == "EqualsToken" ||
      op == "FirstAssignment")
    {
      // Check if LHS is a setter property access
      const jsont &lhs_node = json_member(expr_node, "left");
      // ES2024 §20.1.2.7: If LHS is a property of a frozen object,
      // the assignment is silently ignored (non-strict mode). We
      // convert it to a no-op.
      if(is_kind(lhs_node, "PropertyAccessExpression"))
      {
        exprt base_expr =
          convert_expression(json_member(lhs_node, "expression"));
        if(
          base_expr.id() == ID_symbol &&
          frozen_symbols.count(to_symbol_expr(base_expr).get_identifier()) > 0)
        {
          // Still evaluate RHS for side effects, but drop the write.
          (void)convert_expression(json_member(expr_node, "right"));
          return code_skipt{};
        }
      }
      if(is_kind(lhs_node, "PropertyAccessExpression"))
      {
        exprt obj_expr =
          convert_expression(json_member(lhs_node, "expression"));
        std::string prop =
          json_string(json_member(json_member(lhs_node, "name"), "text"));
        if(!obj_expr.is_nil() && obj_expr.type().id() == ID_struct)
        {
          const auto &st = to_struct_type(obj_expr.type());
          if(!st.has_component(prop))
          {
            std::string cls_tag = id2string(st.get_tag());
            std::string cls_name = cls_tag;
            if(cls_tag.find("typescript_class_") == 0)
              cls_name = cls_tag.substr(17);
            irep_idt setter_id{"typescript::" + cls_name + "::__set_" + prop};
            const symbolt *setter = symbol_table.lookup(setter_id);
            if(setter != nullptr && setter->type.id() == ID_code)
            {
              exprt rhs = convert_expression(json_member(expr_node, "right"));
              return code_expressiont{side_effect_expr_function_callt{
                symbol_exprt{setter_id, setter->type},
                {address_of_exprt{obj_expr}, rhs},
                to_code_type(setter->type).return_type(),
                get_location(expr_node)}};
            }
          }
        }
      }
      exprt lhs = convert_expression(json_member(expr_node, "left"));
      exprt rhs = convert_expression(json_member(expr_node, "right"));
      if(!lhs.is_nil() && !rhs.is_nil())
      {
        exprt new_val = rhs;
        if(op != "EqualsToken" && op != "FirstAssignment")
        {
          if(lhs.type() != rhs.type())
            rhs = typecast_exprt{rhs, lhs.type()};
          if(op == "FirstCompoundAssignment" || op == "PlusEqualsToken")
            new_val = plus_exprt{lhs, rhs};
          else if(op == "MinusEqualsToken")
            new_val = minus_exprt{lhs, rhs};
          else if(op == "AsteriskEqualsToken")
            new_val = mult_exprt{lhs, rhs};
          else if(op == "SlashEqualsToken")
            new_val = div_exprt{lhs, rhs};
          else if(op == "AmpersandAmpersandEqualsToken")
            new_val = if_exprt{typecast_exprt{lhs, bool_typet{}}, rhs, lhs};
          else if(op == "BarBarEqualsToken")
            new_val = if_exprt{typecast_exprt{lhs, bool_typet{}}, lhs, rhs};
          else if(op == "QuestionQuestionEqualsToken")
            new_val = lhs; // non-null types: just keep lhs
          else
            new_val = mod_exprt{lhs, rhs};
        }
        if(new_val.type() != lhs.type())
          new_val = typecast_exprt{new_val, lhs.type()};
        return code_frontend_assignt{lhs, new_val};
      }
    }
  }
  // Fallback: evaluate expression
  exprt e = convert_expression(expr_node);
  if(!pending_stmts.empty())
  {
    code_blockt block;
    for(auto &s : pending_stmts)
      block.add(std::move(s));
    pending_stmts.clear();
    if(!e.is_nil())
      block.add(code_expressiont{e});
    return std::move(block);
  }
  if(e.is_nil())
    return code_skipt{};
  return code_expressiont{e};
}
// ES2024 sec-if-statement
codet typescript_convertert::convert_if_statement(const jsont &node)
{
  exprt cond = convert_expression(json_member(node, "expression"));
  if(cond.is_nil())
    return code_skipt{};
  if(cond.type().id() != ID_bool)
    cond = typecast_exprt{cond, bool_typet{}};

  // If the condition expression produced pending side-effect
  // statements (typically bounds-check asserts for indexed array
  // reads inside the condition), drain them into a block that runs
  // BEFORE the `if` evaluates. Without this, the pending_stmts
  // queue outlives the `if` converter and gets flushed at the
  // enclosing statement's trailing flush point, which for
  // `if (arr[i] ...) { ... }` inside a loop places the bounds
  // check AFTER the loop body exits — at which point the loop
  // index has already advanced past the asserted bound and the
  // check fires spuriously. Draining here keeps each evaluation
  // of the condition paired with its own bounds checks.
  code_blockt pre_cond;
  if(!pending_stmts.empty())
  {
    for(auto &s : pending_stmts)
      pre_cond.add(std::move(s));
    pending_stmts.clear();
  }

  codet then_code = convert_statement(json_member(node, "thenStatement"));

  const jsont &else_node = json_member(node, "elseStatement");
  codet if_code =
    else_node.is_object()
      ? code_ifthenelset{cond, std::move(then_code), convert_statement(else_node)}
      : code_ifthenelset{cond, std::move(then_code)};

  if(pre_cond.statements().empty())
    return if_code;
  pre_cond.add(std::move(if_code));
  return std::move(pre_cond);
}

// ES2024 sec-while-statement
codet typescript_convertert::convert_while_statement(const jsont &node)
{
  exprt cond = convert_expression(json_member(node, "expression"));
  if(cond.is_nil())
    return code_skipt{};
  if(cond.type().id() != ID_bool)
    cond = typecast_exprt{cond, bool_typet{}};

  // Drain any pending side-effect statements the condition produced
  // (typically bounds-check asserts for indexed array reads). The
  // condition is re-evaluated on every iteration, so the checks
  // must run on every iteration too; we accomplish that by lifting
  // them to the start of the body and rewriting the loop as
  //   while(true) { <pending>; if(!cond) break; <body> }
  // which preserves semantics and matches the natural "assert
  // before access" intent.
  std::vector<codet> cond_pending;
  if(!pending_stmts.empty())
  {
    for(auto &s : pending_stmts)
      cond_pending.push_back(std::move(s));
    pending_stmts.clear();
  }

  // Body-pending: any pending_stmts produced by the body itself
  // (e.g. bounds-check asserts from `arr[i]` inside the body) need
  // to land INSIDE the loop so they re-run each iteration. Save
  // the outer pending_stmts queue, swap in an empty one, convert
  // the body, and then pull out the body's pending.
  std::vector<codet> saved_pending;
  saved_pending.swap(pending_stmts);
  codet body = convert_statement(json_member(node, "statement"));
  std::vector<codet> body_pending;
  if(!pending_stmts.empty())
  {
    for(auto &s : pending_stmts)
      body_pending.push_back(std::move(s));
    pending_stmts.clear();
  }
  // Restore outer pending so enclosing context drains them.
  for(auto &s : saved_pending)
    pending_stmts.push_back(std::move(s));
  if(cond_pending.empty() && body_pending.empty())
  {
    code_whilet loop{cond, std::move(body)};
    loop.add_source_location() = get_location(node);
    return std::move(loop);
  }

  code_blockt new_body;
  for(auto &s : cond_pending)
    new_body.add(std::move(s));
  if(!cond_pending.empty())
    new_body.add(code_ifthenelset{not_exprt{cond}, code_breakt{}});
  for(auto &s : body_pending)
    new_body.add(std::move(s));
  new_body.add(std::move(body));
  code_whilet loop{
    cond_pending.empty() ? cond : static_cast<exprt>(true_exprt{}),
    std::move(new_body)};
  loop.add_source_location() = get_location(node);
  return std::move(loop);
}

// ES2024 sec-for-statement
codet typescript_convertert::convert_for_statement(const jsont &node)
{
  code_blockt block;

  // Initializer
  const jsont &init = json_member(node, "initializer");
  if(init.is_object())
    block.add(convert_statement(init));

  // Condition
  exprt cond = true_exprt{};
  std::vector<codet> cond_pending;
  const jsont &cond_node = json_member(node, "condition");
  if(cond_node.is_object())
  {
    cond = convert_expression(cond_node);
    if(cond.type().id() != ID_bool)
      cond = typecast_exprt{cond, bool_typet{}};
    // Drain pending stmts from the condition — same reasoning as in
    // convert_while_statement above.
    if(!pending_stmts.empty())
    {
      for(auto &s : pending_stmts)
        cond_pending.push_back(std::move(s));
      pending_stmts.clear();
    }
  }

  // Lower the incrementor to a side_effect_expr_assignt (or nil if
  // there is no incrementor / the form isn't recognised). Using the
  // side-effect form lets code_fort/goto_convert place the iter at
  // the continue-target; that way `continue` inside the body runs
  // the iter before rechecking the condition, matching spec. If we
  // instead put a plain `code_expressiont(i + 1)` in the body, the
  // update has no side effect and the loop diverges.
  exprt iter_expr = nil_exprt{};
  const jsont &inc = json_member(node, "incrementor");
  if(inc.is_object())
  {
    std::string inc_kind = json_string(json_member(inc, "_kind"));
    const source_locationt inc_loc = get_location(inc);
    if(
      inc_kind == "PostfixUnaryExpression" ||
      inc_kind == "PrefixUnaryExpression")
    {
      std::string op = json_string(json_member(inc, "operator"));
      if(op == "PlusPlusToken" || op == "MinusMinusToken")
      {
        exprt operand = convert_expression(json_member(inc, "operand"));
        if(!operand.is_nil())
        {
          exprt one = from_integer(1, operand.type());
          exprt new_val = (op == "PlusPlusToken")
                            ? exprt{plus_exprt{operand, one}}
                            : exprt{minus_exprt{operand, one}};
          iter_expr = side_effect_expr_assignt{operand, new_val, inc_loc};
        }
      }
    }
    else if(inc_kind == "BinaryExpression")
    {
      std::string op = json_string(json_member(inc, "operator"));
      exprt lhs = convert_expression(json_member(inc, "left"));
      exprt rhs = convert_expression(json_member(inc, "right"));
      if(!lhs.is_nil() && !rhs.is_nil())
      {
        if(rhs.type() != lhs.type())
          rhs = typecast_exprt{rhs, lhs.type()};
        exprt new_val = rhs;
        if(op == "PlusEqualsToken")
          new_val = plus_exprt{lhs, rhs};
        else if(op == "MinusEqualsToken")
          new_val = minus_exprt{lhs, rhs};
        else if(op == "AsteriskEqualsToken")
          new_val = mult_exprt{lhs, rhs};
        else if(op == "SlashEqualsToken")
          new_val = div_exprt{lhs, rhs};
        if(new_val.type() != lhs.type())
          new_val = typecast_exprt{new_val, lhs.type()};
        iter_expr = side_effect_expr_assignt{lhs, new_val, inc_loc};
      }
    }
    // Other incrementor forms (comma-expressions, general side-
    // effecting calls) are not common; if we can't lower to a
    // side-effect assign we fall back to a plain expression
    // evaluation. This matches the old behaviour for those cases.
    if(iter_expr.is_nil())
    {
      exprt plain = convert_expression(inc);
      if(!plain.is_nil())
        iter_expr = plain;
    }
  }

  // Fast path: no bounds-check pending from the condition. Emit a
  // standard code_fort(init-done-above, cond, iter, body). GOTO
  // conversion places `iter` at the continue-target.
  //
  // Wrap the body so body-generated pending_stmts (e.g. array
  // bounds-check asserts from `arr[i]` accesses inside the body)
  // end up INSIDE the loop body. Otherwise they leak to the
  // enclosing block and the check fires once after the loop with
  // the post-exit value of the loop index, producing a spurious
  // bounds violation.
  code_blockt body_block;
  std::vector<codet> saved_pending;
  saved_pending.swap(pending_stmts);
  codet body = convert_statement(json_member(node, "statement"));
  // Drain any pending stmts the body produced into the body_block,
  // positioned BEFORE the body statement so bounds checks run at
  // access time within each iteration. (The ordering matches the
  // runtime semantics: the bounds check for arr[i] precedes the
  // read, and the read happens during body execution.)
  // Actually CBMC's convention is to emit the ASSERT before the
  // use; placing pending before body matches this.
  if(!pending_stmts.empty())
  {
    for(auto &s : pending_stmts)
      body_block.add(std::move(s));
    pending_stmts.clear();
  }
  body_block.add(std::move(body));
  // Restore the previously-queued pending_stmts (from before this
  // for-loop) so they'll be drained by the enclosing context.
  for(auto &s : saved_pending)
    pending_stmts.push_back(std::move(s));
  if(cond_pending.empty())
  {
    // code_fort's init was already done above (block.add(init)) and
    // we pass nil_exprt for init to avoid double execution.
    code_fort loop{nil_exprt{}, cond, iter_expr, std::move(body_block)};
    loop.add_source_location() = get_location(node);
    block.add(std::move(loop));
    return std::move(block);
  }

  // Slow path: condition has bounds-check pending stmts. Rewrite as
  //   while(true) { <pending>; if(!cond) break; <body>; <iter>; }
  // `continue` inside <body> still skips <iter> in this form. If we
  // need to support continue-with-iter in the pending case, we can
  // revisit with an explicit label. For now, flag this rarely-used
  // combination.
  code_blockt loop_body;
  for(auto &s : cond_pending)
    loop_body.add(std::move(s));
  loop_body.add(code_ifthenelset{not_exprt{cond}, code_breakt{}});
  loop_body.add(std::move(body_block));
  if(iter_expr.is_not_nil())
  {
    if(
      iter_expr.id() == ID_side_effect &&
      to_side_effect_expr(iter_expr).get_statement() == ID_assign)
    {
      loop_body.add(code_frontend_assignt{
        iter_expr.operands()[0], iter_expr.operands()[1]});
    }
    else
    {
      loop_body.add(code_expressiont{iter_expr});
    }
  }
  code_whilet loop{static_cast<exprt>(true_exprt{}), std::move(loop_body)};
  loop.add_source_location() = get_location(node);
  block.add(std::move(loop));
  return std::move(block);
}

// ES2024 sec-return-statement
codet typescript_convertert::convert_return_statement(const jsont &node)
{
  const jsont &expr = json_member(node, "expression");
  if(expr.is_object())
  {
    exprt val = convert_expression(expr);
    if(!val.is_nil())
    {
      // Typecast return value to match function's return type
      if(!current_function.empty())
      {
        irep_idt fid{"typescript::" + current_function};
        auto dot = current_function.find("::");
        if(dot == std::string::npos)
          fid = irep_idt{"typescript::" + current_function};
        const symbolt *fsym = symbol_table.lookup(fid);
        if(fsym != nullptr && fsym->type.id() == ID_code)
        {
          typet ret_type = to_code_type(fsym->type).return_type();
          // Function → function-pointer return: wrap in address_of,
          // not typecast. Same reasoning as the convert_variable_
          // statement fix above: a bare typecast leaves the value
          // undefined at symex.
          if(
            val.type().id() == ID_code && ret_type.id() == ID_pointer &&
            to_pointer_type(ret_type).base_type().id() == ID_code)
          {
            val = address_of_exprt{val};
          }
          if(ret_type.id() != ID_empty && val.type() != ret_type)
            val = typecast_exprt(val, ret_type);
        }
      }
      // Flush pending_stmts (e.g., constructor calls from NewExpression)
      if(!pending_stmts.empty())
      {
        code_blockt block;
        for(auto &s : pending_stmts)
          block.add(std::move(s));
        pending_stmts.clear();
        block.add(code_frontend_returnt{val});
        return std::move(block);
      }
      return code_frontend_returnt{val};
    }
  }
  return code_frontend_returnt{};
}

codet typescript_convertert::convert_block(const jsont &node)
{
  const jsont &stmts = json_member(node, "statements");
  if(!stmts.is_array())
    return code_skipt{};

  code_blockt block;
  for(const auto &stmt : to_json_array(stmts))
  {
    std::string sk = json_string(json_member(stmt, "_kind"));
    block.add(convert_statement(stmt));
    // Dead code elimination: stop after unconditional control flow
    if(
      sk == "ReturnStatement" || sk == "BreakStatement" ||
      sk == "ContinueStatement" || sk == "ThrowStatement")
      break;
  }
  return std::move(block);
}

// ES2024 sec-function-definitions
