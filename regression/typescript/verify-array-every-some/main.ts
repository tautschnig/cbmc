const arr: number[] = [2, 4, 6, 8, 10];
const allEven: boolean = arr.every((x: number): boolean => x % 2 === 0);
console.assert(allEven === true);
const hasOdd: boolean = arr.some((x: number): boolean => x % 2 !== 0);
console.assert(hasOdd === false);
