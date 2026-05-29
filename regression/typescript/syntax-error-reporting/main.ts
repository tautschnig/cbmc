// Test file deliberately contains a syntax error to verify the
// frontend surfaces it with file:line:col diagnostics rather than
// silently producing a degenerate AST.
let x = ;
