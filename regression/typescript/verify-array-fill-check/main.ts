const arr: number[] = [0, 0, 0, 0, 0];
const filled: number[] = arr.fill(7);
console.assert(filled.every((x: number): boolean => x === 7) === true);
