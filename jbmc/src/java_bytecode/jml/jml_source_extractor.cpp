/*******************************************************************\

Module: JML Source Comment Extraction

Author: Kiro (AI agent)

\*******************************************************************/

#include "jml_source_extractor.h"

#include "jml_parser.h"

#include <fstream>
#include <regex>
#include <sstream>
#include <string>

namespace
{

std::string trim(const std::string &s)
{
  auto start = s.find_first_not_of(" \t\r\n");
  if(start == std::string::npos)
    return "";
  auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

/// Extract class name from a line like "public class Foo {"
/// Strip Java // line comments and string literals from a line so
/// regex pattern matching doesn't accidentally match inside them.
/// Block-comment regions (`/* ... */`) are tracked separately by the
/// caller.
std::string strip_line_comments_and_strings(const std::string &line)
{
  std::string out;
  out.reserve(line.size());
  bool in_string = false;
  char string_quote = '\0';
  for(std::size_t i = 0; i < line.size(); ++i)
  {
    const char c = line[i];
    if(in_string)
    {
      // Skip the contents of a string literal.
      if(c == '\\' && i + 1 < line.size())
      {
        ++i; // skip the escaped char
        continue;
      }
      if(c == string_quote)
        in_string = false;
      continue;
    }
    if(c == '"' || c == '\'')
    {
      in_string = true;
      string_quote = c;
      continue;
    }
    if(c == '/' && i + 1 < line.size() && line[i + 1] == '/')
      break; // line comment: ignore the rest
    out.push_back(c);
  }
  return out;
}

/// Extract class name from a line like "public class Foo {".
/// Requires a class/interface/record/enum keyword preceded by a
/// modifier or appearing at start-of-statement (after stripping
/// comments and strings) to reduce false positives.
std::string extract_class_name(const std::string &line)
{
  const std::string clean = strip_line_comments_and_strings(line);
  // Match: <modifiers?> <class|interface|record|enum> <name>.
  // The modifiers group accepts any subset, in any order, separated
  // by whitespace. The leading boundary requires start-of-line or a
  // delimiter so we don't accidentally match `aclass` etc.
  static const std::regex re{
    R"((?:^|[\s;{}])\s*(?:(?:public|private|protected|static|final|abstract|sealed|strictfp)\s+)*(class|interface|record|enum)\s+(\w+))"};
  std::smatch m;
  if(std::regex_search(clean, m, re))
    return m[2].str();
  return "";
}

/// Extract method name from a line like "public int bar(int x) {"
/// or "public int bar(" (continuation across multiple lines).
std::string extract_method_name(const std::string &line)
{
  const std::string clean = strip_line_comments_and_strings(line);
  auto paren = clean.find('(');
  if(paren == std::string::npos)
    return "";
  auto end = paren;
  while(end > 0 && clean[end - 1] == ' ')
    --end;
  auto start = end;
  while(start > 0 &&
        (std::isalnum(static_cast<unsigned char>(clean[start - 1])) ||
         clean[start - 1] == '_'))
    --start;
  return clean.substr(start, end - start);
}

/// Find method symbol in symbol table by class + method name.
irep_idt find_method(
  const std::string &class_name,
  const std::string &method_name,
  const symbol_table_baset &symbol_table)
{
  const std::string prefix = "java::" + class_name + "." + method_name + ":";
  for(const auto &entry : symbol_table.symbols)
  {
    const std::string id_str = id2string(entry.first);
    if(
      id_str.substr(0, prefix.size()) == prefix &&
      entry.second.type.id() == ID_code)
      return entry.first;
  }
  return irep_idt();
}

} // namespace

jml_contract_mapt jml_extract_from_source(
  const std::filesystem::path &java_source,
  const symbol_table_baset &symbol_table)
{
  jml_contract_mapt result;
  std::ifstream in(java_source);
  if(!in.is_open())
    return result;

  std::string package_name;
  std::string class_name;
  std::vector<std::string> pending_jml;
  bool in_block_comment = false;

  std::string line;
  while(std::getline(in, line))
  {
    std::string trimmed = trim(line);

    // Handle multi-line /*@ ... @*/ blocks
    if(in_block_comment)
    {
      auto end_pos = trimmed.find("@*/");
      if(end_pos != std::string::npos)
      {
        std::string content = trim(trimmed.substr(0, end_pos));
        // Strip leading * (JML block continuation style)
        if(!content.empty() && content[0] == '*')
          content = trim(content.substr(1));
        if(!content.empty())
          pending_jml.push_back(content);
        in_block_comment = false;
      }
      else
      {
        std::string content = trimmed;
        if(!content.empty() && content[0] == '*')
          content = trim(content.substr(1));
        if(!content.empty() && content[0] == '@')
          content = trim(content.substr(1));
        if(!content.empty())
          pending_jml.push_back(content);
      }
      continue;
    }

    // Package. Java syntax requires a trailing semicolon
    // (`package com.example;`); Kotlin omits it
    // (`package com.example`). Accept both.
    if(
      trimmed.substr(0, 7) == "package" &&
      (trimmed.size() == 7 ||
       std::isspace(static_cast<unsigned char>(trimmed[7]))))
    {
      auto semi = trimmed.find(';');
      const auto end = (semi == std::string::npos) ? trimmed.size() : semi;
      package_name = trim(trimmed.substr(8, end - 8));
      continue;
    }

    // Class/interface declaration
    auto cn = extract_class_name(trimmed);
    if(!cn.empty())
    {
      class_name = cn;
      if(!package_name.empty())
        class_name = package_name + "." + cn;
      continue;
    }

    // //@ single-line JML annotation
    if(trimmed.substr(0, 3) == "//@")
    {
      std::string content = trim(trimmed.substr(3));
      if(!content.empty())
        pending_jml.push_back(content);
      continue;
    }

    // /*@ block start
    if(trimmed.substr(0, 3) == "/*@")
    {
      auto end_pos = trimmed.find("@*/");
      if(end_pos != std::string::npos)
      {
        // Single-line block
        std::string content = trim(trimmed.substr(3, end_pos - 3));
        if(!content.empty())
          pending_jml.push_back(content);
      }
      else
      {
        in_block_comment = true;
        std::string content = trim(trimmed.substr(3));
        if(!content.empty())
          pending_jml.push_back(content);
      }
      continue;
    }

    // Method declaration — associate pending JML
    if(!pending_jml.empty() && trimmed.find('(') != std::string::npos)
    {
      std::string method_name = extract_method_name(trimmed);
      if(!method_name.empty() && !class_name.empty())
      {
        irep_idt method_id = find_method(class_name, method_name, symbol_table);
        if(!method_id.empty())
        {
          const irep_idt cid = "java::" + class_name;
          jml_method_spect spec;
          spec.method_id = method_id;

          // Extract parameter names from the signature
          auto paren_start = trimmed.find('(');
          auto paren_end = trimmed.find(')');
          if(
            paren_start != std::string::npos &&
            paren_end != std::string::npos && paren_end > paren_start)
          {
            std::string params_str =
              trimmed.substr(paren_start + 1, paren_end - paren_start - 1);
            // Split by top-level commas: a comma inside generic
            // angle brackets (`HashMap<Integer, Integer>`) is
            // part of the parameter type, not a separator.
            std::vector<std::string> param_pieces;
            {
              std::string current;
              int depth = 0;
              for(char c : params_str)
              {
                if(c == '<')
                  ++depth;
                else if(c == '>')
                  --depth;
                if(c == ',' && depth == 0)
                {
                  param_pieces.push_back(current);
                  current.clear();
                }
                else
                {
                  current.push_back(c);
                }
              }
              if(!current.empty())
                param_pieces.push_back(current);
            }
            for(const auto &raw_param : param_pieces)
            {
              std::string param = trim(raw_param);
              if(param.empty())
                continue;
              // Java syntax: `int x` — the last whitespace-separated
              // token is the parameter name.
              // Kotlin syntax: `x: Int` (or `x: Int = default`) —
              // the parameter name is the token *before* the first
              // colon. We disambiguate by checking for a colon
              // before any equals sign.
              auto colon = param.find(':');
              auto eq = param.find('=');
              const bool is_kotlin = colon != std::string::npos &&
                                     (eq == std::string::npos || colon < eq);
              if(is_kotlin)
              {
                std::string name = trim(param.substr(0, colon));
                // Strip Kotlin parameter modifiers (`vararg`,
                // `crossinline`, `noinline`) if present at the
                // start of the token.
                auto sp = name.rfind(' ');
                if(sp != std::string::npos)
                  name = trim(name.substr(sp + 1));
                spec.param_names.push_back(name);
              }
              else
              {
                // Last word is the parameter name (Java form).
                auto last_space = param.rfind(' ');
                if(last_space != std::string::npos)
                  spec.param_names.push_back(
                    trim(param.substr(last_space + 1)));
                else
                  spec.param_names.push_back(param);
              }
            }
          }

          for(const auto &jml_line : pending_jml)
          {
            auto clause =
              jml_parse_clause(jml_line, method_id, cid, symbol_table);
            if(clause.kind != jml_clauset::kindt::UNKNOWN)
              spec.clauses.push_back(std::move(clause));
          }
          if(!spec.clauses.empty())
            result[method_id] = std::move(spec);
        }
      }
      pending_jml.clear();
      continue;
    }

    // Non-JML, non-method line — don't clear pending (might be
    // annotations like @Override between JML and method)
    if(!trimmed.empty() && trimmed[0] == '@')
      continue; // Java annotation — skip, keep pending JML

    // Anything else clears pending
    if(!trimmed.empty() && trimmed != "{" && trimmed != "}")
      pending_jml.clear();
  }

  return result;
}
