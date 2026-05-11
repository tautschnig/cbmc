// ES2024 §23.1.3.10: Array.prototype.flat() on nested arrays.
// One level of flatten. Constant arrays are flattened at conversion
// time.
const a: number[][] = [[1, 2], [3, 4]];
const b: number[] = a.flat();
console.assert(b.length === 4);
console.assert(b[0] === 1);
console.assert(b[1] === 2);
console.assert(b[2] === 3);
console.assert(b[3] === 4);

// Uneven inner lengths
const c: number[][] = [[1], [2, 3, 4], [5]];
const d: number[] = c.flat();
console.assert(d.length === 5);
console.assert(d[0] === 1);
console.assert(d[1] === 2);
console.assert(d[4] === 5);

// 1-D array (no-op)
const e: number[] = [10, 20, 30];
const f: number[] = e.flat();
console.assert(f.length === 3);
console.assert(f[0] === 10);
