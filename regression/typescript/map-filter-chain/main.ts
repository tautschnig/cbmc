const arr: number[] = [1, 2, 3, 4, 5];
const doubled: number[] = arr.map((x: number): number => x * 2);
const big: number = doubled.reduce((a: number, b: number): number => a + b, 0);
console.assert(big === 30);
