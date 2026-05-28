// Taint analysis test: a constant string flows to the sink.
// Since no taint source is called, the assertion holds.
function eval_sink(s: string): void { console.assert(s.length >= 0); }

function harness(): void {
    const data: string = "developer-controlled-constant";
    eval_sink(data);
}
harness();
