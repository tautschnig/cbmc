const arr: number[] = [1, 2, 3, 4, 5];
const sum: number = arr.reduce((a: number, b: number): number => a + b, 0);
console.assert(sum === 15);
const avg: number = sum / 5;
console.assert(avg === 3);
