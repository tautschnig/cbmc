class Counter {
  value: number;
  constructor() { this.value = 0; }
  increment(): void { this.value = this.value + 1; }
  getCount(): number { return this.value; }
}
const c = new Counter();
c.increment();
c.increment();
c.increment();
console.assert(c.getCount() === 3);
