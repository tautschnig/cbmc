const arr: number[] = [1, 2, 3, 4, 5];
const product: number = arr.reduce((a: number, b: number): number => a * b, 1);
console.assert(product === 120);
