// KNOWNBUG: Set.prototype.has returns nondet instead of tracking membership.
// Map.has works; Set.has doesn't. Set.size does work.
// ES2024 sec-set.prototype.has
const s: Set<number> = new Set();
s.add(1);
console.assert(s.has(1));
