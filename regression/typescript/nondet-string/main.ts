declare function nondet_string(): string;
const s: string = nondet_string();
__CPROVER_assume(s.length > 0);
__CPROVER_assume(s.length < 100);
console.assert(s.length > 0);
console.assert(s.length < 100);
