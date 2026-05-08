// ES2024 §23.1.2.3: Array.of(...items) creates an array from items.
console.assert(Array.of(1, 2, 3).length === 3);
console.assert(Array.of(1, 2, 3)[0] === 1);
console.assert(Array.of(1, 2, 3)[2] === 3);
console.assert(Array.of().length === 0);
console.assert(Array.of(7).length === 1);
console.assert(Array.of(7)[0] === 7);
