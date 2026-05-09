// ES2024 §20.1.3.2: Object.prototype.hasOwnProperty
const o = { a: 1, b: 2, c: 3 };
console.assert(o.hasOwnProperty("a"));
console.assert(o.hasOwnProperty("b"));
console.assert(o.hasOwnProperty("c"));
console.assert(!o.hasOwnProperty("z"));
console.assert(!o.hasOwnProperty("d"));

// Different types of values are all enumerable
const mixed = { n: 42, s: "hi", b: true };
console.assert(mixed.hasOwnProperty("n"));
console.assert(mixed.hasOwnProperty("s"));
console.assert(mixed.hasOwnProperty("b"));
console.assert(!mixed.hasOwnProperty("missing"));
