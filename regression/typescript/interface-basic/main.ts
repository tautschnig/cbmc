// TypeScript Handbook: Interfaces
interface Point {
  x: number;
  y: number;
}

function distance(p: Point): number {
  return Math.sqrt(p.x * p.x + p.y * p.y);
}

const origin: Point = { x: 3, y: 4 };
console.assert(distance(origin) === 5);
