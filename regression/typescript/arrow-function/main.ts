// ES2024 sec-arrow-function-definitions: Arrow Function Definitions
const double = (x: number): number => x * 2;
const add = (a: number, b: number): number => a + b;
console.assert(double(5) === 10);
console.assert(add(3, 4) === 7);
