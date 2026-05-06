class Counter {
  value: number;
  max: number;
  constructor(m: number) { this.value = 0; this.max = m; }
  increment(): boolean {
    if (this.value < this.max) {
      this.value = this.value + 1;
      return true;
    }
    return false;
  }
  reset(): void { this.value = 0; }
}
const c = new Counter(3);
console.assert(c.increment() === true);
console.assert(c.increment() === true);
console.assert(c.increment() === true);
console.assert(c.increment() === false);
c.reset();
console.assert(c.value === 0);
