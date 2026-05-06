interface Point { x: number; y: number; }
function distance(p1: Point, p2: Point): number {
  const dx: number = p1.x - p2.x;
  const dy: number = p1.y - p2.y;
  return dx * dx + dy * dy;
}
const a: Point = { x: 0, y: 0 };
const b: Point = { x: 3, y: 4 };
console.assert(distance(a, b) === 25);
