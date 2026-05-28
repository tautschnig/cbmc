// Taint analysis test: untrusted input flows directly to a sink.
// Expected: taint analysis instruments an assertion at eval_sink;
// CBMC reports it as failing.
function user_input(): string { return nondet_string(); }
function eval_sink(s: string): void { console.assert(s.length >= 0); }

function harness(): void {
    const data: string = user_input();
    eval_sink(data);
}
harness();
