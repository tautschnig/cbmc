class Rect {
  w: number;
  h: number;
  constructor(w: number, h: number) { this.w = w; this.h = h; }
  area(): number { return this.w * this.h; }
  perimeter(): number { return 2 * (this.w + this.h); }
}
const r = new Rect(3, 4);
console.assert(r.area() === 12);
console.assert(r.perimeter() === 14);
