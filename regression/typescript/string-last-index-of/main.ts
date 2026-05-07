// ES2024 §22.1.3.11: String.prototype.lastIndexOf
console.assert("hello".lastIndexOf("l") === 3);
console.assert("hello".lastIndexOf("o") === 4);
console.assert("hello".lastIndexOf("h") === 0);
console.assert("hello".lastIndexOf("x") === -1);
// Repeated occurrences — last one wins
console.assert("banana".lastIndexOf("a") === 5);
console.assert("banana".lastIndexOf("an") === 3);
