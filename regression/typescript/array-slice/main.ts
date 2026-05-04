const arr: number[] = [1, 2, 3, 4, 5];
const sub: number[] = arr.slice(1, 4);
console.assert(sub.length === 3);
console.assert(sub[0] === 2);
console.assert(sub[2] === 4);
