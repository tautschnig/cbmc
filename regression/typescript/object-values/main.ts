const obj = { a: 10, b: 20 };
const vals: number[] = Object.values(obj);
console.assert(vals.length === 2);
console.assert(vals[0] === 10);
