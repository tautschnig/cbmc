class Accumulator {
  total: number;
  count: number;
  constructor() { this.total = 0; this.count = 0; }
  add(x: number): void { this.total = this.total + x; this.count = this.count + 1; }
  average(): number { return this.total / this.count; }
}
const acc = new Accumulator();
acc.add(10);
acc.add(20);
acc.add(30);
console.assert(acc.count === 3);
console.assert(acc.total === 60);
