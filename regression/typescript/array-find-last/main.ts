// ES2024 §23.1.3.9: Array.prototype.findLast
// ES2024 §23.1.3.9.1: Array.prototype.findLastIndex
const a: number[] = [1, 2, 3, 4, 5];
console.assert(a.findLast((x: number) => x > 2) === 5);
console.assert(a.findLastIndex((x: number) => x > 2) === 4);
// No match: undefined (we return 0 for numeric default) / -1
console.assert(a.findLastIndex((x: number) => x > 100) === -1);
// First match is last in source order
const b: number[] = [1, 2, 3, 2, 1];
console.assert(b.findLast((x: number) => x === 2) === 2);
console.assert(b.findLastIndex((x: number) => x === 2) === 3);
