const base: number[] = [1, 2, 3];
const extended: number[] = [...base, 4, 5];
console.assert(extended.length === 5);
console.assert(extended[3] === 4);
console.assert(extended[4] === 5);
