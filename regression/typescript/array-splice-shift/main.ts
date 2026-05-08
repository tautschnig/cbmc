// ES2024 §23.1.3.30: Array.prototype.splice shifts elements correctly.
const a: number[] = [1, 2, 3, 4, 5];
a.splice(1, 2);
console.assert(a.length === 3);
console.assert(a[0] === 1);
console.assert(a[1] === 4);
console.assert(a[2] === 5);

// Insert: splice(start, 0, ...items)
const b: number[] = [1, 2, 5];
b.splice(2, 0, 3, 4);
console.assert(b.length === 5);
console.assert(b[0] === 1);
console.assert(b[2] === 3);
console.assert(b[3] === 4);
console.assert(b[4] === 5);

// Replace: splice(start, deleteCount, ...items)
const c: number[] = [1, 2, 3];
c.splice(1, 1, 9, 8);
console.assert(c.length === 4);
console.assert(c[1] === 9);
console.assert(c[2] === 8);
console.assert(c[3] === 3);
