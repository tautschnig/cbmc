// ES2024 §13.5.4: Symbolic string concatenation routes through the
// refined-string solver (cprover_string_concat_func); the result is
// character-precise, not just length-correct.
//
// This test covers ONE concatenation per program. Multiple concats
// in the same program expose a refinement-loop scalability issue
// and are tracked separately (see string-concat-symbolic-multi
// KNOWNBUG).
const a: string = nondet_string();
__CPROVER_assume(a === "hello");
const b: string = a + " world";
console.assert(b === "hello world");
