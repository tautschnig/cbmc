// ES2024 §28.2: Proxy with inline-literal handler.
//
// `new Proxy(target, handler)` constructions where `handler` is an
// inline ObjectLiteralExpression (or an Identifier whose
// initialiser is one) now route property reads through the
// handler's `get` trap and writes through the `set` trap.
//
// Scope (P3.3 in typescript-remaining-work-plan.md):
//   - get and set traps only.
//   - Handler must be inline / declared-as-literal at call site.
//   - Other traps (has, deleteProperty, apply, construct, …) and
//     dynamic-handler patterns still use the pragmatic identity
//     model (Proxy = target).

function main(): void {
    // (a) get trap fires with the right key, returning a value
    // independent of target.
    const target = { v: 42 };
    const p = new Proxy(target, {
        get(t: any, key: string): number {
            console.assert(key === "v");
            return 999; // unrelated to target.v
        }
    });
    const x = p.v;
    console.assert(x === 999);

    // (b) Handler stored in a variable still works.
    const handler = {
        get(t: any, key: string): number {
            return 7;
        },
        set(t: any, key: string, value: number): boolean {
            console.assert(key === "v");
            return true;
        }
    };
    const target2 = { v: 0 };
    const q = new Proxy(target2, handler);
    console.assert(q.v === 7);
    q.v = 100; // routes through set trap; trap asserts key === "v"
}
main();
