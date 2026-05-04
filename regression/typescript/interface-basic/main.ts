// TSH: Object Types.md — Interfaces
// "An interface declaration is another way to name an object type."
//
// ES2024 sec-object-initializer: Object Initializer
// "An object initializer is an expression describing the initialization
//  of an Object, written in a form resembling a literal."
//
// ES2024 sec-property-accessors: Property Accessors
// "Properties are accessed by name, using either the dot notation or
//  the bracket notation."
//
// ES2024 sec-math.sqrt: Math.sqrt(x)
interface Point {
  x: number;
  y: number;
}

function distance(p: Point): number {
  return Math.sqrt(p.x * p.x + p.y * p.y);
}

const origin: Point = { x: 3, y: 4 };
console.assert(distance(origin) === 5);
