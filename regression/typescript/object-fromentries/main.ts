// ES2024 §20.1.2.6: Object.fromEntries
const re = Object.fromEntries([["x", 10], ["y", 20]]);
console.assert(re.x === 10);
console.assert(re.y === 20);

// Round-trip: fromEntries(entries(o)) should reconstruct o
const o = { a: 1, b: 2 };
const r = Object.fromEntries(Object.entries(o));
console.assert(r.a === 1);
console.assert(r.b === 2);
