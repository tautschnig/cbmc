class Circle {
  radius: number;
  constructor(r: number) { this.radius = r; }
  get diameter(): number { return this.radius * 2; }
  get area(): number { return this.radius * this.radius * 3; }
}
const c = new Circle(5);
console.assert(c.radius === 5);
console.assert(c.diameter === 10);
console.assert(c.area === 75);
