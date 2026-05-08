// ES2024 §23.1.3.16: Array.prototype.lastIndexOf
console.assert([1, 2, 3, 2, 1].lastIndexOf(2) === 3);
console.assert([1, 2, 3].lastIndexOf(99) === -1);
console.assert([1, 1, 1].lastIndexOf(1) === 2);
// With fromIndex argument
console.assert([1, 2, 3, 2, 1].lastIndexOf(2, 2) === 1);
