// ES2024 §22.1.3.30: trim on symbolic strings routes through the
// refined-string solver.
const s: string = nondet_string();
__CPROVER_assume(s === "  hello  ");
console.assert(s.trim() === "hello");

// Length: trim should reduce length
const t: string = nondet_string();
__CPROVER_assume(t === "   xyz");
console.assert(t.trim().length === 3);
