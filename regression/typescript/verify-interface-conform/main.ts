interface Shape {
  area(): number;
}
class Square {
  side: number;
  constructor(s: number) { this.side = s; }
  area(): number { return this.side * this.side; }
}
const sq = new Square(5);
console.assert(sq.area() === 25);
