// Symbolic-input review: Number.isNaN and Number.isFinite on nondet input.
// Note: Number.isInteger/isSafeInteger on symbolic FLOATS return nondet
// (limitation: cannot detect integer-ness of symbolic floats without
// float-to-int round-trip, which CBMC's encoding doesn't cleanly support).
// With --ts-integer-mode, those work symbolically because numbers become int64.

const x: number = nondet_number();
__CPROVER_assume(!Number.isNaN(x) && x > -100 && x < 100);

// isFinite must be true for bounded symbolic input
console.assert(Number.isFinite(x));

// isNaN must be false (we assumed non-NaN)
console.assert(!Number.isNaN(x));
