const arr: number[] = [5, 12, 8, 130, 44];
const big: number = arr.find((x: number): boolean => x > 100) as number;
console.assert(big === 130);
const small: number[] = arr.filter((x: number): boolean => x < 10);
console.assert(small.length === 2);
