const arr: number[] = [10, 20, 30, 40, 50];
console.assert(arr.indexOf(30) === 2);
console.assert(arr.indexOf(99) === -1);
console.assert(arr.includes(40) === true);
console.assert(arr.includes(99) === false);
