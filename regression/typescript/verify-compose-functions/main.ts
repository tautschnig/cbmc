function double(x: number): number { return x * 2; }
function inc(x: number): number { return x + 1; }
function square(x: number): number { return x * x; }
const result: number = square(inc(double(3)));
console.assert(result === 49); // square(inc(6)) = square(7) = 49
