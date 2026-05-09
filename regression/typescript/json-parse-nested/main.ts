// KNOWNBUG: JSON.parse on nested objects.
// ES2024 §25.5.1. Our quick parser handles primitives and flat
// arrays; nested objects {"a":{"b":1}} need a proper recursive parser.
interface Inner { b: number; }
interface Outer { a: Inner; }
const o: Outer = JSON.parse('{"a":{"b":1}}');
console.assert(o.a.b === 1);
