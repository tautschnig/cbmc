// ES2024 sec-spread-element
const a: number[] = [1, 2];
const b: number[] = [3, 4];
const c: number[] = [...a, ...b];
console.assert(c.length === 4);
console.assert(c[0] === 1);
console.assert(c[3] === 4);
