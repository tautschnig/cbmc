const arr: number[] = [-1, -2, -3, -4, -5];
const allNeg: boolean = arr.every((x: number): boolean => x < 0);
console.assert(allNeg === true);
const arr2: number[] = [1, -2, 3];
const allNeg2: boolean = arr2.every((x: number): boolean => x < 0);
console.assert(allNeg2 === false);
