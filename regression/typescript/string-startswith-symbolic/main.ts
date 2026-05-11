// ES2024 §22.1.3.23/7: startsWith/endsWith on symbolic strings.
const s: string = nondet_string();
__CPROVER_assume(s === "hello world");
console.assert(s.startsWith("hello"));
console.assert(s.endsWith("world"));
console.assert(!s.startsWith("world"));
console.assert(!s.endsWith("hello"));
