async function getA(): Promise<number> { return 1; }
async function getB(): Promise<number> { return 2; }
async function getC(): Promise<number> { return 3; }
const results: number[] = await Promise.all([getA(), getB(), getC()]);
console.assert(results[0] === 1);
console.assert(results[1] === 2);
console.assert(results[2] === 3);
