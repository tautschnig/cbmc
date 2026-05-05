enum Shape { Circle = 0, Square = 1, Triangle = 2 }
function sides(s: Shape): number {
  switch (s) {
    case Shape.Circle: return 0;
    case Shape.Square: return 4;
    default: return 3;
  }
}
console.assert(sides(Shape.Circle) === 0);
console.assert(sides(Shape.Square) === 4);
console.assert(sides(Shape.Triangle) === 3);
