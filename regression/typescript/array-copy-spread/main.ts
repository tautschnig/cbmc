const original: number[] = [1, 2, 3, 4, 5];
const copy: number[] = [...original];
console.assert(copy.length === 5);
console.assert(copy[0] === 1);
console.assert(copy[4] === 5);
