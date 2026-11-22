#!/bin/sh
# Serves the JUNO FM-1 installer site on http://localhost:8106 and opens it in Chrome (or Edge).
# Close this Terminal window (or press Ctrl-C) to stop the server.
cd "$(dirname "$0")/site" || { echo "no site folder next to this script"; exit 1; }
URL="http://localhost:8106/install/"
python3 -m http.server 8106 --bind 127.0.0.1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT INT TERM HUP
sleep 1
open -a "Google Chrome" "$URL" 2>/dev/null || open -a "Microsoft Edge" "$URL" 2>/dev/null || open "$URL"
echo "Installer at $URL - close this window to stop the server."
wait $SERVER
