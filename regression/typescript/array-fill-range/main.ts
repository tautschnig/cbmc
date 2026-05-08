// ES2024 §23.1.3.7: Array.prototype.fill(value, start, end) mutates in place.
const a: number[] = [1, 2, 3, 4, 5];
a.fill(0);
console.assert(a[0] === 0);
console.assert(a[4] === 0);

const b: number[] = [1, 2, 3, 4, 5];
b.fill(9, 1, 3);
console.assert(b[0] === 1);
console.assert(b[1] === 9);
console.assert(b[2] === 9);
console.assert(b[3] === 4);
console.assert(b[4] === 5);

// Negative indices
const c: number[] = [1, 2, 3, 4, 5];
c.fill(7, -2);
console.assert(c[0] === 1);
console.assert(c[3] === 7);
console.assert(c[4] === 7);
