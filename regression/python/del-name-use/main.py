# PLR §7.5: `del x` unbinds the name, so a subsequent read (before any
# reassignment) raises NameError. Modelled via a per-name deleted flag set on
# `del`, cleared on assignment, checked at reads. CPython: NameError; FAILED.
x = 5
del x
y = x
