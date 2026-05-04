function add(a: number, b: number): number { return a + b; }
function mul(a: number, b: number): number { return a * b; }
console.assert(add(2, 3) === 5);
console.assert(mul(2, 3) === 6);
console.assert(add(mul(2, 3), 4) === 10);
