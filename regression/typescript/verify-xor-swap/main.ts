let a: number = 5;
let b: number = 3;
a = a ^ b;
b = a ^ b;
a = a ^ b;
console.assert(a === 3);
console.assert(b === 5);
