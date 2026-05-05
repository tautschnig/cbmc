// Filter with named function variable
const isEven = (x: number): boolean => x % 2 === 0;
const arr: number[] = [1, 2, 3, 4, 5, 6];
const evens: number[] = arr.filter(isEven);
console.assert(evens.length === 3);
console.assert(evens[0] === 2);
