// ES2024 §22.1.3.9: String.prototype.indexOf with a symbolic needle.
// We now emit a per-position if_exprt chain comparing needle chars
// against source chars directly. Works for bounded-length needles
// even when the needle's content is symbolic (constrained by
// assumptions).
const s: string = "hello world";
const needle: string = nondet_string();
__CPROVER_assume(needle.length === 3);
__CPROVER_assume(needle === "llo");
const pos: number = s.indexOf(needle);
console.assert(pos === 2);

// Different needle, different position
const needle2: string = nondet_string();
__CPROVER_assume(needle2.length === 5);
__CPROVER_assume(needle2 === "world");
const pos2: number = s.indexOf(needle2);
console.assert(pos2 === 6);
