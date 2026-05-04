const arr: number[] = [2, 4, 5, 8];
const allEven: boolean = arr.every((x: number): boolean => x % 2 === 0);
console.assert(allEven === false);
