const arr: number[] = [1, 2, 3, 4, 5];
const found: number = arr.find((x: number): boolean => x > 3) as number;
console.assert(found === 4);
