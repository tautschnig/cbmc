class Box {
  private_width: number;
  private_height: number;
  constructor(w: number, h: number) {
    this.private_width = w;
    this.private_height = h;
  }
  getArea(): number { return this.private_width * this.private_height; }
  getPerimeter(): number { return 2 * (this.private_width + this.private_height); }
}
const b = new Box(5, 3);
console.assert(b.getArea() === 15);
console.assert(b.getPerimeter() === 16);
