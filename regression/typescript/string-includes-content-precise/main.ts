// ES2024 §22.1.3.7: includes on a symbolic receiver is
// content-precise for a single solver call per receiver. Multiple
// includes calls on the same receiver hit a refinement-loop
// convergence issue (tracked via string-includes-symbolic
// KNOWNBUG).
const s: string = nondet_string();
__CPROVER_assume(s === "hello");
console.assert(s.includes("ell"));
