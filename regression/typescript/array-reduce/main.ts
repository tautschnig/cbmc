const arr: number[] = [1, 2, 3, 4, 5];
const sum: number = arr.reduce((acc: number, x: number): number => acc + x, 0);
console.assert(sum === 15);
