# PLR §7.3: an assert the front-end cannot encode must never be
# silently dropped — VERIFICATION SUCCESSFUL must not mean "I didn't
# encode your assertion" (a reported false-proof class). The backstop
# emits a definite-failure property with its own property class.
# __loader__ is a module attribute the front-end does not model, and
# its dunder form is exempt from the undefined-name NameError check,
# so the assert condition deterministically fails to encode.
def main():
    assert __loader__ is not None


main()
