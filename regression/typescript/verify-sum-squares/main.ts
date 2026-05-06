const arr: number[] = [1, 2, 3, 4];
const squares: number[] = arr.map((x: number): number => x * x);
const total: number = squares.reduce((a: number, b: number): number => a + b, 0);
console.assert(total === 30); // 1+4+9+16
