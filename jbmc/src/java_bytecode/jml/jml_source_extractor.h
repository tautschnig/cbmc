/*******************************************************************\

Module: JML Source Comment Extraction

Author: Kiro (AI agent)

\*******************************************************************/

/// \file
/// Extract JML annotations from Java source file comments.
/// Scans for //@ and /*@ @*/ patterns and associates them with
/// the immediately following method declaration.

#ifndef CPROVER_JAVA_BYTECODE_JML_JML_SOURCE_EXTRACTOR_H
#define CPROVER_JAVA_BYTECODE_JML_JML_SOURCE_EXTRACTOR_H

#include "jml_spec_loader.h"

#include <filesystem>

/// Extract JML specs from a Java source file's comments.
/// Uses the same jml_contract_mapt as the .jml sidecar loader.
jml_contract_mapt jml_extract_from_source(
  const std::filesystem::path &java_source,
  const symbol_table_baset &symbol_table);

#endif // CPROVER_JAVA_BYTECODE_JML_JML_SOURCE_EXTRACTOR_H
