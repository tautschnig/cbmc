class Counter {
  value: number;
  constructor() { this.value = 0; }
  increment(): void { this.value = this.value + 1; }
  decrement(): void { this.value = this.value - 1; }
}
const c = new Counter();
c.increment();
c.increment();
c.increment();
c.decrement();
console.assert(c.value === 2);
