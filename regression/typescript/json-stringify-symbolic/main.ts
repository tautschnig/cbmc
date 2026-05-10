// ES2024 §25.5.2: JSON.stringify on a symbolic number.
// Any finite non-NaN number produces a string of length ≥ 1.
// Our encoding sets the result length to a nondet value in
// [1, TYPESCRIPT_MAX_STRING_LENGTH]; content stays nondet.
const x: number = nondet_number();
__CPROVER_assume(x >= 1 && x <= 5);
const s: string = JSON.stringify(x);
console.assert(s.length >= 1);
console.assert(s.length <= 64);

// Multiple calls are independent.
const y: number = nondet_number();
__CPROVER_assume(y >= 10 && y <= 99);
const t: string = JSON.stringify(y);
console.assert(t.length >= 1);
