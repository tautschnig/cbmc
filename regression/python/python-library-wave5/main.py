# Smoke test for the wave 5 library stubs: random, configparser,
# struct. Each imports cleanly. Behavioural assertions are
# avoided because the stubs rely on dict / list ops that the
# frontend doesn't symex precisely.

# random
from random import random, randint, Random
x = random()
y = randint(0, 10)
r = Random()

# configparser
from configparser import ConfigParser, DEFAULTSECT, Error
cp = ConfigParser()

# struct: just import the surface.
from struct import pack, unpack, Struct, error
