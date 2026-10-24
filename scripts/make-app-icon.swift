#!/usr/bin/env swift
// Deterministic native vector drawing. Regenerate with:
// xcrun swift scripts/make-app-icon.swift assets
import AppKit

let destination = URL(fileURLWithPath: CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "assets", isDirectory: true)
let iconset = destination.appendingPathComponent("AppIcon.iconset", isDirectory: true)
try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)

func color(_ hex: UInt32, alpha: CGFloat = 1) -> NSColor {
    NSColor(srgbRed: CGFloat((hex >> 16) & 255) / 255,
            green: CGFloat((hex >> 8) & 255) / 255,
            blue: CGFloat(hex & 255) / 255, alpha: alpha)
}

func rounded(_ rect: NSRect, radius: CGFloat) -> NSBezierPath {
    NSBezierPath(roundedRect: rect, xRadius: radius, yRadius: radius)
}

func drawIcon(pixels: Int) throws -> Data {
    guard let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil,
        pixelsWide: pixels, pixelsHigh: pixels, bitsPerSample: 8,
        samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
        colorSpaceName: .deviceRGB, bytesPerRow: pixels * 4, bitsPerPixel: 32),
        let graphics = NSGraphicsContext(bitmapImageRep: bitmap) else {
        throw NSError(domain: "AppIcon", code: 1, userInfo: [NSLocalizedDescriptionKey: "Could not allocate icon bitmap"])
    }
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = graphics
    let context = graphics.cgContext
    context.setAllowsAntialiasing(true)
    context.setShouldAntialias(true)
    context.scaleBy(x: CGFloat(pixels) / 1024, y: CGFloat(pixels) / 1024)

    // The generous transparent margin keeps the icon aligned with macOS icons.
    let shell = rounded(NSRect(x: 80, y: 80, width: 864, height: 864), radius: 194)
    NSGraphicsContext.saveGraphicsState()
    let shadow = NSShadow()
    shadow.shadowColor = color(0x70594A, alpha: 0.19)
    shadow.shadowBlurRadius = 28
    shadow.shadowOffset = NSSize(width: 0, height: -13)
    shadow.set()
    color(0xF5EEE3).setFill()
    shell.fill()
    NSGraphicsContext.restoreGraphicsState()

    NSGradient(starting: color(0xFFF9F0), ending: color(0xEDE3D4))!.draw(in: shell, angle: -90)
    color(0xFFFFFF, alpha: 0.75).setStroke()
    let rim = rounded(NSRect(x: 84, y: 84, width: 856, height: 856), radius: 190)
    rim.lineWidth = 3
    rim.stroke()

    // Six tactile keys, with two quiet tones per row: coral, sage, blue.
    let keys: [(CGFloat, CGFloat, UInt32, UInt32)] = [
        (226, 654, 0xEAA18B, 0xD88D79), (540, 654, 0xEEB099, 0xDB9B84),
        (226, 422, 0xA7B8A3, 0x90A28D), (540, 422, 0xB9C4A6, 0xA3B090),
        (226, 190, 0x8EADC5, 0x7898B2), (540, 190, 0xA3BDCA, 0x8AA6B6)
    ]
    for (x, y, top, bottom) in keys {
        let key = rounded(NSRect(x: x, y: y, width: 258, height: 186), radius: 49)
        NSGraphicsContext.saveGraphicsState()
        let keyShadow = NSShadow()
        keyShadow.shadowColor = color(0x6D5948, alpha: 0.18)
        keyShadow.shadowBlurRadius = 12
        keyShadow.shadowOffset = NSSize(width: 0, height: -6)
        keyShadow.set()
        color(bottom).setFill()
        key.fill()
        NSGraphicsContext.restoreGraphicsState()
        NSGradient(starting: color(top), ending: color(bottom))!.draw(in: key, angle: -90)
        let keyRim = rounded(NSRect(x: x + 1.5, y: y + 1.5, width: 255, height: 183), radius: 47.5)
        color(0xFFFFFF, alpha: 0.22).setStroke()
        keyRim.lineWidth = 3
        keyRim.stroke()
    }
    NSGraphicsContext.restoreGraphicsState()
    guard let png = bitmap.representation(using: .png, properties: [:]) else {
        throw NSError(domain: "AppIcon", code: 2, userInfo: [NSLocalizedDescriptionKey: "Could not encode icon PNG"])
    }
    return png
}

for size in [16, 32, 128, 256, 512] {
    for scale in [1, 2] {
        let filename = "icon_\(size)x\(size)\(scale == 2 ? "@2x" : "").png"
        try drawIcon(pixels: size * scale).write(to: iconset.appendingPathComponent(filename), options: .atomic)
    }
}
try drawIcon(pixels: 1024).write(to: destination.appendingPathComponent("AppIcon-preview.png"), options: .atomic)
let iconutil = Process()
iconutil.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
iconutil.arguments = ["--convert", "icns", "--output", destination.appendingPathComponent("AppIcon.icns").path, iconset.path]
try iconutil.run()
iconutil.waitUntilExit()
guard iconutil.terminationStatus == 0 else {
    throw NSError(domain: "AppIcon", code: Int(iconutil.terminationStatus), userInfo: [NSLocalizedDescriptionKey: "iconutil failed"])
}
print("Generated \(destination.appendingPathComponent("AppIcon.icns").path)")
