// ES2024 sec-class-definitions: Class Definitions
// TSH: Classes.md
class Counter {
  count: number;
  constructor(initial: number) {
    this.count = initial;
  }
  increment(): void {
    this.count++;
  }
  getCount(): number {
    return this.count;
  }
}
const c = new Counter(0);
c.increment();
c.increment();
c.increment();
console.assert(c.getCount() === 3);
