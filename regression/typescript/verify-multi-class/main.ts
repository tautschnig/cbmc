class Point {
  x: number;
  y: number;
  constructor(x: number, y: number) { this.x = x; this.y = y; }
}
class Line {
  start: number;
  end: number;
  constructor(s: number, e: number) { this.start = s; this.end = e; }
  length(): number { return this.end - this.start; }
}
const l = new Line(3, 10);
console.assert(l.length() === 7);
const p = new Point(1, 2);
console.assert(p.x + p.y === 3);
