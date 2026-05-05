const arr: number[] = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
const sum: number = arr.reduce((a: number, b: number): number => a + b, 0);
console.assert(sum === 55);
