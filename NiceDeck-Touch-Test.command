#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
echo 'Close the NiceDeck Mac editor before starting this test.'
read -r -p 'Press Return when the editor is closed. ' _nicedeck_ready
read -r -p 'NiceDeck USB port (for example /dev/cu.usbmodemPORT): ' _nicedeck_port
mkdir -p build/touch-diagnostics
python3 scripts/touch-test.py guided --port "$_nicedeck_port" --output "build/touch-diagnostics/test-$(date +%Y%m%d-%H%M%S).json"
read -r -p 'Test finished. Press Return to close. ' _nicedeck_finished
