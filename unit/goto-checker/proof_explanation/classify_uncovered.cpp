// Copyright 2026 Diffblue Limited. All Rights Reserved.

/// \file
/// Unit tests for classify_step() / classify_uncovered() — the
/// Tomb & Joshi static-coverage layer (FMCAD 2025) sitting on top
/// of CBMC PR #8927's word-level proof explanation.
///
/// classify_step() requires a full SSA_stept which depends on
/// solver state, so it is exercised indirectly through end-to-end
/// JBMC regression tests. The functional unit under test here is
/// classify_uncovered(), which takes a vector of
/// proof_explanation_stept (already-classified steps) and groups
/// the not-in-core entries into per-element warnings.

#include <goto-checker/proof_explanation.h>
#include <testing-utils/use_catch.h>

namespace
{
proof_explanation_stept
make_step(step_kindt kind, bool in_core, const std::string &description = "")
{
  proof_explanation_stept s;
  s.step_kind = kind;
  s.in_core = in_core;
  s.description = description;
  s.source_location.set_file("Foo.java");
  s.source_location.set_line("3");
  return s;
}
} // namespace

TEST_CASE(
  "classify_uncovered emits no warnings when every step is in-core",
  "[core][goto-checker][proof-explanation]")
{
  std::vector<proof_explanation_stept> explanation;
  explanation.push_back(make_step(step_kindt::PRECONDITION, true));
  explanation.push_back(make_step(step_kindt::POSTCONDITION, true));
  explanation.push_back(make_step(step_kindt::REGULAR_ASSIGNMENT, true));

  const auto warnings = classify_uncovered(explanation);
  REQUIRE(warnings.empty());
}

TEST_CASE(
  "classify_uncovered flags an unnecessary precondition",
  "[core][goto-checker][proof-explanation]")
{
  std::vector<proof_explanation_stept> explanation;
  explanation.push_back(
    make_step(step_kindt::PRECONDITION, /*in_core=*/false, "x >= 0"));

  const auto warnings = classify_uncovered(explanation);
  REQUIRE(warnings.size() == 1);
  REQUIRE(
    warnings[0].kind ==
    static_coverage_warningt::kindt::UNNECESSARY_PRECONDITION);
  REQUIRE(warnings[0].description == "x >= 0");
}

TEST_CASE(
  "classify_uncovered flags a vacuous postcondition",
  "[core][goto-checker][proof-explanation]")
{
  std::vector<proof_explanation_stept> explanation;
  explanation.push_back(
    make_step(step_kindt::POSTCONDITION, /*in_core=*/false, "true"));

  const auto warnings = classify_uncovered(explanation);
  REQUIRE(warnings.size() == 1);
  REQUIRE(
    warnings[0].kind == static_coverage_warningt::kindt::VACUOUS_POSTCONDITION);
}

TEST_CASE(
  "classify_uncovered flags an unnecessary loop invariant",
  "[core][goto-checker][proof-explanation]")
{
  std::vector<proof_explanation_stept> explanation;
  explanation.push_back(
    make_step(step_kindt::LOOP_INVARIANT, /*in_core=*/false, "i <= n"));

  const auto warnings = classify_uncovered(explanation);
  REQUIRE(warnings.size() == 1);
  REQUIRE(
    warnings[0].kind == static_coverage_warningt::kindt::UNNECESSARY_INVARIANT);
}

TEST_CASE(
  "classify_uncovered flags an unnecessary assumption",
  "[core][goto-checker][proof-explanation]")
{
  std::vector<proof_explanation_stept> explanation;
  explanation.push_back(
    make_step(step_kindt::REGULAR_ASSUMPTION, /*in_core=*/false, "y != 0"));

  const auto warnings = classify_uncovered(explanation);
  REQUIRE(warnings.size() == 1);
  REQUIRE(
    warnings[0].kind ==
    static_coverage_warningt::kindt::UNNECESSARY_ASSUMPTION);
}

TEST_CASE(
  "classify_uncovered flags unconstrained code",
  "[core][goto-checker][proof-explanation]")
{
  std::vector<proof_explanation_stept> explanation;
  explanation.push_back(
    make_step(step_kindt::REGULAR_ASSIGNMENT, /*in_core=*/false, "tmp = 5"));

  const auto warnings = classify_uncovered(explanation);
  REQUIRE(warnings.size() == 1);
  REQUIRE(
    warnings[0].kind == static_coverage_warningt::kindt::UNCONSTRAINED_CODE);
}

TEST_CASE(
  "classify_uncovered filters DFCC contracts-internal steps",
  "[core][goto-checker][proof-explanation]")
{
  std::vector<proof_explanation_stept> explanation;
  // The DFCC write-set machinery shouldn't surface as a user-
  // facing warning even when its synthetic assignments don't
  // contribute to the proof.
  explanation.push_back(make_step(
    step_kindt::CONTRACTS_INTERNAL, /*in_core=*/false, "__contract_ws"));

  const auto warnings = classify_uncovered(explanation);
  REQUIRE(warnings.empty());
}

TEST_CASE(
  "classify_uncovered does not warn for OLD_CAPTURE / DECREASES / OTHER",
  "[core][goto-checker][proof-explanation]")
{
  std::vector<proof_explanation_stept> explanation;
  explanation.push_back(make_step(step_kindt::OLD_CAPTURE, /*in_core=*/false));
  explanation.push_back(make_step(step_kindt::DECREASES, /*in_core=*/false));
  explanation.push_back(make_step(step_kindt::OTHER, /*in_core=*/false));
  explanation.push_back(
    make_step(step_kindt::REGULAR_ASSERTION, /*in_core=*/false));

  const auto warnings = classify_uncovered(explanation);
  // OLD_CAPTURE / DECREASES are bookkeeping; OTHER is unclassified;
  // a not-in-core ASSERTION just means a stronger assertion proved
  // it. None of these are actionable.
  REQUIRE(warnings.empty());
}

TEST_CASE(
  "classify_uncovered emits one warning per uncovered element, in order",
  "[core][goto-checker][proof-explanation]")
{
  std::vector<proof_explanation_stept> explanation;
  explanation.push_back(make_step(step_kindt::PRECONDITION, false, "P1"));
  explanation.push_back(make_step(step_kindt::REGULAR_ASSIGNMENT, true));
  explanation.push_back(make_step(step_kindt::POSTCONDITION, false, "Q1"));
  explanation.push_back(make_step(step_kindt::CONTRACTS_INTERNAL, false));
  explanation.push_back(make_step(step_kindt::REGULAR_ASSIGNMENT, false, "A1"));

  const auto warnings = classify_uncovered(explanation);
  REQUIRE(warnings.size() == 3);
  REQUIRE(
    warnings[0].kind ==
    static_coverage_warningt::kindt::UNNECESSARY_PRECONDITION);
  REQUIRE(warnings[0].description == "P1");
  REQUIRE(
    warnings[1].kind == static_coverage_warningt::kindt::VACUOUS_POSTCONDITION);
  REQUIRE(warnings[1].description == "Q1");
  REQUIRE(
    warnings[2].kind == static_coverage_warningt::kindt::UNCONSTRAINED_CODE);
  REQUIRE(warnings[2].description == "A1");
}
