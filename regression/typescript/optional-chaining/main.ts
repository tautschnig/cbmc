// KNOWNBUG: o?.x doesn't short-circuit on null/undefined.
// ES2024 sec-optional-chains (§13.3.9)
interface Obj { x?: { y?: number; }; }
const o: Obj = {};
const v: number | undefined = o.x?.y;
console.assert(v === undefined);
