// ES2024 §22.1.3.21: substring(start, end) swaps args when start > end.
console.assert("hello".substring(3, 1) === "el");
console.assert("hello".substring(4, 0) === "hell");
// Negative clamps to 0
console.assert("hello".substring(-1, 3) === "hel");
console.assert("hello".substring(3, -1) === "hel");
// Past length clamps to length
console.assert("hello".substring(0, 99) === "hello");
// Both out of range
console.assert("hello".substring(99, -99) === "hello");
