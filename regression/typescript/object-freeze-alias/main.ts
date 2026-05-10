// ES2024 §20.1.2.7 — Object.freeze returns its argument, so a const
// bound to Object.freeze(o) aliases the frozen object. The frozen
// status propagates through const-symbol alias chains.
const o: { x: number } = { x: 1 };
const alias: { x: number } = Object.freeze(o);
console.assert(Object.isFrozen(alias));
console.assert(Object.isFrozen(o));

// Chain: alias2 aliases alias, which aliases o. All frozen.
const alias2: { x: number } = alias;
console.assert(Object.isFrozen(alias2));

// Separate unfrozen object stays unfrozen.
const other: { y: number } = { y: 42 };
const other_alias: { y: number } = other;
console.assert(!Object.isFrozen(other));
console.assert(!Object.isFrozen(other_alias));
