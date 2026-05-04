const arr: number[] = [1, 2, 3, 4, 5];
const result: number[] = arr.filter((x: number): boolean => x > 2).map((x: number): number => x * 10);
console.assert(result.length === 3);
console.assert(result[0] === 30);
