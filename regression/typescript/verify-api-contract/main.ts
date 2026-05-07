// API contract: status must be consistent with data presence
interface Response { status: number; hasData: boolean; }
function makeResponse(success: boolean): Response {
  if (success) return { status: 200, hasData: true };
  return { status: 500, hasData: false };
}
const r: Response = makeResponse(true);
console.assert(r.status === 200);
console.assert(r.hasData === true);
// Invariant: hasData iff status < 400
const invariant1: boolean = r.hasData === (r.status < 400);
console.assert(invariant1 === true);

const r2: Response = makeResponse(false);
const invariant2: boolean = r2.hasData === (r2.status < 400);
console.assert(invariant2 === true);
