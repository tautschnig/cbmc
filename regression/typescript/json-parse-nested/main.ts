// ES2024 §25.5.1: JSON.parse for nested objects.
interface Inner { b: number; }
interface Outer { a: Inner; }
const o: Outer = JSON.parse('{"a":{"b":1}}');
console.assert(o.a.b === 1);

// Two-field object
interface Two { x: number; y: number; }
const t: Two = JSON.parse('{"x":10,"y":20}');
console.assert(t.x === 10);
console.assert(t.y === 20);

// Mixed types
interface Mixed { n: number; s: string; b: boolean; }
const m: Mixed = JSON.parse('{"n":42,"s":"hello","b":true}');
console.assert(m.n === 42);
console.assert(m.s === "hello");
console.assert(m.b);
