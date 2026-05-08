// Symbolic-input review: nondet_string() now returns a string with
// length bounded to [0, TYPESCRIPT_MAX_STRING_LENGTH]. Previously it
// was completely nondet (including negative length, which is unsound).

const s: string = nondet_string();
// Length bounds
console.assert(s.length >= 0);
console.assert(s.length <= 64);

// Two nondet strings may be different, so comparison is nondet —
// but length constraints should still hold on each.
const t: string = nondet_string();
console.assert(t.length >= 0);
console.assert(t.length <= 64);
