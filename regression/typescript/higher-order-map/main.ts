// Higher-order function: pass function to map directly
const triple = (x: number): number => x * 3;
const arr: number[] = [1, 2, 3];
const result: number[] = arr.map(triple);
console.assert(result[0] === 3);
console.assert(result[2] === 9);
