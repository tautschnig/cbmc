// TSH: Narrowing.md — Type narrowing
// ES2024 sec-ecmascript-language-types-null-type: The Null Type
const x: number = 42;
const y: number = x > 0 ? x : -x;
console.assert(y === 42);

// Truthiness narrowing
const s: string = "hello";
if (s) {
  console.assert(s.length > 0);
}
