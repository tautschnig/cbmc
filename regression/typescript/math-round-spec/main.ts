// ES2024 §21.3.2.29: Math.round rounds ties toward +infinity.
// std::round rounds ties away from zero, so we override.
console.assert(Math.round(0.5) === 1);
console.assert(Math.round(-0.5) === 0);  // NOT -1 per spec
console.assert(Math.round(2.5) === 3);
console.assert(Math.round(-2.5) === -2); // toward +infinity
console.assert(Math.round(0.4) === 0);
console.assert(Math.round(-0.4) === 0);
console.assert(Math.round(3.7) === 4);
console.assert(Math.round(-3.7) === -4);
