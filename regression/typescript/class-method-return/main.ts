// ES2024 sec-class-definitions: method return values
class Calculator {
  value: number;
  constructor(v: number) { this.value = v; }
  add(x: number): number { return this.value + x; }
  multiply(x: number): number { return this.value * x; }
}
const c = new Calculator(10);
console.assert(c.add(5) === 15);
console.assert(c.multiply(3) === 30);
