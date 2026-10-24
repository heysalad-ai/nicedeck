#!/bin/bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_DIR="$PROJECT_DIR/build/NiceDeck.app"
mkdir -p "$APP_DIR/Contents/MacOS" "$APP_DIR/Contents/Resources" "$PROJECT_DIR/build/swift-cache"
if [[ ! -f "$PROJECT_DIR/assets/AppIcon.icns" || "$PROJECT_DIR/scripts/make-app-icon.swift" -nt "$PROJECT_DIR/assets/AppIcon.icns" ]]; then
  xcrun swift -module-cache-path "$PROJECT_DIR/build/swift-cache" "$PROJECT_DIR/scripts/make-app-icon.swift" "$PROJECT_DIR/assets"
fi
cp "$PROJECT_DIR/assets/AppIcon.icns" "$APP_DIR/Contents/Resources/AppIcon.icns"
xcrun swiftc -swift-version 5 -O -target arm64-apple-macosx13.0 \
  -module-cache-path "$PROJECT_DIR/build/swift-cache" \
  -framework SwiftUI -framework AppKit -framework UniformTypeIdentifiers \
  "$PROJECT_DIR"/macos/*.swift -o "$APP_DIR/Contents/MacOS/NiceDeck"
cat > "$APP_DIR/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleName</key><string>NiceDeck</string>
<key>CFBundleDisplayName</key><string>NiceDeck</string>
<key>CFBundleIdentifier</key><string>com.nicedeck.mac</string>
<key>CFBundleExecutable</key><string>NiceDeck</string>
<key>CFBundleIconFile</key><string>AppIcon</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>1.0.2</string>
<key>CFBundleVersion</key><string>3</string>
<key>LSMinimumSystemVersion</key><string>13.0</string>
<key>NSHighResolutionCapable</key><true/>
<key>NSPrincipalClass</key><string>NSApplication</string>
</dict></plist>
PLIST
codesign --force --sign - --identifier com.nicedeck.mac "$APP_DIR"
echo "Built $APP_DIR"
