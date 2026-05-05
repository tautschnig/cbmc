const arr: number[] = [1, 2, 3, 4, 5, 6, 7, 8];
const count: number = arr.filter((x: number): boolean => x > 5).length;
console.assert(count === 3);
