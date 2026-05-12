// Multi-assertion refined-string regression.
//
// Verifies that the refined-string axioms are generated for every
// cprover_string_* call regardless of how many assertions the
// program has. Until 2026-05-12 this pattern silently reported
// FAILURE on every assertion because
// `symex_target_equationt::convert_assertions` wraps each assertion
// in `handle()`, which bit-blasts the inner function_application
// into a fresh literal before `set_to()` is invoked on the composed
// goal. string_refinementt::dec_solve iterated over `equations`,
// found no function_applications, and emitted zero universal
// axioms. Fixed in commit 8fd1144b50 by overriding convert_rest /
// convert_function_application in string_refinementt.
//
// If this test ever regresses (reports VERIFICATION FAILED), the
// multi-assertion axiom propagation is broken again.
const s: string = nondet_string();
__CPROVER_assume(s === "hello world");
console.assert(s.startsWith("hello"));
console.assert(s.endsWith("world"));
console.assert(s.includes("o w"));
console.assert(!s.startsWith("world"));
console.assert(!s.endsWith("hello"));
console.assert(!s.includes("xyz"));
