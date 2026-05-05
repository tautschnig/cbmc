const arr: number[] = [1, 3, 5, 7, 9];
const idx: number = arr.findIndex((x: number): boolean => x > 4);
console.assert(idx === 2);
