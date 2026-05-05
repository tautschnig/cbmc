interface Shape {
  area: number;
}
function totalArea(shapes: Shape[]): number {
  return shapes.reduce((sum: number, s: Shape): number => sum + s.area, 0);
}
const shapes: Shape[] = [{ area: 10 }, { area: 20 }, { area: 30 }];
console.assert(totalArea(shapes) === 60);
