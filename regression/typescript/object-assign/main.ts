// ES2024 §20.1.2.1: Object.assign(target, ...sources)
const t = Object.assign({ a: 1 }, { b: 2 }, { c: 3 });
console.assert(t.a === 1);
console.assert(t.b === 2);
console.assert(t.c === 3);

// Later sources override earlier
const u = Object.assign({ a: 1, b: 2 }, { b: 99 });
console.assert(u.a === 1);
console.assert(u.b === 99);

// Single source: copies the source
const v = Object.assign({}, { x: 42 });
console.assert(v.x === 42);
