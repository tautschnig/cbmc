/*******************************************************************\

Module: JML Integration Entry Point

Author: Kiro (AI agent)

\*******************************************************************/

#include "jml_integration.h"

#include "jml_lowering.h"
#include "jml_source_extractor.h"
#include "jml_spec_loader.h"

std::set<irep_idt> process_jml_specs(
  goto_modelt &goto_model,
  const jml_configt &config)
{
  // Step 1: Load specs from .jml sidecar files
  jml_contract_mapt contracts;
  if(!config.spec_paths.empty())
  {
    auto sidecar_specs =
      jml_load_specs(config.spec_paths, goto_model.symbol_table);
    for(auto &[id, spec] : sidecar_specs)
      contracts[id] = std::move(spec);
  }

  // Step 2: Extract specs from Java source comments
  for(const auto &source : config.source_files)
  {
    auto source_specs =
      jml_extract_from_source(source, goto_model.symbol_table);
    for(auto &[id, spec] : source_specs)
    {
      // Source specs don't override sidecar specs
      if(contracts.find(id) == contracts.end())
        contracts[id] = std::move(spec);
    }
  }

  if(contracts.empty())
    return {};

  // Step 3: Lower JML contracts into the GOTO model
  auto annotated = lower_jml_contracts(goto_model, contracts);

  // Step 4: modular substitution.
  //
  // Note on `config.modular`: this flag is currently informational
  // only. The actual modular substitution lives in the main JBMC
  // pipeline (apply_modular_contract_substitution in
  // java_bytecode_contracts.cpp), which runs after this function
  // returns and consults its own --modular CLI flag rather than
  // anything in jml_configt.
  //
  // The reason we accept the flag here is API stability: callers
  // that orchestrate JML lowering and modular substitution from a
  // single config object should not have to thread two flags. If
  // a future refactor moves the substitution call inside
  // process_jml_specs, this is the place to dispatch on
  // config.modular.
  //
  // For now, we silently rely on the caller to set --modular at the
  // CLI level if they want modular semantics; an end-to-end test
  // that exercises both --jml-source and --modular together is
  // future work (tracked in REVIEW-PRE-PUSH.md).

  return annotated;
}
