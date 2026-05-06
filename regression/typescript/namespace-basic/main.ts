namespace Math2 {
  export function add(a: number, b: number): number { return a + b; }
  export function subtract(a: number, b: number): number { return a - b; }
  export function multiply(a: number, b: number): number { return a * b; }
}

console.assert(Math2.add(10, 20) === 30);
console.assert(Math2.subtract(50, 30) === 20);
console.assert(Math2.multiply(6, 7) === 42);
