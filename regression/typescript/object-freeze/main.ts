// KNOWNBUG: Object.freeze / Object.isFrozen not modeled.
// ES2024 §20.1.2.9 / §20.1.2.17. Freeze prevents modification; our
// struct model doesn't track frozenness. After freeze, assignments
// to properties are silently allowed (should be ignored / throw in
// strict mode).
const o = { x: 1 };
Object.freeze(o);
console.assert(Object.isFrozen(o));
