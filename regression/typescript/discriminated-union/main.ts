type Shape = { kind: "circle"; radius: number } | { kind: "square"; side: number };
function area(s: Shape): number {
  if (s.kind === "circle") return s.radius * s.radius * 3;
  return s.side * s.side;
}
const c: Shape = { kind: "circle", radius: 5 };
console.assert(area(c) === 75);
