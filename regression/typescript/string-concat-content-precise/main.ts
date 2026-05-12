// ES2024 §22.1.3.3: Symbolic string concatenation's content is
// precisely solver-backed. Constrained symbolic operands imply the
// exact concatenated content, not just length.
const a: string = nondet_string();
__CPROVER_assume(a === "foo");
const b: string = a + "bar";
console.assert(b.length === 6);
console.assert(b === "foobar");
