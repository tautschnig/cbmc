// ES2024 §20.1.2.7, §20.1.2.15: Object.freeze and Object.isFrozen.
// Freeze prevents property writes; isFrozen reports the flag.
// Note: alias tracking through const assignment is not modeled,
// so we only test freeze via the original binding.
const o = { x: 1 };
console.assert(!Object.isFrozen(o));
Object.freeze(o);
console.assert(Object.isFrozen(o));

// Assignments to frozen object properties are silently ignored
// (non-strict mode behavior).
o.x = 99;
console.assert(o.x === 1);

// Second freeze call is idempotent.
Object.freeze(o);
console.assert(Object.isFrozen(o));

// A separate unfrozen object can still be mutated.
const u = { y: 2 };
console.assert(!Object.isFrozen(u));
u.y = 42;
console.assert(u.y === 42);
