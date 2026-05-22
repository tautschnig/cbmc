/*******************************************************************\

Module: JML Integration Entry Point

Author: Kiro (AI agent)

\*******************************************************************/

/// \file
/// Top-level entry point for JML support in JBMC. Orchestrates:
/// 1. Loading specs from .jml files and/or source comments
/// 2. Lowering to GOTO contracts
/// 3. Applying modular substitution (via existing DFCC pipeline)

#ifndef CPROVER_JAVA_BYTECODE_JML_JML_INTEGRATION_H
#define CPROVER_JAVA_BYTECODE_JML_JML_INTEGRATION_H

#include <goto-programs/goto_model.h>

#include <filesystem>
#include <set>
#include <vector>

/// Configuration for JML processing.
struct jml_configt
{
  /// Directories to search for .jml sidecar files.
  std::vector<std::filesystem::path> spec_paths;

  /// Java source files to scan for //@ annotations.
  std::vector<std::filesystem::path> source_files;

  /// Whether to apply modular contract substitution.
  bool modular = false;
};

/// Process JML specifications and lower them into the GOTO model.
///
/// This is the main entry point called from jbmc_parse_options
/// when --jml-specs-path or --jml-source is specified.
///
/// \param goto_model: The GOTO model to modify.
/// \param config: JML configuration (paths, options).
/// \return Set of function identifiers with JML contracts.
std::set<irep_idt> process_jml_specs(
  goto_modelt &goto_model,
  const jml_configt &config);

#endif // CPROVER_JAVA_BYTECODE_JML_JML_INTEGRATION_H
