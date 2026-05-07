// ES2024 sec-array.prototype.sort — supports custom numeric comparator.
// Recognizes (a, b) => a - b (ascending) and (a, b) => b - a (descending).

// Ascending sort
const a: number[] = [3, 1, 2];
a.sort((x: number, y: number) => x - y);
console.assert(a[0] === 1);
console.assert(a[1] === 2);
console.assert(a[2] === 3);

// Descending sort
const b: number[] = [1, 3, 2];
b.sort((x: number, y: number) => y - x);
console.assert(b[0] === 3);
console.assert(b[1] === 2);
console.assert(b[2] === 1);

// Already-sorted
const c: number[] = [1, 2, 3, 4];
c.sort((x: number, y: number) => x - y);
console.assert(c[0] === 1);
console.assert(c[3] === 4);

// Reverse sorted
const d: number[] = [5, 4, 3, 2, 1];
d.sort((x: number, y: number) => x - y);
console.assert(d[0] === 1);
console.assert(d[4] === 5);
