function sum(a: number, b: number, c: number): number { return a + b + c; }
const args: number[] = [1, 2, 3];
const result: number = sum(...args);
console.assert(result === 6);
