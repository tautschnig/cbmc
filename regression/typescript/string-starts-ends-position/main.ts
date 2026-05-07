// ES2024 §22.1.3.23: startsWith(searchString, position).
// ES2024 §22.1.3.7: endsWith(searchString, endPosition).
console.assert("hello".startsWith("ell", 1));
console.assert(!"hello".startsWith("hel", 1));
console.assert("hello".startsWith("hel", 0));
console.assert("hello".endsWith("ell", 4));
console.assert(!"hello".endsWith("llo", 3));
// position beyond bounds
console.assert("hello".endsWith("hel", 3));
