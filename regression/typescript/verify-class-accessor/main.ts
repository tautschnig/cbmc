class Range {
  min: number;
  max: number;
  constructor(min: number, max: number) { this.min = min; this.max = max; }
  contains(x: number): boolean { return x >= this.min && x <= this.max; }
  width(): number { return this.max - this.min; }
}
const r = new Range(10, 50);
console.assert(r.contains(25) === true);
console.assert(r.contains(5) === false);
console.assert(r.width() === 40);
