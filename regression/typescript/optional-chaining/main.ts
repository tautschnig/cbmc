// ES2024 sec-optional-chains (§13.3.9): optional chaining returns
// undefined (our NaN sentinel) when the receiver is "not present".
//
// In our struct model, `const o: Obj = {}` fills missing optional
// fields with NaN-recursive defaults. So `o.x?.y` correctly reads
// NaN through the chain, and `v === undefined` becomes isNaN(v).
//
// Works for NAMED inner interface types. Nested INLINE types
// ({ y?: number }) have a remaining limitation tracked separately.

interface Inner { y?: number; }
interface Obj { x?: Inner; }
const o: Obj = {};
const v: number | undefined = o.x?.y;
console.assert(v === undefined);
