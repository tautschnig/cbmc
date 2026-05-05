const arr: number[] = [1, 2, 3, 4, 5];
const found: number = arr.find((x: number): boolean => x > 10) as number;
// found is uninitialized (0) since nothing matches
console.assert(found === 0);
