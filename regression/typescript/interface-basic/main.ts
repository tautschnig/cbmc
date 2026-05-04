// TSH: Object Types.md — Interfaces
// ES2024 sec-object-initializer: Object Initializer
// ES2024 sec-property-accessors: Property Accessors
interface Point {
  x: number;
  y: number;
}

const origin: Point = { x: 3, y: 4 };
console.assert(origin.x === 3);
console.assert(origin.y === 4);
console.assert(origin.x + origin.y === 7);
