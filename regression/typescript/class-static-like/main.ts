class MathUtils {
  value: number;
  constructor(v: number) { this.value = v; }
  double(): number { return this.value * 2; }
  isPositive(): boolean { return this.value > 0; }
}
const m = new MathUtils(5);
console.assert(m.double() === 10);
console.assert(m.isPositive() === true);
