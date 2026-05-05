interface HasArea {
  width: number;
  height: number;
}
function area(shape: HasArea): number {
  return shape.width * shape.height;
}
const rect: HasArea = { width: 5, height: 3 };
console.assert(area(rect) === 15);
