// ES2024 §25.5.2: JSON.stringify
// Primitives
console.assert(JSON.stringify(42) === "42");
console.assert(JSON.stringify("hello") === '"hello"');
console.assert(JSON.stringify(true) === "true");
console.assert(JSON.stringify(false) === "false");
console.assert(JSON.stringify(null) === "null");
// NaN and Infinity → "null" per spec §25.5.2.4
console.assert(JSON.stringify(NaN) === "null");
console.assert(JSON.stringify(Infinity) === "null");
// Arrays
console.assert(JSON.stringify([1, 2, 3]) === "[1,2,3]");
console.assert(JSON.stringify([]) === "[]");
console.assert(JSON.stringify([true, false]) === "[true,false]");
// Objects
console.assert(JSON.stringify({ a: 1 }) === '{"a":1}');
console.assert(JSON.stringify({ a: 1, b: "x" }) === '{"a":1,"b":"x"}');
// Nested
console.assert(JSON.stringify({ arr: [1, 2] }) === '{"arr":[1,2]}');
// Constants via symbol
const n: number = 42;
console.assert(JSON.stringify(n) === "42");
const s: string = "hi";
console.assert(JSON.stringify(s) === '"hi"');
