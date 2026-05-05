const obj = { a: 1, b: 2, c: 3 };
const keys: string[] = Object.keys(obj);
const vals: number[] = Object.values(obj);
console.assert(keys.length === 3);
console.assert(vals.length === 3);
console.assert(vals[0] === 1);
