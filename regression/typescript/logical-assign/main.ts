let x: number = 5;
x &&= 10;
console.assert(x === 10);
let y: number = 0;
y ||= 42;
console.assert(y === 42);
