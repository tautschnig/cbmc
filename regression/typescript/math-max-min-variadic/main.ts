// ES2024 sec-math.max, sec-math.min — support variadic arguments.
console.assert(Math.max(1, 2) === 2);
console.assert(Math.max(1, 2, 3) === 3);
console.assert(Math.max(1, 2, 3, 4, 5) === 5);
console.assert(Math.min(1, 2) === 1);
console.assert(Math.min(5, 4, 3, 2, 1) === 1);
console.assert(Math.min(10, -5, 3) === -5);
console.assert(Math.max(-10, -5, -3) === -3);
