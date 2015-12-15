#/bin/bash

# Name: t.imap.id.sh
# Author: Ladar Levison
#
# Description: Used for testing IMAP ID command.

echo ""
tput setaf 6; echo "IMAP ID Command:"; tput sgr0
echo ""
printf "A01 ID\r\nA04 LOGOUT\r\n" | nc localhost 9000
printf "A01 ID (\"program\" \"mrbig\")\r\nA04 LOGOUT\r\n" | nc localhost 9000
