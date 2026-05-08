// Symbolic-input review: Math methods must work with nondet numbers.
const x: number = nondet_number();
__CPROVER_assume(!Number.isNaN(x) && x > -100 && x < 100);

// Math.abs
console.assert(Math.abs(x) >= 0);
console.assert(Math.abs(-x) === Math.abs(x));

// Math.sign
if (x > 0) console.assert(Math.sign(x) === 1);
if (x < 0) console.assert(Math.sign(x) === -1);

// Math.max / min (binary, different second operand)
const y: number = nondet_number();
__CPROVER_assume(!Number.isNaN(y) && y > -100 && y < 100);
console.assert(Math.max(x, y) >= x);
console.assert(Math.min(x, y) <= x);
