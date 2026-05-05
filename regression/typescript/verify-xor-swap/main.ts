let a: number = 42;
let b: number = 99;
a = a ^ b;
b = a ^ b;
a = a ^ b;
console.assert(a === 99);
console.assert(b === 42);
