// ES2024 sec-array.isarray
console.assert(Array.isArray([1, 2, 3]) === true);
console.assert(Array.isArray([]) === true);
console.assert(Array.isArray("x") === false);
console.assert(Array.isArray(42) === false);
console.assert(Array.isArray(true) === false);
