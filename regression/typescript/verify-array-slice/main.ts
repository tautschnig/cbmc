const arr: number[] = [1, 2, 3, 4, 5, 6, 7, 8];
const mid: number[] = arr.slice(2, 5);
console.assert(mid.length === 3);
console.assert(mid[0] === 3);
console.assert(mid[2] === 5);
