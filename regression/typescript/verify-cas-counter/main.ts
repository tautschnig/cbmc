class AtomicCounter {
  value: number;
  constructor() { this.value = 0; }
  cas(expected: number, newVal: number): boolean {
    if (this.value === expected) {
      this.value = newVal;
      return true;
    }
    return false;
  }
}
const c = new AtomicCounter();
console.assert(c.cas(0, 1) === true);
console.assert(c.value === 1);
console.assert(c.cas(0, 2) === false); // expected doesn't match
console.assert(c.value === 1); // unchanged
console.assert(c.cas(1, 5) === true);
console.assert(c.value === 5);
