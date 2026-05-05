const add1 = (x: number): number => x + 1;
const mul2 = (x: number): number => x * 2;
const arr: number[] = [1, 2, 3];
const result: number[] = arr.map(add1).map(mul2);
console.assert(result[0] === 4);
console.assert(result[1] === 6);
console.assert(result[2] === 8);
