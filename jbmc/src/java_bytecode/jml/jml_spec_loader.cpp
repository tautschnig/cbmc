/*******************************************************************\

Module: JML Sidecar File Loader

Author: Kiro (AI agent)

\*******************************************************************/

#include "jml_spec_loader.h"

#include "jml_parser.h"

#include <util/suffix.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace
{

/// Strip leading/trailing whitespace from a string.
std::string trim(const std::string &s)
{
  auto start = s.find_first_not_of(" \t\r\n");
  if(start == std::string::npos)
    return "";
  auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

/// Extract JML annotation lines from a .jml file. Returns pairs
/// of (method_signature, clauses) where method_signature is the
/// Java method declaration line and clauses are the //@ lines
/// preceding it.
struct method_spec_rawt
{
  std::string package_name;   // e.g., "com.example"
  std::string class_name;     // e.g., "Foo"
  std::string method_sig;     // e.g., "public int bar(int x)"
  std::vector<std::string> jml_lines; // e.g., {"requires x > 0"}
};

/// Parse a .jml file into raw method specs.
std::vector<method_spec_rawt>
parse_jml_file(const std::filesystem::path &path)
{
  std::vector<method_spec_rawt> results;
  std::ifstream in(path);
  if(!in.is_open())
    return results;

  std::string package_name;
  std::string class_name;
  std::vector<std::string> pending_jml;

  std::string line;
  while(std::getline(in, line))
  {
    std::string trimmed = trim(line);

    // Skip empty lines and pure comments (not JML)
    if(trimmed.empty())
      continue;
    if(trimmed.substr(0, 2) == "//" && trimmed.substr(0, 3) != "//@")
      continue;

    // Package declaration
    if(trimmed.substr(0, 7) == "package")
    {
      auto semi = trimmed.find(';');
      if(semi != std::string::npos)
        package_name = trim(trimmed.substr(8, semi - 8));
      continue;
    }

    // Import — skip
    if(trimmed.substr(0, 6) == "import")
      continue;

    // Class declaration
    if(trimmed.find("class ") != std::string::npos ||
       trimmed.find("interface ") != std::string::npos)
    {
      // Extract class name
      std::regex class_re(
        R"((class|interface)\s+(\w+))");
      std::smatch match;
      if(std::regex_search(trimmed, match, class_re))
        class_name = match[2].str();
      continue;
    }

    // Closing brace — skip
    if(trimmed == "}" || trimmed == "{")
      continue;

    // JML annotation line: //@ clause
    if(trimmed.substr(0, 3) == "//@")
    {
      std::string jml_content = trim(trimmed.substr(3));
      if(!jml_content.empty())
        pending_jml.push_back(jml_content);
      continue;
    }

    // JML block annotation: /*@ ... @*/
    if(trimmed.substr(0, 3) == "/*@")
    {
      // Single-line block: /*@ clause @*/
      auto end_marker = trimmed.find("@*/");
      if(end_marker != std::string::npos)
      {
        std::string content = trim(trimmed.substr(3, end_marker - 3));
        if(!content.empty())
          pending_jml.push_back(content);
      }
      else
      {
        // Multi-line block: read until @*/
        std::string content = trim(trimmed.substr(3));
        if(!content.empty())
          pending_jml.push_back(content);
        while(std::getline(in, line))
        {
          trimmed = trim(line);
          // Strip leading @ or * (JML block continuation)
          if(!trimmed.empty() && (trimmed[0] == '@' || trimmed[0] == '*'))
            trimmed = trim(trimmed.substr(1));
          auto end_pos = trimmed.find("@*/");
          if(end_pos != std::string::npos)
          {
            std::string last = trim(trimmed.substr(0, end_pos));
            if(!last.empty())
              pending_jml.push_back(last);
            break;
          }
          if(!trimmed.empty())
            pending_jml.push_back(trimmed);
        }
      }
      continue;
    }

    // Method declaration (anything with parentheses that's not a
    // class/package/import)
    if(trimmed.find('(') != std::string::npos && !pending_jml.empty())
    {
      method_spec_rawt spec;
      spec.package_name = package_name;
      spec.class_name = class_name;
      spec.method_sig = trimmed;
      spec.jml_lines = std::move(pending_jml);
      results.push_back(std::move(spec));
      pending_jml.clear();
      continue;
    }

    // Anything else — clear pending JML (it wasn't attached to a method)
    pending_jml.clear();
  }

  return results;
}

/// Extract method name from a Java method signature line.
/// E.g., "public static int baz(int[] arr);" → "baz"
std::string extract_method_name(const std::string &sig)
{
  // Find the opening paren
  auto paren = sig.find('(');
  if(paren == std::string::npos)
    return "";
  // Walk backward from paren to find the method name
  auto end = paren;
  while(end > 0 && sig[end - 1] == ' ')
    --end;
  auto start = end;
  while(start > 0 && (std::isalnum(static_cast<unsigned char>(sig[start - 1])) ||
                       sig[start - 1] == '_' || sig[start - 1] == '$'))
    --start;
  return sig.substr(start, end - start);
}

/// Build the JBMC-style method identifier from package, class, and
/// method name. E.g., "com.example", "Foo", "bar" →
/// "java::com.example.Foo.bar"
/// Note: this is a simplified match — it doesn't include the
/// descriptor (parameter types). For overloaded methods, we'd need
/// to match on the full descriptor. For now, we match by name only
/// and take the first match in the symbol table.
irep_idt find_method_symbol(
  const std::string &package_name,
  const std::string &class_name,
  const std::string &method_name,
  const symbol_table_baset &symbol_table)
{
  // Build the prefix: "java::com.example.Foo.method_name:"
  std::string prefix = "java::";
  if(!package_name.empty())
    prefix += package_name + ".";
  prefix += class_name + "." + method_name + ":";

  // Search the symbol table for a function symbol with this prefix
  for(const auto &entry : symbol_table.symbols)
  {
    const std::string id_str = id2string(entry.first);
    if(id_str.substr(0, prefix.size()) == prefix &&
       entry.second.type.id() == ID_code)
    {
      return entry.first;
    }
  }

  // Fallback: try without package (inner classes, default package)
  std::string fallback_prefix =
    "java::" + class_name + "." + method_name + ":";
  for(const auto &entry : symbol_table.symbols)
  {
    const std::string id_str = id2string(entry.first);
    if(id_str.substr(0, fallback_prefix.size()) == fallback_prefix &&
       entry.second.type.id() == ID_code)
    {
      return entry.first;
    }
  }

  return irep_idt();
}

} // namespace

jml_contract_mapt jml_load_file(
  const std::filesystem::path &jml_file,
  const symbol_table_baset &symbol_table)
{
  jml_contract_mapt result;

  auto raw_specs = parse_jml_file(jml_file);
  for(const auto &raw : raw_specs)
  {
    const std::string method_name = extract_method_name(raw.method_sig);
    if(method_name.empty())
      continue;

    const irep_idt method_id = find_method_symbol(
      raw.package_name, raw.class_name, method_name, symbol_table);
    if(method_id.empty())
      continue; // Method not loaded — skip

    const irep_idt class_id =
      raw.package_name.empty()
        ? irep_idt("java::" + raw.class_name)
        : irep_idt("java::" + raw.package_name + "." + raw.class_name);

    jml_method_spect spec;
    spec.method_id = method_id;
    for(const auto &jml_line : raw.jml_lines)
    {
      auto clause =
        jml_parse_clause(jml_line, method_id, class_id, symbol_table);
      if(clause.kind != jml_clauset::kindt::UNKNOWN)
        spec.clauses.push_back(std::move(clause));
    }

    if(!spec.clauses.empty())
      result[method_id] = std::move(spec);
  }

  return result;
}

jml_contract_mapt jml_load_specs(
  const std::vector<std::filesystem::path> &spec_paths,
  const symbol_table_baset &symbol_table)
{
  jml_contract_mapt result;

  for(const auto &dir : spec_paths)
  {
    if(!std::filesystem::exists(dir))
      continue;

    // Recursively find all .jml files
    for(const auto &entry :
        std::filesystem::recursive_directory_iterator(dir))
    {
      if(entry.is_regular_file() && entry.path().extension() == ".jml")
      {
        auto file_specs = jml_load_file(entry.path(), symbol_table);
        for(auto &[id, spec] : file_specs)
          result[id] = std::move(spec);
      }
    }
  }

  return result;
}
