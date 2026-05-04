class Circle {
  readonly radius: number;
  constructor(r: number) { this.radius = r; }
  area(): number { return 3.14159 * this.radius * this.radius; }
}
const c = new Circle(5);
console.assert(c.radius === 5);
console.assert(c.area() > 78);
console.assert(c.area() < 79);
