/// \file
/// Python Parse Tree

#ifndef CPROVER_PYTHON_PYTHON_PARSE_TREE_H
#define CPROVER_PYTHON_PYTHON_PARSE_TREE_H

#include <util/json.h>

#include <iosfwd>
#include <string>

/// Holds the parsed representation of a Python source file.
/// Currently stores the raw JSON AST produced by CPython's ast module.
/// This will be replaced by a proper typed C++ AST in later phases.
class python_parse_treet
{
public:
  void clear()
  {
    ast_json = jsont{};
    filename.clear();
  }

  void output(std::ostream &out) const;

  void swap(python_parse_treet &other)
  {
    ast_json.swap(other.ast_json);
    filename.swap(other.filename);
  }

  /// The JSON AST produced by ast_to_json.py
  jsont ast_json;

  /// The source filename
  std::string filename;
};

#endif // CPROVER_PYTHON_PYTHON_PARSE_TREE_H
