class Rectangle {
  width: number;
  height: number;
  constructor(w: number, h: number) { this.width = w; this.height = h; }
  get area(): number { return this.width * this.height; }
  get perimeter(): number { return 2 * (this.width + this.height); }
}
const r = new Rectangle(3, 4);
console.assert(r.area === 12);
console.assert(r.perimeter === 14);
