// ES2024 §22.1.3.16/17: padStart/padEnd must return the string
// unchanged when length >= target.
console.assert("hello".padStart(2, "0") === "hello");
console.assert("hello".padEnd(2, "0") === "hello");
console.assert("hello".padStart(5, "0") === "hello");
console.assert("hello".padEnd(5, "0") === "hello");

// Normal padding
console.assert("5".padStart(3, "0") === "005");
console.assert("5".padEnd(3, "0") === "500");
// Default fill char is space
console.assert("x".padStart(3) === "  x");
console.assert("x".padEnd(3) === "x  ");
