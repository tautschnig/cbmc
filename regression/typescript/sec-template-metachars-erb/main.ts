// Security: no-template-metachars catches "<%...%>" (ERB-style).
function main(): void {
    __CPROVER_assert_no_template_metachars("Hello <%= attacker %>");
}
main();
