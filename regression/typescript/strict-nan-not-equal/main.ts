// KNOWNBUG: Per ES2024 §7.2.14 IsStrictlyEqual, NaN === NaN should
// return false. Our frontend models NaN, null, and undefined as the
// same IEEE NaN value, so NaN === NaN incorrectly returns true (in
// exchange for correct null === null / undefined === undefined).
console.assert(!(NaN === NaN));
