const t: boolean = true;
const f: boolean = false;
console.assert(t && t);
console.assert(!(t && f));
console.assert(t || f);
console.assert(!f);
