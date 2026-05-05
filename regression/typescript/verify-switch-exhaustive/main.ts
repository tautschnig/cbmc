enum Color { R = 0, G = 1, B = 2 }
function toHex(c: Color): number {
  switch (c) {
    case Color.R: return 0xFF0000;
    case Color.G: return 0x00FF00;
    default: return 0x0000FF;
  }
}
console.assert(toHex(Color.R) === 0xFF0000);
console.assert(toHex(Color.G) === 0x00FF00);
console.assert(toHex(Color.B) === 0x0000FF);
