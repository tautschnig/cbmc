class MathUtil {
  x: number;
  constructor() { this.x = 0; }
  static add(a: number, b: number): number { return a + b; }
  static multiply(a: number, b: number): number { return a * b; }
}
console.assert(MathUtil.add(3, 4) === 7);
console.assert(MathUtil.multiply(3, 4) === 12);
