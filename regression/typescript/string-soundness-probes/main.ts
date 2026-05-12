// Soundness regression for the refined-string solver.
//
// Each assertion below is FALSE for the given constrained symbolic
// string. A sound verifier must report FAILURE for each of them.
// Historical bug (2026-05-09 → 2026-05-12 in the TypeScript
// frontend's refined-string integration): a type-tag collision +
// static-size mismatch made every path vacuously UNSAT, causing
// these assertions to trivially report SUCCESS — an unsound
// silent miss. Covered by commit 5abee1595d.
const s: string = nondet_string();
__CPROVER_assume(s === "hello");
// Each of these must fail verification:
console.assert(s.includes("xyz"));
console.assert(s === "goodbye");
console.assert(s.startsWith("x"));
console.assert(s.endsWith("x"));
console.assert(s.toUpperCase() === "goodbye");
console.assert(s.length === 99);
