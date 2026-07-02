# KNOWNBUG (PLR §7.5): `del x` unbinds the name, so a subsequent read raises
# NameError. The frontend approximates `del x` by resetting the slot to the None
# marker (no name-binding tracking), so this false-proves SUCCESSFUL. A sound fix
# needs deleted-name tracking (whole-group with del-attr; see plan).
x = 5
del x
y = x
