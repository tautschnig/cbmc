const arr: number[] = [1, 2, 3, 4, 5];
const rotated: number[] = arr.slice(2).concat(arr.slice(0, 2));
console.assert(rotated[0] === 3);
console.assert(rotated[4] === 2);
console.assert(rotated.length === 5);
