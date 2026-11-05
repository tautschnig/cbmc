#!/bin/sh
# Expansions with side effects in redirections: command substitutions in
# here-documents of utilities, and assignments in those of builtins.
cat <<EOF
version $(uname -r)
EOF
unset x
: <<EOF
${x=here}
EOF
__CPROVER_assert "$x" = here
unset y
echo hi > /dev/null${y=}
__CPROVER_assert "${y-unset}" = ""
