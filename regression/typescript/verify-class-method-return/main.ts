class Calculator {
  result: number;
  constructor() { this.result = 0; }
  add(n: number): number { this.result = this.result + n; return this.result; }
  sub(n: number): number { this.result = this.result - n; return this.result; }
}
const c = new Calculator();
console.assert(c.add(10) === 10);
console.assert(c.add(5) === 15);
console.assert(c.sub(3) === 12);
