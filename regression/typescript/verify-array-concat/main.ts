const a: number[] = [1, 2, 3];
const b: number[] = [4, 5, 6];
const c: number[] = a.concat(b);
console.assert(c.length === 6);
console.assert(c[0] === 1);
console.assert(c[5] === 6);
