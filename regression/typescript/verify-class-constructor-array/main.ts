class Matrix {
  rows: number;
  cols: number;
  constructor(r: number, c: number) { this.rows = r; this.cols = c; }
  size(): number { return this.rows * this.cols; }
}
const m = new Matrix(3, 4);
console.assert(m.size() === 12);
