type Shape = { kind: "circle"; radius: number } | { kind: "square"; side: number };
const c: Shape = { kind: "circle", radius: 5 };
console.assert(c.kind === "circle");
