class Counter {
  count: number;
  constructor(start: number) { this.count = start; }
  increment(): void { this.count = this.count + 1; }
  getCount(): number { return this.count; }
}
const a = new Counter(0);
const b = new Counter(10);
a.increment();
console.assert(a.getCount() === 1);
console.assert(b.getCount() === 10);
