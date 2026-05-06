class Pair<T> {
  first: T;
  second: T;
  constructor(a: T, b: T) { this.first = a; this.second = b; }
  swap(): void { const t = this.first; this.first = this.second; this.second = t; }
}
const p = new Pair<number>(10, 20);
console.assert(p.first === 10);
p.swap();
console.assert(p.first === 20);
console.assert(p.second === 10);
