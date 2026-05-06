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
class C extends B {
  z: number;
  constructor(x: number, y: number, z: number) { super(x, y); this.z = z; }
  sum(): number { return this.x + this.y + this.z; }
}
const c = new C(1, 2, 3);
console.assert(c.getX() === 1);
console.assert(c.getY() === 2);
console.assert(c.sum() === 6);
