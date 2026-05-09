// ES2024 §25.5.1: JSON.parse
const n: number = JSON.parse("42");
console.assert(n === 42);
const s: string = JSON.parse('"hello"');
console.assert(s === "hello");
const b: boolean = JSON.parse("true");
console.assert(b === true);
const f: boolean = JSON.parse("false");
console.assert(f === false);
// Array
const a: number[] = JSON.parse("[1,2,3]");
console.assert(a.length === 3);
console.assert(a[0] === 1);
console.assert(a[1] === 2);
console.assert(a[2] === 3);
// Negative number
const neg: number = JSON.parse("-42");
console.assert(neg === -42);
// Escaped string
const esc: string = JSON.parse('"a\\"b"');
console.assert(esc === 'a"b');
