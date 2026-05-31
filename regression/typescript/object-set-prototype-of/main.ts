// ES2024 §20.1.2.21: Object.setPrototypeOf(target, proto).
//
// Pragmatic model — see typescript-known-limitations §1.6. The
// common practical patterns are no-ops in our static-class-only
// world:
//
//   (a) Object.setPrototypeOf(this, ClassName.prototype) inside a
//       method whose `this` is already a ClassName — restoring the
//       prototype after super(). Already correct.
//   (b) Object.setPrototypeOf(obj, null) — defensive freeze.
//       Already correct: we don't carry prototype-chain inheritance.
//
// Genuine runtime rebindings emit a warning and are no-op'd.

class HttpError extends Error {
    public status: number;
    constructor(status: number, msg: string) {
        super(msg);
        this.status = status;
        // Pattern (a): the Error-subclass workaround, very common
        // in TS production code.
        Object.setPrototypeOf(this, HttpError.prototype);
    }
}

function freeze(o: object): void {
    // Pattern (b): defensive prototype freeze.
    Object.setPrototypeOf(o, null);
}

function main(): void {
    const e = new HttpError(404, "not found");
    console.assert(e.status === 404);

    const obj = { a: 1, b: 2 };
    freeze(obj);
    console.assert(obj.a === 1);

    // Reflect.setPrototypeOf is recognised the same way; returns
    // boolean true on success in our model.
    const c = new HttpError(500, "boom");
    const ok = Reflect.setPrototypeOf(c, HttpError.prototype);
    console.assert(ok === true);
    console.assert(c.status === 500);
}
main();
