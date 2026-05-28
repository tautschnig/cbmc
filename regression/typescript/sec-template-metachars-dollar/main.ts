// Security: no-template-metachars catches "${...}" injection pattern.
function main(): void {
    __CPROVER_assert_no_template_metachars("Hello ${attacker_payload}");
}
main();
