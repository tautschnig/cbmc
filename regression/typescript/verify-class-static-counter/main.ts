class Counter {
  value: number;
  constructor() { this.value = 0; }
  static zero(): Counter { return new Counter(); }
}
const c = new Counter();
c.value = 42;
console.assert(c.value === 42);
