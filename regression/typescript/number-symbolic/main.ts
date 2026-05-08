// Symbolic-input review: Number.is* predicates on symbolic floats.
// Now uses floatbv_round_to_integral_exprt (same primitive as
// Math.floor) to detect integer-ness, so works for symbolic inputs.

const x: number = nondet_number();
__CPROVER_assume(!Number.isNaN(x) && x > -100 && x < 100);

// isFinite / isNaN — always worked symbolically
console.assert(Number.isFinite(x));
console.assert(!Number.isNaN(x));

// isInteger — symbolic round-trip check now works
// Known integer value: isInteger returns true
const y: number = Math.floor(x);  // y is integer
console.assert(Number.isInteger(y));

// Non-integer 5.5 returns false
const z: number = nondet_number();
__CPROVER_assume(z === 5.5);
console.assert(!Number.isInteger(z));

// isSafeInteger for small integers
const i: number = Math.floor(x);  // i is integer in [-100, 100]
console.assert(Number.isSafeInteger(i));
