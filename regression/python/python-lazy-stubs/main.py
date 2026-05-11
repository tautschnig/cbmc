# --python-lazy-stubs smoke test. Imports a module whose
# body would have assertions; with lazy stubs those
# assertions are never converted so the call returns nondet
# instead of failing.


from configparser import ConfigParser

cp = ConfigParser()
# The body of ConfigParser's methods isn't converted under
# --python-lazy-stubs, so this call returns nondet. No
# embedded assertions fire.
sections = cp.sections()
