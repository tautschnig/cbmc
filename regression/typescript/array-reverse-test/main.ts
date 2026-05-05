const arr: number[] = [1, 2, 3, 4, 5];
const rev: number[] = arr.reverse();
console.assert(rev[0] === 5);
console.assert(rev[4] === 1);
console.assert(rev.length === 5);
