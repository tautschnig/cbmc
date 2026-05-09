// KNOWNBUG: Freeze tracking doesn't propagate through returned aliases.
// ES2024 §20.1.2.7 — Object.freeze returns its argument, so the
// result is the same frozen object. Our model tracks frozenness by
// symbol name, not by aliasing, so the returned alias doesn't inherit
// the flag.
const o = { x: 1 };
const alias = Object.freeze(o);
console.assert(Object.isFrozen(alias));
