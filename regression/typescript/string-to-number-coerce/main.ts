// ES2024 §13.5.4 UnaryPlus + §21.1.1.1 ToNumber(string).
// Constant-string parsing is now supported at conversion time.
// Symbolic-string parsing remains a KNOWNBUG (would need the
// refined string solver); see string-to-number-coerce-symbolic.
const v: number = +"42";
console.assert(v === 42);

// Negative via unary minus
console.assert(+"-7" === -7);

// Decimal
console.assert(+"3.14" === 3.14);

// Empty string → 0
console.assert(+"" === 0);

// Whitespace-only → 0
console.assert(+"  " === 0);

// Trimmed number
console.assert(+"  42  " === 42);

// Non-numeric → NaN (NaN !== NaN but Object.is(NaN, NaN))
console.assert(Object.is(+"abc", NaN));
