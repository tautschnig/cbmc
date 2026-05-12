// KNOWNBUG: Chained string concat inside a reusable function
// triggers 'cannot associate two different arrays to the same
// pointer' in the refined-string solver. Root cause: when the
// same helper function is called multiple times, each call
// generates associate(data_sym, ptr_sym) with the SAME
// conversion-time symbols but DIFFERENT runtime SSA versions.
// The solver's pointer normalization apparently collapses the
// SSA versions, triggering the invariant.
//
// Simple single-concat works; only multi-call patterns fail.
function cls2(a: string, b: string): string {
  return a + " " + b;
}
let result: string = "a";
result = cls2(result, "bb");
result = cls2(result, "ccc");
console.assert(result.length === 8);
