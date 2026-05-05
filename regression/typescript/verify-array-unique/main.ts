const arr: number[] = [1, 2, 3, 4, 5];
// All elements are unique (no duplicates in first 5)
console.assert(arr.indexOf(1) === 0);
console.assert(arr.indexOf(5) === 4);
console.assert(arr.indexOf(6) === -1);
