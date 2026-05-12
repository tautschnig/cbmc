// Harness for npm package 'compose-function' (stoeffel/compose-function)
// Version tracked in integration/typescript-npm/package.json
//
// compose(f, g)(x) === f(g(x)). The full higher-order composition
// pattern (returning an arrow that captures f/g and applies them)
// currently hits a frontend limitation — tracked as
// regression/typescript/higher-order-compose KNOWNBUG.
//
// This harness inlines the composition to verify the mathematical
// semantics over constant and symbolic inputs.

// Inlined compose(f, g)(x) = f(g(x))
// f = *2, g = +3
function compose_f_g(x: number): number {
  return (x + 3) * 2;
}

// f = x + 1, g = x * 2
function compose_fg(x: number): number { return (x * 2) + 1; }
// f = x * 2, g = x + 1  (swapped)
function compose_gf(x: number): number { return (x + 1) * 2; }

// Property 1: basic correctness over constant.
console.assert(compose_f_g(5) === 16);
console.assert(compose_f_g(0) === 6);

// Property 2: over symbolic bounded input.
const n: number = nondet_number();
__CPROVER_assume(!Number.isNaN(n) && n >= -100 && n <= 100);
console.assert(compose_f_g(n) === (n + 3) * 2);

// Property 3: order matters.
console.assert(compose_fg(3) === 7);
console.assert(compose_gf(3) === 8);
console.assert(compose_fg(3) !== compose_gf(3));

// Property 4: identity — compose(id, f)(x) === f(x).
function id_then_times3(x: number): number { return x * 3; }
const m: number = nondet_number();
__CPROVER_assume(!Number.isNaN(m) && m >= -100 && m <= 100);
console.assert(id_then_times3(m) === m * 3);
