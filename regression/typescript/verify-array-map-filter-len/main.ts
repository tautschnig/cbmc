const arr: number[] = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
const evens: number[] = arr.filter((x: number): boolean => x % 2 === 0);
console.assert(evens.length === 5);
