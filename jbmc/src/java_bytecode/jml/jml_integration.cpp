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

  // Step 4: If modular mode, apply DFCC substitution
  // (This reuses the existing apply_modular_contract_substitution
  // from java_bytecode_contracts.cpp — called by the main pipeline
  // after this function returns.)

  return annotated;
}
