const a: number[] = [1, 2];
const b: number[] = [...a, 3];
b.push(4);
console.assert(b.length === 4);
console.assert(b[2] === 3);
console.assert(b[3] === 4);
