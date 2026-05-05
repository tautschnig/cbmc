const arr: number[] = [1, 2, 3, 4, 5];
const flat: number[] = arr.flat();
console.assert(flat.length === 5);
console.assert(flat[0] === 1);
console.assert(flat[4] === 5);
