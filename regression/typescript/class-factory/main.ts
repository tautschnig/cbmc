class Point {
  x: number;
  y: number;
  constructor(x: number, y: number) { this.x = x; this.y = y; }
  sum(): number { return this.x + this.y; }
}
const p = new Point(3, 4);
console.assert(p.sum() === 7);
console.assert(p.x === 3);
console.assert(p.y === 4);
