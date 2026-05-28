// Integration test: defensive _.template usage with only developer-
// controlled, static keys reaching the Function() sink.
//
// Mirrors the lodash@4.18.0 contract: importsKeys are constants.

function harness_safe(): void {
    // Application uses only static, developer-controlled keys.
    const helpersKey: string = "helpers";
    const escapeKey: string = "escape";
    __CPROVER_assert_constant_string(helpersKey);
    __CPROVER_assert_constant_string(escapeKey);
}
harness_safe();
