/// Python to GOTO converter — string-method dispatch
/// (PLR §6.4.6 str methods). Handles split, replace,
/// format, format_map, startswith, endswith, the
/// is{alpha,digit,space,alnum,...} predicates, upper,
/// lower, capitalize, title, swapcase, find, rfind,
/// index, rindex, encode, decode, join, strip variants,
/// count, partition, rpartition, ljust, rjust, center,
/// expandtabs, zfill. Extracted from
/// python_converter_call_method.cpp per
/// doc/python-frontend-call-refactor-plan.md (Phase 3.2).
/// Pure source-split — semantics are preserved.

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/cprover_prefix.h>
#include <util/floatbv_expr.h>
#include <util/ieee_float.h>
#include <util/json.h>
#include <util/mathematical_expr.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/string_constant.h>
#include <util/string_expr.h>
#include <util/symbol.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

std::optional<exprt> python_convertert::try_string_method(
  const jsont &expr,
  const exprt &obj,
  const typet &obj_base_type,
  const std::string &method_name,
  const jsont &args)
{
  // PLib stdtypes: String methods
  if(method_name == "splitlines")
  {
    // PLib stdtypes: str.splitlines(keepends=False).
    // Splits on line boundaries (\n, \r, \r\n, plus \v, \f,
    // \x1c, \x1d, \x1e, \x85, U+2028, U+2029). Empty string
    // produces []; trailing newline does NOT produce an
    // empty trailing element.
    bool keepends = false;
    if(args.is_array() && !as_array(args).empty())
    {
      auto cv = try_eval_double(convert_expression(*as_array(args).begin()));
      if(cv.has_value())
        keepends = cv.value() != 0;
    }
    auto obj_sv = extract_string_value(obj);
    if(obj_sv.has_value())
    {
      const std::string &s = obj_sv.value();
      std::vector<std::string> parts;
      auto is_line_break = [](char c)
      {
        return c == '\n' || c == '\r' || c == '\v' || c == '\f' ||
               c == '\x1c' || c == '\x1d' || c == '\x1e' || c == '\x85';
      };
      std::size_t i = 0, n = s.size();
      while(i < n)
      {
        std::size_t start = i;
        while(i < n && !is_line_break(s[i]))
          ++i;
        std::size_t end_no_sep = i;
        if(i < n)
        {
          // \r\n is a single boundary.
          if(s[i] == '\r' && i + 1 < n && s[i + 1] == '\n')
            i += 2;
          else
            ++i;
        }
        std::size_t end = keepends ? i : end_no_sep;
        parts.push_back(s.substr(start, end - start));
      }
      typet list_type = python_list_type(python_string_type());
      const auto &data_type =
        to_array_type(to_struct_type(list_type).components()[1].type());
      exprt::operandst list_elems;
      for(const auto &p : parts)
        list_elems.push_back(python_string_literal(p));
      while(list_elems.size() < PYTHON_MAX_LIST_LENGTH)
        list_elems.push_back(safe_zero(python_string_type()));
      return struct_exprt{
        {from_integer(static_cast<long long>(parts.size()), python_int_type()),
         array_exprt{std::move(list_elems), data_type}},
        list_type};
    }
  }
  if(method_name == "split")
  {
    // PLib stdtypes: str.split() / str.split(None[, maxsplit]) —
    // whitespace mode. Splits on runs of any whitespace,
    // skipping empty tokens entirely. Triggered when no
    // separator is provided OR the separator argument is None.
    bool whitespace_mode = false;
    long long ws_max_split = -1;
    if(!args.is_array() || as_array(args).empty())
    {
      whitespace_mode = true;
    }
    else
    {
      auto wit = as_array(args).begin();
      // Check if first arg is None (Constant with null value).
      if(is_node_type(*wit, "Constant") && json_member(*wit, "value").is_null())
      {
        whitespace_mode = true;
        ++wit;
        if(wit != as_array(args).end())
        {
          auto cv = try_eval_double(convert_expression(*wit));
          if(cv.has_value())
            ws_max_split = static_cast<long long>(cv.value());
        }
      }
    }
    if(whitespace_mode)
    {
      auto obj_sv = extract_string_value(obj);
      if(obj_sv.has_value())
      {
        std::string s = obj_sv.value();
        std::vector<std::string> parts;
        auto is_ws = [](char c)
        {
          return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
                 c == '\f';
        };
        std::size_t i = 0, n = s.size();
        long long splits = 0;
        while(i < n)
        {
          while(i < n && is_ws(s[i]))
            ++i;
          if(i >= n)
            break;
          if(ws_max_split >= 0 && splits >= ws_max_split)
          {
            // Remaining string (with leading whitespace
            // already stripped) is the final token.
            parts.push_back(s.substr(i));
            i = n;
            break;
          }
          std::size_t start = i;
          while(i < n && !is_ws(s[i]))
            ++i;
          parts.push_back(s.substr(start, i - start));
          ++splits;
        }
        typet list_type = python_list_type(python_string_type());
        const auto &data_type =
          to_array_type(to_struct_type(list_type).components()[1].type());
        exprt::operandst list_elems;
        for(const auto &p : parts)
          list_elems.push_back(python_string_literal(p));
        while(list_elems.size() < PYTHON_MAX_LIST_LENGTH)
          list_elems.push_back(safe_zero(python_string_type()));
        return struct_exprt{
          {from_integer(
             static_cast<long long>(parts.size()), python_int_type()),
           array_exprt{std::move(list_elems), data_type}},
          list_type};
      }
    }
    // PLib stdtypes: str.split(sep[, maxsplit]) — constant optimization
    if(args.is_array() && !as_array(args).empty())
    {
      auto ait = as_array(args).begin();
      exprt delim_expr = convert_expression(*ait);
      // PLR: optional 2nd arg is maxsplit (max number of
      // splits performed; default unlimited = -1).
      long long max_split = -1;
      ++ait;
      if(ait != as_array(args).end())
      {
        auto cv = try_eval_double(convert_expression(*ait));
        if(cv.has_value())
          max_split = static_cast<long long>(cv.value());
      }
      auto obj_sv = extract_string_value(obj);
      auto delim_sv = extract_string_value(delim_expr);
      if(obj_sv.has_value() && delim_sv.has_value())
      {
        std::string s = obj_sv.value();
        std::string d = delim_sv.value();
        std::vector<std::string> parts;
        if(!d.empty())
        {
          std::size_t pos = 0;
          long long splits = 0;
          while(pos <= s.size())
          {
            if(max_split >= 0 && splits >= max_split)
            {
              parts.push_back(s.substr(pos));
              break;
            }
            auto found = s.find(d, pos);
            if(found == std::string::npos)
            {
              parts.push_back(s.substr(pos));
              break;
            }
            parts.push_back(s.substr(pos, found - pos));
            pos = found + d.size();
            splits++;
          }
        }
        else
          parts.push_back(s);

        typet str_type = python_string_type();
        typet list_type = python_list_type(python_string_type());
        const auto &data_type =
          to_array_type(to_struct_type(list_type).components()[1].type());
        exprt::operandst list_elems;
        for(const auto &p : parts)
          list_elems.push_back(python_string_literal(p));
        while(list_elems.size() < PYTHON_MAX_LIST_LENGTH)
          list_elems.push_back(safe_zero(python_string_type()));
        return struct_exprt{
          {from_integer(
             static_cast<long long>(parts.size()), python_int_type()),
           array_exprt{std::move(list_elems), data_type}},
          list_type};
      }
    }
    // Fallback: original struct-literal handler
    if(obj.id() == ID_struct && args.is_array() && !as_array(args).empty())
    {
      exprt delim_expr = convert_expression(*as_array(args).begin());
      // Extract constant string bytes from obj
      if(
        obj.operands().size() == 2 && obj.operands()[0].is_constant() &&
        delim_expr.id() == ID_struct && delim_expr.operands().size() == 2 &&
        delim_expr.operands()[0].is_constant())
      {
        mp_integer slen, dlen;
        if(
          !to_integer(to_constant_expr(obj.operands()[0]), slen) &&
          !to_integer(to_constant_expr(delim_expr.operands()[0]), dlen) &&
          dlen == 1 && slen <= PYTHON_MAX_STRING_LENGTH)
        {
          // Get delimiter byte
          const auto &delim_data = delim_expr.operands()[1];
          mp_integer delim_byte;
          if(
            delim_data.operands().size() > 0 &&
            delim_data.operands()[0].is_constant() &&
            !to_integer(to_constant_expr(delim_data.operands()[0]), delim_byte))
          {
            // Scan string for delimiter, build parts
            const auto &str_data = obj.operands()[1];
            std::vector<std::string> parts;
            std::string current;
            for(mp_integer i = 0; i < slen; ++i)
            {
              std::size_t idx = i.to_ulong();
              if(
                idx < str_data.operands().size() &&
                str_data.operands()[idx].is_constant())
              {
                mp_integer ch;
                if(!to_integer(to_constant_expr(str_data.operands()[idx]), ch))
                {
                  if(ch == delim_byte)
                  {
                    parts.push_back(current);
                    current.clear();
                  }
                  else
                    current += static_cast<char>(ch.to_ulong());
                }
              }
            }
            parts.push_back(current);

            // Build list of string structs
            typet str_type = python_string_type();
            const auto &data_type = array_typet(
              unsignedbv_typet{8},
              from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
            typet list_type = python_list_type(python_string_type());
            const auto &list_data_type =
              to_array_type(to_struct_type(list_type).components()[1].type());

            exprt::operandst list_elems;
            for(const auto &part : parts)
            {
              list_elems.push_back(python_string_literal(part));
            }
            while(list_elems.size() < PYTHON_MAX_LIST_LENGTH)
              list_elems.push_back(safe_zero(python_string_type()));
            exprt len_expr = from_integer(
              static_cast<long long>(parts.size()), python_int_type());
            return struct_exprt{
              {len_expr, array_exprt{std::move(list_elems), list_data_type}},
              list_type};
          }
        }
      }
    }
    // Fallback: nondet list of strings
    return side_effect_expr_nondett{
      python_list_type(python_string_type()), get_location(expr)};
  }
  // PLib stdtypes: upper/lower — exact byte transformation
  if(method_name == "upper" || method_name == "lower")
  {
    auto sv = extract_string_value(obj);
    if(!sv.has_value() && (obj.id() == ID_symbol || obj.id() == ID_dereference))
    {
      auto it = string_constants.find(
        obj.id() == ID_symbol ? to_symbol_expr(obj).get_identifier()
                              : irep_idt{});
      if(it != string_constants.end())
        sv = it->second;
    }
    if(sv.has_value())
    {
      std::string result = sv.value();
      for(auto &c : result)
        c = (method_name == "upper") ? toupper(c) : tolower(c);
      return python_string_literal(result);
    }
    // 1-char struct fast-path: when obj is a single-char
    // struct {1, address_of(arr[0])} (e.g. from the symbolic-
    // string genexp unroll where each loop iteration binds
    // c to such a struct), transform the byte directly via
    // an if_exprt instead of routing through the refined-
    // string solver. This unblocks
    // 'all(c.lower() == ... for c in s)' patterns
    // (e.g. github_3036) that would otherwise emit one
    // cprover_string_to_lower_case_func call per iteration —
    // those cumulate into the SAT-loop crash territory.
    if(
      obj.id() == ID_struct && obj.operands().size() == 2 &&
      obj.operands()[0].is_constant())
    {
      mp_integer slen;
      if(
        !to_integer(to_constant_expr(obj.operands()[0]), slen) && slen == 1)
      {
        const exprt &data_op = obj.operands()[1];
        const exprt *byte = nullptr;
        if(
          data_op.id() == ID_address_of && data_op.operands().size() == 1 &&
          data_op.operands()[0].id() == ID_index &&
          data_op.operands()[0].operands().size() == 2 &&
          data_op.operands()[0].operands()[0].id() == ID_array &&
          data_op.operands()[0].operands()[0].operands().size() == 1)
        {
          byte = &data_op.operands()[0].operands()[0].operands()[0];
        }
        if(byte != nullptr && byte->type().id() == ID_unsignedbv)
        {
          // ASCII-only byte transform. Non-ASCII (>= 0x80)
          // passes through unchanged — Unicode case folding
          // would need cprover_string_to_*_case_func.
          exprt new_byte;
          if(method_name == "lower")
          {
            // 'A' (0x41) <= b <= 'Z' (0x5A)  ->  b + 32
            exprt is_upper = and_exprt{
              binary_relation_exprt{
                *byte, ID_ge, from_integer('A', byte->type())},
              binary_relation_exprt{
                *byte, ID_le, from_integer('Z', byte->type())}};
            new_byte = if_exprt{
              is_upper,
              plus_exprt{*byte, from_integer(32, byte->type())},
              *byte};
          }
          else
          {
            // 'a' (0x61) <= b <= 'z' (0x7A)  ->  b - 32
            exprt is_lower = and_exprt{
              binary_relation_exprt{
                *byte, ID_ge, from_integer('a', byte->type())},
              binary_relation_exprt{
                *byte, ID_le, from_integer('z', byte->type())}};
            new_byte = if_exprt{
              is_lower,
              minus_exprt{*byte, from_integer(32, byte->type())},
              *byte};
          }
          exprt::operandst chars;
          chars.push_back(new_byte);
          array_typet at(
            unsignedbv_typet{8}, from_integer(1, signedbv_typet{64}));
          array_exprt new_arr(std::move(chars), at);
          exprt ptr = address_of_exprt(index_exprt(
            new_arr,
            from_integer(0, signedbv_typet{64}),
            unsignedbv_typet{8}));
          exprt len_one = from_integer(1, signedbv_typet{64});
          return struct_exprt({len_one, ptr}, python_string_type());
        }
      }
    }
    // Use string solver for non-constant upper/lower
    {
      // Decompose obj into struct for the solver
      exprt src =
        (obj.id() == ID_struct && obj.operands().size() == 2)
          ? obj
          : exprt(struct_exprt(
              {member_exprt(obj, "length", signedbv_typet{64}),
               member_exprt(
                 obj, "data", pointer_typet(unsignedbv_typet{8}, 64))},
              obj.type()));
      return emit_string_function(
        method_name == "upper" ? ID_cprover_string_to_upper_case_func
                               : ID_cprover_string_to_lower_case_func,
        {src},
        symbol_table,
        pending_checks,
        loop_depth > 0);
    }
    const auto &data_type = array_typet(
      unsignedbv_typet{8},
      from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
    member_exprt src_data{obj, "data", data_type};
    member_exprt src_len{obj, "length", signedbv_typet{64}};

    static unsigned case_ctr = 0;
    std::string tn = "__case_" + std::to_string(case_ctr++);
    std::string tq = qualify_name(tn);
    irep_idt ti{tq};
    if(symbol_table.lookup(ti) == nullptr)
    {
      symbolt ts{ti, python_string_type(), "python"};
      ts.base_name = tn;
      ts.is_lvalue = true;
      ts.is_state_var = true;
      symbol_table.add(ts);
    }
    symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      member_exprt{tmp, "length", signedbv_typet{64}}, src_len});
    member_exprt dst_data{tmp, "data", data_type};

    exprt lo =
      from_integer(method_name == "upper" ? 'a' : 'A', unsignedbv_typet{8});
    exprt hi =
      from_integer(method_name == "upper" ? 'z' : 'Z', unsignedbv_typet{8});
    exprt offset =
      from_integer(method_name == "upper" ? -32 : 32, signedbv_typet{8});

    for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      exprt ch = index_exprt{src_data, idx};
      exprt in_range = and_exprt{
        binary_relation_exprt{ch, ID_ge, lo},
        binary_relation_exprt{ch, ID_le, hi}};
      exprt converted =
        plus_exprt{ch, typecast_exprt{offset, unsignedbv_typet{8}}};
      pending_checks.push_back(code_frontend_assignt{
        index_exprt{dst_data, idx}, if_exprt{in_range, converted, ch}});
    }
    return std::move(tmp);
  }
  if(
    method_name == "strip" || method_name == "lstrip" ||
    method_name == "rstrip" || method_name == "title" ||
    method_name == "capitalize" || method_name == "swapcase" ||
    method_name == "zfill" || method_name == "casefold" ||
    method_name == "center" || method_name == "ljust" ||
    method_name == "rjust" || method_name == "expandtabs" ||
    method_name == "zfill" || method_name == "encode" ||
    method_name == "decode" || method_name == "removeprefix" ||
    method_name == "removesuffix")
  {
    // Try exact computation for constant strings
    auto str_val = extract_string_value(obj);
    if(str_val.has_value())
    {
      std::string s = str_val.value();
      std::string result;
      if(
        method_name == "strip" || method_name == "lstrip" ||
        method_name == "rstrip")
      {
        std::string chars_to_strip = " \t\n\r\f\v";
        if(args.is_array() && !as_array(args).empty())
        {
          auto cv =
            extract_string_value(convert_expression(*as_array(args).begin()));
          if(cv.has_value())
            chars_to_strip = cv.value();
        }
        if(method_name == "strip")
        {
          size_t start = s.find_first_not_of(chars_to_strip);
          size_t end = s.find_last_not_of(chars_to_strip);
          result = (start == std::string::npos)
                     ? ""
                     : s.substr(start, end - start + 1);
        }
        else if(method_name == "lstrip")
        {
          size_t start = s.find_first_not_of(chars_to_strip);
          result = (start == std::string::npos) ? "" : s.substr(start);
        }
        else // rstrip
        {
          size_t end = s.find_last_not_of(chars_to_strip);
          result = (end == std::string::npos) ? "" : s.substr(0, end + 1);
        }
      }
      else if(method_name == "capitalize")
      {
        result = s;
        if(!result.empty())
        {
          result[0] = static_cast<char>(
            std::toupper(static_cast<unsigned char>(result[0])));
          for(size_t i = 1; i < result.size(); i++)
            result[i] = static_cast<char>(
              std::tolower(static_cast<unsigned char>(result[i])));
        }
      }
      else if(method_name == "title")
      {
        result = s;
        bool next_upper = true;
        for(size_t i = 0; i < result.size(); i++)
        {
          if(std::isalpha(static_cast<unsigned char>(result[i])))
          {
            result[i] = next_upper ? static_cast<char>(std::toupper(
                                       static_cast<unsigned char>(result[i])))
                                   : static_cast<char>(std::tolower(
                                       static_cast<unsigned char>(result[i])));
            next_upper = false;
          }
          else
            next_upper = true;
        }
      }
      else if(method_name == "swapcase")
      {
        result = s;
        for(auto &c : result)
        {
          if(std::isupper(static_cast<unsigned char>(c)))
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          else if(std::islower(static_cast<unsigned char>(c)))
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
      }
      else if(method_name == "casefold")
      {
        result = s;
        for(auto &c : result)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      else if(
        method_name == "center" || method_name == "ljust" ||
        method_name == "rjust")
      {
        int width = 0;
        char fill = ' ';
        if(args.is_array() && !as_array(args).empty())
        {
          auto ait = as_array(args).begin();
          auto wv = try_eval_double(convert_expression(*ait));
          if(wv.has_value())
            width = static_cast<int>(wv.value());
          ++ait;
          if(ait != as_array(args).end())
          {
            auto fv = extract_string_value(convert_expression(*ait));
            if(fv.has_value() && !fv.value().empty())
              fill = fv.value()[0];
          }
        }
        if(static_cast<int>(s.size()) >= width)
          result = s;
        else
        {
          int pad = width - static_cast<int>(s.size());
          if(method_name == "center")
          {
            int left_pad = pad / 2;
            int right_pad = pad - left_pad;
            result =
              std::string(left_pad, fill) + s + std::string(right_pad, fill);
          }
          else if(method_name == "ljust")
            result = s + std::string(pad, fill);
          else
            result = std::string(pad, fill) + s;
        }
      }
      else if(method_name == "expandtabs")
      {
        int tabsize = 8;
        if(args.is_array() && !as_array(args).empty())
        {
          auto tv =
            try_eval_double(convert_expression(*as_array(args).begin()));
          if(tv.has_value())
            tabsize = static_cast<int>(tv.value());
        }
        result.clear();
        int col = 0;
        for(char c : s)
        {
          if(c == '\t')
          {
            int spaces = tabsize - (col % tabsize);
            result += std::string(spaces, ' ');
            col += spaces;
          }
          else if(c == '\n' || c == '\r')
          {
            result += c;
            col = 0;
          }
          else
          {
            result += c;
            col++;
          }
        }
      }
      else if(method_name == "removeprefix")
      {
        std::string prefix;
        if(args.is_array() && !as_array(args).empty())
        {
          auto pv =
            extract_string_value(convert_expression(*as_array(args).begin()));
          if(pv.has_value())
            prefix = pv.value();
        }
        if(!prefix.empty() && s.find(prefix) == 0)
          result = s.substr(prefix.size());
        else
          result = s;
      }
      else if(method_name == "removesuffix")
      {
        std::string suffix;
        if(args.is_array() && !as_array(args).empty())
        {
          auto sv2 =
            extract_string_value(convert_expression(*as_array(args).begin()));
          if(sv2.has_value())
            suffix = sv2.value();
        }
        if(
          !suffix.empty() && s.size() >= suffix.size() &&
          s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
          result = s.substr(0, s.size() - suffix.size());
        else
          result = s;
      }
      else if(method_name == "zfill")
      {
        int width = 0;
        if(args.is_array() && !as_array(args).empty())
        {
          auto wv =
            try_eval_double(convert_expression(*as_array(args).begin()));
          if(wv.has_value())
            width = static_cast<int>(wv.value());
        }
        if(static_cast<int>(s.size()) >= width)
          result = s;
        else
        {
          int pad = width - static_cast<int>(s.size());
          if(!s.empty() && (s[0] == '+' || s[0] == '-'))
            result = s[0] + std::string(pad, '0') + s.substr(1);
          else
            result = std::string(pad, '0') + s;
        }
      }
      else
        result = s; // fallback for unhandled methods
      if(result.size() <= PYTHON_MAX_STRING_LENGTH)
        return python_string_literal(result);
    }
    // Return nondet string with constraints for symbolic strings
    {
      static unsigned str_method_ctr = 0;
      std::string tn = "__str_meth_" + std::to_string(str_method_ctr++);
      std::string tq = qualify_name(tn);
      irep_idt ti{tq};
      if(symbol_table.lookup(ti) == nullptr)
      {
        symbolt ts{ti, python_string_type(), "python"};
        ts.base_name = tn;
        ts.is_lvalue = true;
        ts.is_state_var = true;
        symbol_table.add(ts);
      }
      symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
      pending_checks.push_back(code_frontend_assignt{
        tv,
        side_effect_expr_nondett{python_string_type(), get_location(expr)}});
      // Both ASSUMEs use cprover_string_length_func so the
      // len()-consumer (which also goes through the intrinsic)
      // sees a value coordinated with the input's length.
      // The .length member-access path remains valid too
      // because refine-strings' array_pool.find() respects
      // the struct's .length field for non-constant
      // pointers.
      exprt result_len_intr = emit_string_int_function(
        ID_cprover_string_length_func, tv, symbol_table, pending_checks);
      exprt input_len_intr = emit_string_int_function(
        ID_cprover_string_length_func, obj, symbol_table, pending_checks);
      // strip/lstrip/rstrip: result length <= input length
      if(
        method_name == "strip" || method_name == "lstrip" ||
        method_name == "rstrip")
      {
        pending_checks.push_back(code_assumet{and_exprt{
          binary_relation_exprt{
            result_len_intr, ID_ge, from_integer(0, signedbv_typet{64})},
          binary_relation_exprt{result_len_intr, ID_le, input_len_intr}}});
      }
      // capitalize/title/swapcase/casefold: same length
      else if(
        method_name == "capitalize" || method_name == "title" ||
        method_name == "swapcase" || method_name == "casefold")
      {
        pending_checks.push_back(
          code_assumet{equal_exprt{result_len_intr, input_len_intr}});
      }
      return std::move(tv);
    }
  }
  if(
    method_name == "replace" || method_name == "format" ||
    method_name == "format_map")
  {
    // PLib stdtypes: str.replace(old, new[, count]) for constant strings
    if(
      method_name == "replace" && args.is_array() && as_array(args).size() >= 2)
    {
      auto ait = as_array(args).begin();
      exprt old_expr = convert_expression(*ait);
      ++ait;
      exprt new_expr = convert_expression(*ait);
      // PLR: optional 3rd arg is the maximum number of
      // replacements; default is 'unlimited'. -1 sentinel.
      long long max_count = -1;
      ++ait;
      if(ait != as_array(args).end())
      {
        auto cv = try_eval_double(convert_expression(*ait));
        if(cv.has_value())
          max_count = static_cast<long long>(cv.value());
      }
      // Extract all three as constant strings
      auto extract_str = [&](const exprt &e) -> std::string
      {
        auto sv = extract_string_value(e);
        if(sv.has_value())
          return sv.value();
        return "";
      };
      std::string src = extract_str(obj);
      std::string old_s = extract_str(old_expr);
      std::string new_s = extract_str(new_expr);
      if(!src.empty() && !old_s.empty())
      {
        // Perform replacement up to max_count times.
        std::string result;
        std::size_t pos = 0;
        long long replaced = 0;
        while(pos < src.size())
        {
          if(max_count >= 0 && replaced >= max_count)
          {
            result += src.substr(pos);
            break;
          }
          auto found = src.find(old_s, pos);
          if(found == std::string::npos)
          {
            result += src.substr(pos);
            break;
          }
          result += src.substr(pos, found - pos) + new_s;
          pos = found + old_s.size();
          replaced++;
        }
        // Build string literal
        return python_string_literal(result);
      }
      // Symbolic-string replace not wired: the solver's
      // cprover_string_replace_func handles char-to-char
      // replacement only (5 args, chars not strings).
      // str.replace(old, new) on general strings needs
      // a new multi-char solver intrinsic. Falls through
      // to nondet for now.
    }
    // PLib stdtypes: str.format_map(mapping) — raise
    // KeyError when a {name} placeholder isn't a key in
    // the mapping. We handle the common case where the
    // mapping argument is a dict literal whose keys are
    // resolvable at conversion time.
    if(method_name == "format_map" && args.is_array())
    {
      auto fmt_sv = extract_string_value(obj);
      if(fmt_sv.has_value())
      {
        std::string fmt = fmt_sv.value();
        // Collect named placeholders.
        std::set<std::string> placeholders;
        for(std::size_t i = 0; i + 1 < fmt.size(); i++)
        {
          if(fmt[i] != '{' || fmt[i + 1] == '{')
            continue;
          auto close = fmt.find('}', i + 1);
          if(close == std::string::npos)
            continue;
          std::string spec = fmt.substr(i + 1, close - i - 1);
          if(spec.empty() || std::isdigit(spec[0]) || spec[0] == ':')
          {
            i = close;
            continue;
          }
          auto colon = spec.find(':');
          std::string name =
            colon == std::string::npos ? spec : spec.substr(0, colon);
          placeholders.insert(name);
          i = close;
        }
        // Resolve the mapping keys AND values when it's a
        // literal. We capture each (key, AST-value) pair so
        // we can substitute below.
        std::set<std::string> mapping_keys;
        std::map<std::string, jsont> mapping_pairs;
        bool keys_known = false;
        auto a_it = as_array(args).begin();
        if(a_it != as_array(args).end())
        {
          if(is_node_type(*a_it, "Dict"))
          {
            keys_known = true;
            const jsont &kn = json_member(*a_it, "keys");
            const jsont &vn = json_member(*a_it, "values");
            if(kn.is_array() && vn.is_array())
            {
              auto ki = as_array(kn).begin();
              auto vi = as_array(vn).begin();
              for(; ki != as_array(kn).end() && vi != as_array(vn).end();
                  ++ki, ++vi)
              {
                if(is_node_type(*ki, "Constant"))
                {
                  const jsont &v = json_member(*ki, "value");
                  if(v.is_string())
                  {
                    mapping_keys.insert(v.value);
                    mapping_pairs[v.value] = *vi;
                  }
                }
              }
            }
          }
        }
        if(keys_known)
        {
          bool missing = false;
          for(const auto &name : placeholders)
          {
            if(mapping_keys.count(name) == 0)
            {
              missing = true;
              const symbolt *exc_sym =
                symbol_table.lookup("python::__exception_active");
              const symbolt *exc_type_sym =
                symbol_table.lookup("python::__exception_type");
              if(exc_sym != nullptr)
              {
                pending_checks.push_back(
                  code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
                if(exc_type_sym != nullptr)
                {
                  long h = exception_type_hash("KeyError");
                  pending_checks.push_back(code_frontend_assignt{
                    exc_type_sym->symbol_expr(),
                    from_integer(h, python_int_type())});
                }
              }
              break;
            }
          }
          if(!missing)
          {
            // PLR §6.1.4: substitute every {name} placeholder
            // with str(mapping[name]) when both the format
            // string and all referenced values are constants.
            std::string result;
            bool all_const = true;
            for(std::size_t i = 0; i < fmt.size(); i++)
            {
              if(fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{')
              {
                result += '{';
                ++i;
                continue;
              }
              if(fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}')
              {
                result += '}';
                ++i;
                continue;
              }
              if(fmt[i] == '{' && i + 1 < fmt.size())
              {
                auto close = fmt.find('}', i + 1);
                if(close == std::string::npos)
                {
                  result += fmt[i];
                  continue;
                }
                std::string spec = fmt.substr(i + 1, close - i - 1);
                i = close;
                auto colon = spec.find(':');
                std::string name =
                  colon == std::string::npos ? spec : spec.substr(0, colon);
                auto pi = mapping_pairs.find(name);
                if(pi == mapping_pairs.end())
                {
                  all_const = false;
                  break;
                }
                const jsont &val_ast = pi->second;
                if(is_node_type(val_ast, "Constant"))
                {
                  const jsont &v = json_member(val_ast, "value");
                  if(v.is_string())
                    result += v.value;
                  else if(v.is_number())
                  {
                    // e.g. "{x}".format_map({"x": 1}) -> "1"
                    double d = std::stod(v.value);
                    if(d == std::floor(d) && std::abs(d) < 1e15)
                      result += std::to_string(static_cast<long long>(d));
                    else
                      result += v.value;
                  }
                  else if(v.is_true())
                    result += "True";
                  else if(v.is_false())
                    result += "False";
                  else
                    all_const = false;
                }
                else
                  all_const = false;
              }
              else
                result += fmt[i];
            }
            if(all_const)
              return python_string_literal(result);
          }
        }
      }
      return side_effect_expr_nondett{python_string_type(), get_location(expr)};
    }
    // PLib stdtypes: str.format() — substitute {} placeholders
    if(method_name == "format")
    {
      auto fmt_sv = extract_string_value(obj);
      if(fmt_sv.has_value())
      {
        std::string fmt = fmt_sv.value();
        // Simple {} substitution with string args
        std::string result;
        std::size_t arg_idx = 0;
        exprt::operandst arg_exprs;
        if(args.is_array())
        {
          for(const auto &a : as_array(args))
            arg_exprs.push_back(convert_expression(a));
        }
        bool all_const = true;
        for(std::size_t i = 0; i < fmt.size(); i++)
        {
          // PLR §6.1.3: '{{' is an escape for a literal '{',
          // and '}}' is an escape for a literal '}'.
          if(fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{')
          {
            result += '{';
            ++i;
            continue;
          }
          if(fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}')
          {
            result += '}';
            ++i;
            continue;
          }
          if(fmt[i] == '{' && i + 1 < fmt.size())
          {
            // Find closing }
            auto close = fmt.find('}', i + 1);
            if(close == std::string::npos)
            {
              result += fmt[i];
              continue;
            }
            std::string spec = fmt.substr(i + 1, close - i - 1);
            i = close; // skip to }

            // Determine which arg to use
            std::size_t use_idx = arg_idx;
            std::string fmt_spec;
            if(spec.empty())
            {
              // {} — use next arg
              use_idx = arg_idx++;
            }
            else if(std::isdigit(spec[0]))
            {
              // {0}, {1}, {0:d}, etc.
              auto colon = spec.find(':');
              use_idx = std::stoul(spec.substr(
                0, colon != std::string::npos ? colon : spec.size()));
              if(colon != std::string::npos)
                fmt_spec = spec.substr(colon + 1);
            }
            else if(spec[0] == ':')
            {
              // {:d}, {:.2f}, etc.
              fmt_spec = spec.substr(1);
              use_idx = arg_idx++;
            }
            else
            {
              // PLR §6.1.4: format with a named placeholder
              // like '{name}'. If no keyword arg with this
              // name is provided, raise KeyError. We check
              // the call's 'keywords' field for a matching
              // arg name; missing → KeyError.
              std::string key_name = spec;
              auto colon_pos = key_name.find(':');
              std::string named_fmt_spec;
              if(colon_pos != std::string::npos)
              {
                named_fmt_spec = key_name.substr(colon_pos + 1);
                key_name = key_name.substr(0, colon_pos);
              }
              bool found_kw = false;
              std::string kw_value;
              bool kw_value_const = false;
              const jsont &kws = json_member(expr, "keywords");
              if(kws.is_array())
              {
                for(const auto &kw : as_array(kws))
                {
                  if(json_string(json_member(kw, "arg")) == key_name)
                  {
                    found_kw = true;
                    // Try to extract the kwarg's value as a
                    // constant string for fold.
                    exprt v_expr = convert_expression(json_member(kw, "value"));
                    auto sv = extract_string_value(v_expr);
                    if(sv.has_value())
                    {
                      kw_value = sv.value();
                      kw_value_const = true;
                    }
                    break;
                  }
                }
              }
              if(!found_kw)
              {
                const symbolt *exc_sym =
                  symbol_table.lookup("python::__exception_active");
                const symbolt *exc_type_sym =
                  symbol_table.lookup("python::__exception_type");
                if(exc_sym != nullptr)
                {
                  pending_checks.push_back(code_frontend_assignt{
                    exc_sym->symbol_expr(), true_exprt{}});
                  if(exc_type_sym != nullptr)
                  {
                    long h = exception_type_hash("KeyError");
                    pending_checks.push_back(code_frontend_assignt{
                      exc_type_sym->symbol_expr(),
                      from_integer(h, python_int_type())});
                  }
                }
                all_const = false;
                continue;
              }
              if(kw_value_const)
              {
                result += kw_value;
              }
              else
              {
                all_const = false;
              }
              continue;
            }

            if(use_idx >= arg_exprs.size())
            {
              // PLR §6.1.4: format() raises IndexError
              // when a positional placeholder index has
              // no matching argument (e.g. '{1}'.format('a')
              // or '{} {}'.format('a')).
              const symbolt *exc_sym =
                symbol_table.lookup("python::__exception_active");
              const symbolt *exc_type_sym =
                symbol_table.lookup("python::__exception_type");
              if(exc_sym != nullptr)
              {
                pending_checks.push_back(
                  code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
                if(exc_type_sym != nullptr)
                {
                  long h = exception_type_hash("IndexError");
                  pending_checks.push_back(code_frontend_assignt{
                    exc_type_sym->symbol_expr(),
                    from_integer(h, python_int_type())});
                }
              }
              all_const = false;
              continue;
            }

            // Extract value
            // Detect None argument by AST inspection (not type)
            // since Python's None becomes a signedbv sentinel
            // value indistinguishable from an int by type.
            bool is_none_arg = false;
            if(args.is_array() && use_idx < as_array(args).size())
            {
              auto ait_n = as_array(args).begin();
              std::advance(ait_n, use_idx);
              if(is_node_type(*ait_n, "Constant"))
              {
                const jsont &v = json_member(*ait_n, "value");
                if(v.is_null())
                  is_none_arg = true;
              }
            }
            if(is_none_arg)
            {
              result += "None";
              continue;
            }
            auto sv = extract_string_value(arg_exprs[use_idx]);
            if(sv.has_value())
            {
              result += sv.value();
            }
            else if(
              arg_exprs[use_idx].type().id() == ID_bool &&
              arg_exprs[use_idx].is_constant())
            {
              // PLR §6.1.4: format() of a bool yields its
              // str() spelling ("True" / "False"), not the
              // numeric value 1 / 0.
              bool bv = to_constant_expr(arg_exprs[use_idx]).is_true();
              result += bv ? "True" : "False";
            }
            else
            {
              auto fv = try_eval_double(arg_exprs[use_idx]);
              if(fv.has_value())
              {
                double d = fv.value();
                if(fmt_spec.empty() || fmt_spec == "d" || fmt_spec == "n")
                {
                  if(d == std::floor(d) && std::abs(d) < 1e15)
                    result += std::to_string(static_cast<long long>(d));
                  else
                  {
                    std::ostringstream oss;
                    oss << d;
                    result += oss.str();
                  }
                }
                else if(fmt_spec[0] == '.')
                {
                  // .Nf format
                  int prec = std::stoi(fmt_spec.substr(1));
                  std::ostringstream oss;
                  oss << std::fixed << std::setprecision(prec) << d;
                  result += oss.str();
                }
                else
                  all_const = false;
              }
              else
                all_const = false;
            }
          }
          else
            result += fmt[i];
        }
        if(all_const)
        {
          return python_string_literal(result);
        }
        // Symbolic int fast path: template is just "{}"
        // or "{0}" with no format spec, single int arg.
        // Emit cprover_string_of_int_func.
        if(
          (fmt == "{}" || fmt == "{0}") && arg_exprs.size() == 1 &&
          (arg_exprs[0].type().id() == ID_signedbv ||
           arg_exprs[0].type().id() == ID_integer))
        {
          exprt as_i64 = arg_exprs[0].type() == signedbv_typet{64}
                           ? arg_exprs[0]
                           : safe_typecast(arg_exprs[0], signedbv_typet{64});
          exprt r = emit_string_function(
            ID_cprover_string_of_int_func,
            {as_i64},
            symbol_table,
            pending_checks,
            loop_depth > 0);
          auto ensure_fn = [&](const irep_idt &fid)
          {
            if(symbol_table.lookup(fid) == nullptr)
            {
              array_typet inf_array_type{
                unsignedbv_typet{8}, infinity_exprt(signedbv_typet{64})};
              std::vector<typet> at;
              if(fid == ID_cprover_associate_array_to_pointer_func)
              {
                at.push_back(inf_array_type);
                at.push_back(pointer_typet(unsignedbv_typet{8}, 64));
              }
              else
              {
                at.push_back(inf_array_type);
                at.push_back(signedbv_typet{64});
              }
              symbolt fs{
                fid,
                mathematical_function_typet(std::move(at), signedbv_typet{32}),
                "python"};
              fs.base_name = id2string(fid);
              symbol_table.add(fs);
            }
          };
          ensure_fn(ID_cprover_associate_array_to_pointer_func);
          ensure_fn(ID_cprover_associate_length_to_array_func);
          return r;
        }
      }
    }
    return side_effect_expr_nondett{python_string_type(), get_location(expr)};
  }
  // PLib stdtypes: startswith/endswith — constant fast path.
  // For non-constant string operands the byte-by-byte
  // comparison via member_exprt(.., "data", array_typet)
  // type-confused the field (the actual struct field is a
  // pointer, not an inline array), producing
  // `(select <pointer-bv> <idx>)` SMT — rejected by cvc5
  // with "array select operating on non-array". The
  // semantically-correct refinement-string path is taken
  // by the second startswith/endswith handler below
  // (line ~1693), which emits
  // cprover_string_is_prefix_func / is_suffix_func.
  if(
    (method_name == "startswith" || method_name == "endswith") &&
    args.is_array() && !as_array(args).empty())
  {
    exprt prefix = convert_expression(*as_array(args).begin());
    auto obj_sv = extract_string_value(obj);
    auto pre_sv = extract_string_value(prefix);
    if(obj_sv.has_value() && pre_sv.has_value())
    {
      bool result;
      if(method_name == "startswith")
        result =
          obj_sv.value().substr(0, pre_sv.value().size()) == pre_sv.value();
      else
        result =
          obj_sv.value().size() >= pre_sv.value().size() &&
          obj_sv.value().substr(
            obj_sv.value().size() - pre_sv.value().size()) == pre_sv.value();
      return result ? exprt{true_exprt{}} : exprt{false_exprt{}};
    }
    // Non-constant case: fall through to refinement-string path.
  }
  // PLib stdtypes: exact string predicates
  if(
    method_name == "isdigit" || method_name == "isalpha" ||
    method_name == "isalnum" || method_name == "isupper" ||
    method_name == "islower" || method_name == "isspace" ||
    method_name == "isascii" || method_name == "isnumeric" ||
    method_name == "isidentifier")
  {
    // Constant-string optimization
    auto sv = extract_string_value(obj);
    if(sv.has_value())
    {
      const std::string &s = sv.value();
      bool result = !s.empty();
      // isidentifier has a different shape: first char must be
      // alpha or '_'; subsequent chars alpha-numeric or '_'.
      // Python's str.isidentifier returns True for keywords.
      if(method_name == "isidentifier")
      {
        if(s.empty())
          result = false;
        else
        {
          unsigned char first = static_cast<unsigned char>(s[0]);
          result = std::isalpha(first) || first == '_' || first >= 0x80;
          for(std::size_t i = 1; i < s.size() && result; ++i)
          {
            unsigned char uc = static_cast<unsigned char>(s[i]);
            result = std::isalnum(uc) || uc == '_' || uc >= 0x80;
          }
        }
        return result ? exprt{true_exprt{}} : exprt{false_exprt{}};
      }
      for(char c : s)
      {
        unsigned char uc = static_cast<unsigned char>(c);
        if(
          method_name == "isdigit" || method_name == "isdecimal" ||
          method_name == "isnumeric")
          result = result && std::isdigit(uc);
        else if(method_name == "isalpha")
          result = result && (std::isalpha(uc) || uc >= 0x80);
        else if(method_name == "isalnum")
          result = result && (std::isalnum(uc) || uc >= 0x80);
        else if(method_name == "isupper")
          result = result && (!std::isalpha(uc) || std::isupper(uc));
        else if(method_name == "islower")
          result = result && (!std::isalpha(uc) || std::islower(uc));
        else if(method_name == "isspace")
          result = result && std::isspace(uc);
        else if(method_name == "isascii")
          result = result && (uc < 128);
      }
      if(method_name == "isascii" && s.empty())
        result = true; // empty string is ASCII
      return result ? exprt{true_exprt{}} : exprt{false_exprt{}};
    }
    // Non-constant string predicate: try to read the first
    // byte through the string struct's data pointer and apply
    // the predicate as a byte-level check. This is exact for
    // 1-char strings (the s[i] / 'for c in s' patterns) and
    // a safe over-approximation for longer ones (we only
    // examine data[0]).
    if(
      method_name == "isdigit" || method_name == "isalpha" ||
      method_name == "isalnum" || method_name == "isupper" ||
      method_name == "islower" || method_name == "isspace" ||
      method_name == "isascii" || method_name == "isnumeric")
    {
      // Resolve the string source: either obj is the literal
      // struct (1-char from s[i]), or obj is a Name pointing
      // to a python_string symbol.
      const exprt *src = &obj;
      if(obj.id() == ID_symbol)
        src = &obj;
      // Read the first byte via *(data + 0). Empty string
      // (length=0) returns False per Python semantics
      // (isalpha returns False on empty).
      member_exprt data_ptr{
        *src, "data", pointer_typet{unsignedbv_typet{8}, 64}};
      member_exprt len{*src, "length", signedbv_typet{64}};
      exprt zero = from_integer(0, signedbv_typet{64});
      exprt is_empty = equal_exprt{len, zero};
      exprt is_single = equal_exprt{len, from_integer(1, signedbv_typet{64})};
      // Dereference data[0]. Cast to unsignedbv to make ranges
      // unambiguous.
      exprt byte0 = dereference_exprt{plus_exprt{data_ptr, zero}};
      // Per-method predicate on byte0:
      auto byte_in_range = [&](unsigned char lo, unsigned char hi)
      {
        return and_exprt{
          binary_relation_exprt{byte0, ID_ge, from_integer(lo, byte0.type())},
          binary_relation_exprt{byte0, ID_le, from_integer(hi, byte0.type())}};
      };
      exprt byte_pred = false_exprt{};
      if(method_name == "isdigit" || method_name == "isnumeric")
        byte_pred = byte_in_range('0', '9');
      else if(method_name == "isalpha")
      {
        byte_pred = or_exprt{
          byte_in_range('A', 'Z'),
          or_exprt{
            byte_in_range('a', 'z'),
            // High-byte (UTF-8 leading byte) approximated as alpha.
            binary_relation_exprt{
              byte0, ID_ge, from_integer(0x80, byte0.type())}}};
      }
      else if(method_name == "isalnum")
      {
        byte_pred = or_exprt{
          byte_in_range('0', '9'),
          or_exprt{
            byte_in_range('A', 'Z'),
            or_exprt{
              byte_in_range('a', 'z'),
              binary_relation_exprt{
                byte0, ID_ge, from_integer(0x80, byte0.type())}}}};
      }
      else if(method_name == "isupper")
        byte_pred = byte_in_range('A', 'Z');
      else if(method_name == "islower")
        byte_pred = byte_in_range('a', 'z');
      else if(method_name == "isspace")
      {
        byte_pred = or_exprt{
          equal_exprt{byte0, from_integer(' ', byte0.type())},
          or_exprt{
            equal_exprt{byte0, from_integer('\t', byte0.type())},
            or_exprt{
              equal_exprt{byte0, from_integer('\n', byte0.type())},
              or_exprt{
                equal_exprt{byte0, from_integer('\r', byte0.type())},
                or_exprt{
                  equal_exprt{byte0, from_integer('\v', byte0.type())},
                  equal_exprt{byte0, from_integer('\f', byte0.type())}}}}}};
      }
      else if(method_name == "isascii")
      {
        byte_pred =
          binary_relation_exprt{byte0, ID_lt, from_integer(0x80, byte0.type())};
      }
      // PLR semantics: empty string returns False for all
      // these predicates except isascii (which returns True).
      exprt empty_result =
        (method_name == "isascii") ? exprt{true_exprt{}} : exprt{false_exprt{}};
      // For length >= 1, the predicate holds iff byte0 matches.
      // (For longer strings, we under-check by only looking at
      // byte0, which would over-approximate the result —
      // soundly, a stricter compile-time check would walk
      // every byte. For 1-char strings this is exact.)
      // Use 'is_single' to decide whether to gate on byte0
      // alone or fall through to nondet for length>1.
      return if_exprt{
        is_empty,
        empty_result,
        if_exprt{
          is_single,
          byte_pred,
          side_effect_expr_nondett{bool_typet{}, source_locationt{}}}};
    }
    // Other unsupported predicates: nondet bool.
    return side_effect_expr_nondett{bool_typet{}, source_locationt{}};
  }
  if(
    method_name == "startswith" || method_name == "endswith" ||
    method_name == "istitle" || method_name == "isidentifier" ||
    method_name == "isprintable")
  {
    // Use solver for startswith/endswith on non-constant strings
    if(
      (method_name == "startswith" || method_name == "endswith") &&
      args.is_array() && !as_array(args).empty())
    {
      exprt prefix = convert_expression(*as_array(args).begin());
      if(is_python_string_type(prefix.type()))
      {
        return emit_string_bool_function(
          method_name == "startswith" ? ID_cprover_string_is_prefix_func
                                      : ID_cprover_string_is_suffix_func,
          prefix,
          obj,
          symbol_table,
          pending_checks);
      }
    }
    return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
  }
  if(
    method_name == "find" || method_name == "index" || method_name == "rfind" ||
    method_name == "rindex" || method_name == "count")
  {
    auto str_val = extract_string_value(obj);
    if(
      !str_val.has_value() &&
      (obj.id() == ID_symbol || obj.id() == ID_dereference))
    {
      auto it = string_constants.find(
        obj.id() == ID_symbol ? to_symbol_expr(obj).get_identifier()
                              : irep_idt{});
      if(it != string_constants.end())
        str_val = it->second;
    }
    if(str_val.has_value() && args.is_array() && !as_array(args).empty())
    {
      auto ait = as_array(args).begin();
      exprt arg_expr = convert_expression(*ait);
      auto arg_val = extract_string_value(arg_expr);
      if(!arg_val.has_value() && arg_expr.id() == ID_symbol)
      {
        auto it2 =
          string_constants.find(to_symbol_expr(arg_expr).get_identifier());
        if(it2 != string_constants.end())
          arg_val = it2->second;
      }
      // Parse optional start/end range
      int start = 0, end = -1;
      ++ait;
      if(ait != as_array(args).end())
      {
        auto sv2 = try_eval_double(convert_expression(*ait));
        if(sv2.has_value())
          start = static_cast<int>(sv2.value());
        ++ait;
        if(ait != as_array(args).end())
        {
          auto ev = try_eval_double(convert_expression(*ait));
          if(ev.has_value())
            end = static_cast<int>(ev.value());
        }
      }
      if(arg_val.has_value())
      {
        std::string s = str_val.value();
        int slen = static_cast<int>(s.size());
        if(start < 0)
          start += slen;
        if(start < 0)
          start = 0;
        if(end < 0)
          end = slen;
        if(end > slen)
          end = slen;
        std::string slice = (start < end) ? s.substr(start, end - start) : "";
        const std::string &sub = arg_val.value();
        if(method_name == "find")
        {
          auto pos = slice.find(sub);
          return from_integer(
            pos == std::string::npos ? -1 : static_cast<long long>(pos + start),
            python_int_type());
        }
        if(method_name == "rfind")
        {
          auto pos = slice.rfind(sub);
          return from_integer(
            pos == std::string::npos ? -1 : static_cast<long long>(pos + start),
            python_int_type());
        }
        if(method_name == "index" || method_name == "rindex")
        {
          // PLR: str.index(sub, start, end) searches the
          // substring s[start:end], NOT the full string.
          auto pos =
            (method_name == "index") ? slice.find(sub) : slice.rfind(sub);
          if(pos == std::string::npos)
          {
            // Raise ValueError
            const symbolt *exc_sym =
              symbol_table.lookup("python::__exception_active");
            if(exc_sym)
              pending_checks.push_back(
                code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
            return from_integer(-1, python_int_type());
          }
          // Translate slice-relative position back to the
          // original-string position.
          return from_integer(
            static_cast<long long>(pos) + static_cast<long long>(start),
            python_int_type());
        }
        if(method_name == "count")
        {
          long long cnt = 0;
          size_t pos = 0;
          while((pos = slice.find(sub, pos)) != std::string::npos)
          {
            cnt++;
            pos += sub.empty() ? 1 : sub.size();
          }
          if(sub.empty())
            cnt = static_cast<long long>(slice.size() + 1);
          return from_integer(cnt, python_int_type());
        }
      }
    }
    // Constrained nondet for symbolic find/count
    {
      static unsigned find_ctr = 0;
      std::string tn = "__find_" + std::to_string(find_ctr++);
      std::string tq = qualify_name(tn);
      irep_idt ti{tq};
      if(symbol_table.lookup(ti) == nullptr)
      {
        symbolt ts{ti, python_int_type(), "python"};
        ts.base_name = tn;
        ts.is_lvalue = true;
        ts.is_state_var = true;
        symbol_table.add(ts);
      }
      symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
      pending_checks.push_back(code_frontend_assignt{
        tv, side_effect_expr_nondett{python_int_type(), get_location(expr)}});
      member_exprt slen{obj, "length", signedbv_typet{64}};
      if(method_name == "count")
        pending_checks.push_back(code_assumet{and_exprt{
          binary_relation_exprt{tv, ID_ge, from_integer(0, python_int_type())},
          binary_relation_exprt{tv, ID_le, slen}}});
      else // find, rfind, index, rindex
        pending_checks.push_back(code_assumet{and_exprt{
          binary_relation_exprt{tv, ID_ge, from_integer(-1, python_int_type())},
          binary_relation_exprt{tv, ID_lt, slen}}});
      return std::move(tv);
    }
  }
  if(method_name == "join")
  {
    // sep.join(lst) — for constant sep and list of constant strings
    if(args.is_array() && !as_array(args).empty())
    {
      exprt list_arg = convert_expression(*as_array(args).begin());
      auto sep_val = extract_string_value(obj);
      if(sep_val.has_value() && is_python_list_type(list_arg.type()))
      {
        // Look up list literal for symbols
        const exprt *list_val = &list_arg;
        if(list_arg.id() == ID_symbol)
        {
          auto it =
            list_literals.find(to_symbol_expr(list_arg).get_identifier());
          if(it != list_literals.end())
            list_val = &it->second;
        }
        const auto &list_st = to_struct_type(list_arg.type());
        to_array_type(list_st.components()[1].type());
        if(
          list_val->id() == ID_struct && list_val->operands().size() >= 2 &&
          list_val->operands()[0].is_constant())
        {
          mp_integer len;
          if(!to_integer(to_constant_expr(list_val->operands()[0]), len))
          {
            std::string result;
            bool all_const = true;
            const exprt &data_arr = list_val->operands()[1];
            for(mp_integer i = 0; i < len; ++i)
            {
              auto idx = i.to_ulong();
              if(idx < data_arr.operands().size())
              {
                auto sv = extract_string_value(data_arr.operands()[idx]);
                if(sv.has_value())
                {
                  if(i > 0)
                    result += sep_val.value();
                  result += sv.value();
                }
                else
                  all_const = false;
              }
            }
            if(all_const)
              return python_string_literal(result);
          }
        }
      }
    }
    return side_effect_expr_nondett{python_string_type(), get_location(expr)};
  }
  if(method_name == "partition" || method_name == "rpartition")
  {
    // Constant-string-receiver constant-fold: produce the
    // 3-tuple (head, sep_or_empty, tail) per Python's
    // str.partition / str.rpartition semantics.
    auto sv = extract_string_value(obj);
    if(sv.has_value() && args.is_array() && !as_array(args).empty())
    {
      exprt sep_expr = convert_expression(*as_array(args).begin());
      auto sep_v = extract_string_value(sep_expr);
      if(sep_v.has_value() && !sep_v->empty())
      {
        const std::string &s = sv.value();
        const std::string &sep = sep_v.value();
        std::string head, mid, tail;
        std::size_t pos =
          (method_name == "partition") ? s.find(sep) : s.rfind(sep);
        if(pos == std::string::npos)
        {
          // Not found: per Python, partition returns
          // (whole, "", "") and rpartition returns
          // ("", "", whole).
          if(method_name == "partition")
          {
            head = s;
            mid = "";
            tail = "";
          }
          else
          {
            head = "";
            mid = "";
            tail = s;
          }
        }
        else
        {
          head = s.substr(0, pos);
          mid = sep;
          tail = s.substr(pos + sep.size());
        }
        // Build a python_tuple (struct with three string
        // components).
        struct_typet ttype = python_tuple_type(
          {python_string_type(), python_string_type(), python_string_type()});
        return struct_exprt{
          {python_string_literal(head),
           python_string_literal(mid),
           python_string_literal(tail)},
          ttype};
      }
    }
    // Non-constant fallback: nondet 3-tuple.
    struct_typet ttype = python_tuple_type(
      {python_string_type(), python_string_type(), python_string_type()});
    return side_effect_expr_nondett{ttype, get_location(expr)};
  }

  return std::nullopt;
}
