// Real-world harness: email validation.
// Exercises: indexOf, slice, length, charCodeAt, comparison.
// Property: validates structural requirements of email addresses.

function isValidEmail(email: string): boolean {
    // Must contain @
    const atPos = email.indexOf("@");
    if (atPos < 1) return false;  // @ must not be first char

    // Extract local and domain parts
    const local = email.slice(0, atPos);
    const domain = email.slice(atPos + 1);

    // Domain must not contain another @
    if (domain.indexOf("@") >= 0) return false;

    // Domain must contain a dot
    const dotPos = domain.indexOf(".");
    if (dotPos < 1) return false;  // dot must not be first in domain

    // Domain must not end with dot
    if (domain.charCodeAt(domain.length - 1) === 46) return false;  // '.'

    // Minimum lengths
    if (local.length < 1) return false;
    if (domain.length < 3) return false;  // at least "x.y"

    return true;
}

function main(): void {
    // Valid emails
    console.assert(isValidEmail("user@example.com"));
    console.assert(isValidEmail("a@b.co"));
    console.assert(isValidEmail("test.name@domain.org"));

    // Invalid emails
    console.assert(!isValidEmail("@example.com"));     // no local part
    console.assert(!isValidEmail("user@@domain.com")); // double @
    console.assert(!isValidEmail("user@domain"));      // no dot in domain
    console.assert(!isValidEmail("user@.com"));        // dot first in domain
    console.assert(!isValidEmail("u@d."));             // dot last in domain
}
main();
