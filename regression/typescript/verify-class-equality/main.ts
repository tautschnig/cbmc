class Point {
  x: number;
  y: number;
  constructor(x: number, y: number) { this.x = x; this.y = y; }
  equals(other: Point): boolean { return this.x === other.x && this.y === other.y; }
  distSq(): number { return this.x * this.x + this.y * this.y; }
}
const p1 = new Point(3, 4);
const p2 = new Point(3, 4);
console.assert(p1.equals(p2) === true);
console.assert(p1.distSq() === 25);
