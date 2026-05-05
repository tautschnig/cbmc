function getX(opts: { x: number; y: number }): number {
  return opts.x + opts.y;
}
const result: number = getX({ x: 3, y: 4 });
console.assert(result === 7);
