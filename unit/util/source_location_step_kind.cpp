// Copyright 2026 Diffblue Limited. All Rights Reserved.

/// \file
/// Unit tests for source_locationt::get_step_kind() / set_step_kind().
/// The structured step-kind attribute lets contract-lowering passes
/// tag goto-program instructions with their provenance (precondition
/// / postcondition / loop_invariant / decreases / old_capture) so
/// classify_step() in proof_explanation can read a typed value rather
/// than scraping comment strings.

#include <util/source_location.h>

#include <testing-utils/use_catch.h>

TEST_CASE(
  "source_location step_kind round-trip",
  "[core][util][source-location]")
{
  source_locationt loc;

  // Default: empty tag.
  REQUIRE(loc.get_step_kind().empty());

  // Set + read back.
  loc.set_step_kind(ID_precondition);
  REQUIRE(loc.get_step_kind() == ID_precondition);

  loc.set_step_kind(ID_postcondition);
  REQUIRE(loc.get_step_kind() == ID_postcondition);

  loc.set_step_kind(ID_loop_invariant);
  REQUIRE(loc.get_step_kind() == ID_loop_invariant);

  loc.set_step_kind(ID_decreases);
  REQUIRE(loc.get_step_kind() == ID_decreases);

  loc.set_step_kind(ID_old_capture);
  REQUIRE(loc.get_step_kind() == ID_old_capture);
}

TEST_CASE(
  "step_kind is independent of comment and property_class",
  "[core][util][source-location]")
{
  source_locationt loc;
  loc.set_comment("JML requires");
  loc.set_property_class("precondition");
  loc.set_step_kind(ID_precondition);

  // Each attribute holds its own value.
  REQUIRE(loc.get_comment() == "JML requires");
  REQUIRE(loc.get_property_class() == "precondition");
  REQUIRE(loc.get_step_kind() == ID_precondition);

  // Updating one attribute doesn't disturb the others.
  loc.set_comment("JML ensures");
  REQUIRE(loc.get_comment() == "JML ensures");
  REQUIRE(loc.get_step_kind() == ID_precondition);
}
