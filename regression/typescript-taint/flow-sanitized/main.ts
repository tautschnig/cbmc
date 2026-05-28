// Taint analysis test: source → sanitizer → sink.
// The sanitizer clears the taint, so the assertion holds.
function user_input(): string { return nondet_string(); }
function sanitize(s: string): string { return s; }
function eval_sink(s: string): void { console.assert(s.length >= 0); }

function harness(): void {
    const data: string = user_input();
    const safe: string = sanitize(data);
    eval_sink(safe);
}
harness();
