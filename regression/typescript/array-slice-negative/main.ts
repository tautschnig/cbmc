// ES2024 §23.1.3.27: Array.prototype.slice with negative indices.
const a: number[] = [10, 20, 30, 40, 50];
console.assert(a.slice(-2).length === 2);
console.assert(a.slice(-2)[0] === 40);
console.assert(a.slice(-2)[1] === 50);
console.assert(a.slice(-3, -1).length === 2);
console.assert(a.slice(-3, -1)[0] === 30);
console.assert(a.slice(-3, -1)[1] === 40);
// start > end → empty
console.assert(a.slice(3, 1).length === 0);
// Out of range clamps
console.assert(a.slice(-99).length === 5);
console.assert(a.slice(0, 99).length === 5);
