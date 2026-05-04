class Shape {
  sides: number;
  constructor(s: number) { this.sides = s; }
  getSides(): number { return this.sides; }
}
class Triangle extends Shape {
  constructor() { super(3); }
}
const t = new Triangle();
console.assert(t.getSides() === 3);
