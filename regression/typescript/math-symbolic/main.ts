// Symbolic-input review: Math methods work with nondet inputs.
// After adopting floatbv_round_to_integral_exprt (the same primitive
// the C frontend uses for floor/ceil/trunc), the previously-nondet
// rounding methods now work symbolically.
const x: number = nondet_number();
__CPROVER_assume(!Number.isNaN(x) && x > -100 && x < 100);

// Math.abs
console.assert(Math.abs(x) >= 0);
console.assert(Math.abs(-x) === Math.abs(x));

// Math.sign
if (x > 0) console.assert(Math.sign(x) === 1);
if (x < 0) console.assert(Math.sign(x) === -1);

// Math.floor / ceil / trunc — now symbolic via floatbv_round_to_integral
console.assert(Math.floor(x) <= x);
console.assert(Math.ceil(x) >= x);
console.assert(Math.ceil(x) - Math.floor(x) <= 1);
console.assert(Math.ceil(x) - Math.floor(x) >= 0);

// Trunc relation to floor/ceil
if (x >= 0) console.assert(Math.trunc(x) === Math.floor(x));
if (x <= 0) console.assert(Math.trunc(x) === Math.ceil(x));

// Round between floor and ceil
console.assert(Math.round(x) >= Math.floor(x));
console.assert(Math.round(x) <= Math.ceil(x));

// Math.max / min
const y: number = nondet_number();
__CPROVER_assume(!Number.isNaN(y) && y > -100 && y < 100);
console.assert(Math.max(x, y) >= x);
console.assert(Math.min(x, y) <= x);
