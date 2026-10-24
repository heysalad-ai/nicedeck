#!/bin/bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$PROJECT_DIR/build/swift-cache"
xcrun swiftc -swift-version 5 -module-cache-path "$PROJECT_DIR/build/swift-cache" \
  "$PROJECT_DIR/macos/DeckModel.swift" "$PROJECT_DIR/tests/ModelTests.swift" \
  -o "$PROJECT_DIR/build/model-tests"
"$PROJECT_DIR/build/model-tests"
