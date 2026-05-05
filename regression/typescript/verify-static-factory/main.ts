class Pair {
  first: number;
  second: number;
  constructor(a: number, b: number) { this.first = a; this.second = b; }
  static of(a: number, b: number): Pair { return new Pair(a, b); }
  sum(): number { return this.first + this.second; }
}
const p = Pair.of(3, 7);
console.assert(p.sum() === 10);
