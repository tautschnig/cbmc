// ES2024 sec-array-initializer: Array Initializer
// "An ArrayLiteral is an expression that creates and initializes an Array."
//
// ES2024 sec-array.prototype.push: Array.prototype.push (...items)
// "Appends the arguments to the end of the array."
//
// ES2024 sec-array.prototype (length property):
// "The initial value of the length property is +0."
const arr: number[] = [1, 2, 3];
console.assert(arr.length === 3);
console.assert(arr[0] === 1);
console.assert(arr[2] === 3);
arr.push(4);
console.assert(arr.length === 4);
console.assert(arr[3] === 4);
