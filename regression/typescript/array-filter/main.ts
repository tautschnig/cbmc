// ES2024 sec-array.prototype.filter
const arr: number[] = [1, 2, 3, 4, 5];
const evens: number[] = arr.filter((x: number): boolean => x % 2 === 0);
console.assert(evens.length === 2);
console.assert(evens[0] === 2);
console.assert(evens[1] === 4);
