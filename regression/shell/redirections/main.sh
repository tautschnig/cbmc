#!/bin/sh
echo out > /tmp/f 2>&1
cat < /tmp/f >> /tmp/g
exec 3<> /tmp/h
cat <<EOT
hello $HOME
EOT
cat <<'EOT'
literal $HOME
EOT
