class A {
  x: number;
  constructor(x: number) { this.x = x; }
  getX(): number { return this.x; }
}
class B extends A {
  y: number;
  constructor(x: number, y: number) { super(x); this.y = y; }
  getY(): number { return this.y; }
}
const b = new B(1, 2);
console.assert(b.getX() === 1);
console.assert(b.getY() === 2);
console.assert(b.x === 1);
console.assert(b.y === 2);
