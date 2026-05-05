const arr: number[] = [1, 2, 3, 4, 5];
const filled: number[] = arr.fill(0);
console.assert(filled[0] === 0);
console.assert(filled[4] === 0);
