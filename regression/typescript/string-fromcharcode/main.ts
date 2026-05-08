// ES2024 §22.1.2.1: String.fromCharCode builds a string from UTF-16
// code units.
console.assert(String.fromCharCode(104, 101, 108, 108, 111) === "hello");
console.assert(String.fromCharCode(65) === "A");
console.assert(String.fromCharCode(97, 98, 99) === "abc");
console.assert(String.fromCharCode() === "");
