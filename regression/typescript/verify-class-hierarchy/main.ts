class Shape {
  sides: number;
  constructor(s: number) { this.sides = s; }
  getSides(): number { return this.sides; }
}
class Rectangle extends Shape {
  width: number;
  height: number;
  constructor(w: number, h: number) { super(4); this.width = w; this.height = h; }
  area(): number { return this.width * this.height; }
}
const r = new Rectangle(5, 3);
console.assert(r.getSides() === 4);
console.assert(r.area() === 15);
