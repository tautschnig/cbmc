/*******************************************************************\

Module: JML Sidecar File Loader

Author: Kiro (AI agent)

\*******************************************************************/

/// \file
/// Load JML specifications from .jml sidecar files and match them
/// to class/method symbols in the GOTO model.
///
/// .jml file format (simplified OpenJML-compatible):
///
///   // Package declaration (optional)
///   package com.example;
///
///   // Class specification
///   public class Foo {
///
///     //@ requires x > 0;
///     //@ ensures \result >= x;
///     //@ assignable \nothing;
///     public int bar(int x);
///
///     //@ requires arr != null;
///     //@ ensures \result >= 0;
///     public static int baz(int[] arr);
///   }
///
/// The loader:
/// 1. Scans for .jml files on --jml-specs-path
/// 2. Parses method signatures + JML clauses
/// 3. Matches to GOTO model symbols by qualified name
/// 4. Stores in jml_contract_mapt for the lowering pass

#ifndef CPROVER_JAVA_BYTECODE_JML_JML_SPEC_LOADER_H
#define CPROVER_JAVA_BYTECODE_JML_JML_SPEC_LOADER_H

#include "jml_parser.h"

#include <util/irep.h>
#include <util/symbol_table_base.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

/// Parsed JML specification for a single method.
struct jml_method_spect
{
  irep_idt method_id; // Fully-qualified GOTO symbol id
  std::vector<jml_clauset> clauses;
};

/// Map from method identifier to its JML specification.
using jml_contract_mapt = std::map<irep_idt, jml_method_spect>;

/// Load all .jml files from the given spec paths and match
/// specifications to symbols in the symbol table.
///
/// \param spec_paths: Directories to search for .jml files.
/// \param symbol_table: The global symbol table (for matching).
/// \return A map from method identifiers to their JML specs.
jml_contract_mapt jml_load_specs(
  const std::vector<std::filesystem::path> &spec_paths,
  const symbol_table_baset &symbol_table);

/// Load specs from a single .jml file.
jml_contract_mapt jml_load_file(
  const std::filesystem::path &jml_file,
  const symbol_table_baset &symbol_table);

#endif // CPROVER_JAVA_BYTECODE_JML_JML_SPEC_LOADER_H
