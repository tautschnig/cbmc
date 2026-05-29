# Harness Templates — Security-Property Recipes

This directory is a catalog of **starting-point harness templates**
for verifying common security properties of TypeScript code with
CBMC. Each template is a self-contained regression test that
demonstrates one primitive in a realistic context: a nondet attacker
source, the protected sink, and the assertion that catches the bug.

To use a template:

1. Pick the recipe whose threat model matches the code under audit.
2. Copy the template's `main.ts` to your project.
3. Replace the placeholder function with the function you want to verify.
4. Run `cbmc your-main.ts`.

Each template comes in a pair:

- **`harness-template-<recipe>-buggy/`** — the vulnerable pattern;
  the assertion is expected to FAIL. CBMC reports `VERIFICATION FAILED`.
  Use this to confirm your harness is actually exercising the path
  you think it is.
- **`harness-template-<recipe>-defensive/`** — the defensive pattern;
  the assertion holds. CBMC reports `VERIFICATION SUCCESSFUL`. Use
  this as the goal-state your fix should reach.

## Catalog

| Template | Primitive used | Catches |
|----------|----------------|---------|
| `harness-template-recursion-dos` | `__CPROVER_assert_input_size_bounded` | recursive functions over external input without depth guard (CWE-674) |
| `harness-template-allowlist-injection` | `__CPROVER_assert_in_allowlist` | `<value> in <object_literal>` allowlists bypassed via prototype chain (CWE-697) |
| `harness-template-prototype-key-injection` | `__CPROVER_assert_safe_property_key` | property assignments using attacker-keys like `__proto__` / `constructor` (CWE-1321) |
| `harness-template-path-traversal` | `__CPROVER_assert_no_path_traversal` | absolute-path validators that admit `..` segments (CWE-22) |

For the full audit-driven validation evidence behind these templates,
see the ["Scale validation" section](../../doc/typescript-known-limitations.md#7-scale-validation-evidence) of the limitations doc.

## Pairing with the CodeQL→CBMC pipeline

The auto-triage pipeline in `~/codeql-tools/aws-ts-anti-patterns/`
auto-generates harnesses from SARIF results. The templates here are
the *reference* shape those harnesses should converge on as the
pipeline matures. When you write a template by hand for a finding
the pipeline produced, drop it next to the auto-generated harness
and compare — divergences are useful signals about edge cases the
pipeline doesn't yet handle.
