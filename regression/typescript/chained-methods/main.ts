// Chained array methods (requires runtime array evaluation)
const arr: number[] = [1, 2, 3, 4, 5];
const doubled: number[] = arr.map((x: number): number => x * 2);
const sum: number = doubled.reduce((a: number, b: number): number => a + b, 0);
console.assert(sum === 30);
