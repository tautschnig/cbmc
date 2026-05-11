// ES2024 §13.5.4: Symbolic string concatenation now routes through
// the refined-string solver (cprover_string_concat_func), so the
// RESULT is character-precise — not just length-correct.
const a: string = nondet_string();
__CPROVER_assume(a === "hello");
const b: string = a + " world";
console.assert(b === "hello world");

// Other direction: constant + symbolic
const x: string = nondet_string();
__CPROVER_assume(x === "world");
const y: string = "hello " + x;
console.assert(y === "hello world");

// Symbolic + symbolic
const p: string = nondet_string();
const q: string = nondet_string();
__CPROVER_assume(p === "foo");
__CPROVER_assume(q === "bar");
const r: string = p + q;
console.assert(r === "foobar");
