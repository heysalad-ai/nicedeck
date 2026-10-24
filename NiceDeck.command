#!/bin/bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [[ ! -x "$PROJECT_DIR/build/NiceDeck.app/Contents/MacOS/NiceDeck" ]]; then
  /bin/bash "$PROJECT_DIR/scripts/build-mac.sh"
fi
open "$PROJECT_DIR/build/NiceDeck.app"
