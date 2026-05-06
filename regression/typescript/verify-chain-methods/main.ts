const data: number[] = [1, 2, 3, 4, 5];
const sum: number = data.reduce((a: number, b: number): number => a + b, 0);
const count: number = data.length;
console.assert(sum === 15);
console.assert(count === 5);
