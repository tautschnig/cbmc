// ES2024 §22.1.3.9: indexOf(searchString, fromIndex).
console.assert("hello".indexOf("l", 3) === 3);
console.assert("hello".indexOf("l", 4) === -1);
console.assert("hello".indexOf("h", 0) === 0);
console.assert("hello".indexOf("h", 1) === -1);
// Negative fromIndex clamps to 0
console.assert("hello".indexOf("h", -99) === 0);
// Past length
console.assert("hello".indexOf("l", 99) === -1);
