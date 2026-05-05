class Counter {
  value: number;
  constructor() { this.value = 0; }
  inc(): void { this.value = this.value + 1; }
  reset(): void { this.value = 0; }
  get(): number { return this.value; }
}
const c = new Counter();
c.inc(); c.inc(); c.inc();
console.assert(c.get() === 3);
c.reset();
console.assert(c.get() === 0);
