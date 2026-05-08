// ES2024 §23.1.3.15: Array.prototype.indexOf with fromIndex.
console.assert([1, 2, 3, 2, 1].indexOf(2, 2) === 3);
console.assert([1, 2, 3].indexOf(1, 1) === -1);
console.assert([1, 2, 3, 2, 1].indexOf(3, 0) === 2);
// Negative fromIndex
console.assert([1, 2, 3, 2, 1].indexOf(2, -2) === 3);
