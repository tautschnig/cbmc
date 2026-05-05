// Interface-typed array with reduce
interface Shape { area: number; }
const shapes: Shape[] = [{ area: 10 }, { area: 20 }, { area: 30 }];
const total: number = shapes.reduce((sum: number, s: Shape): number => sum + s.area, 0);
console.assert(total === 60);
