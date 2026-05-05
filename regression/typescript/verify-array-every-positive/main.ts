const arr: number[] = [1, 2, 3, 4, 5];
const allPos: boolean = arr.every((x: number): boolean => x > 0);
console.assert(allPos === true);
const hasNeg: boolean = arr.some((x: number): boolean => x < 0);
console.assert(hasNeg === false);
