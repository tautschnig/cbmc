// Regression: destructuring with default values (= N) uses the
// default when the source property is undefined (NaN in our model).
// ES2024 §14.3.3.3 (DestructuringAssignmentTarget with initializer).

// Default used when source property missing (source.y = NaN)
const a: { x: number; y?: number } = { x: 42 };
const { x: ax, y: ay = 99 } = a;
console.assert(ax === 42);
console.assert(ay === 99);

// Default NOT used when source property present
const b: { x: number; y?: number } = { x: 1, y: 7 };
const { x: bx, y: by = 99 } = b;
console.assert(bx === 1);
console.assert(by === 7);

// Default used when source property is explicitly undefined
const c: { x: number; z?: number } = { x: 5 };
const { z = 123 } = c;
console.assert(z === 123);
