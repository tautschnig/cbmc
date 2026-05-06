function sq(x: number): number { return x * x; }
function sum(a: number, b: number): number { return a + b; }
console.assert(sum(sq(3), sq(4)) === 25);
