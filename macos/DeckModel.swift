import Foundation

enum DeckIdentity {
    static let name = "NiceDeck"

    static func supports(device: String?, protocolVersion: Int?) -> Bool {
        protocolVersion == 1 && (device == name || device == "OpenStreamDeck")
    }
}

enum DeckConfigurationStorage {
    static func configURL(in applicationSupport: URL) -> URL {
        applicationSupport.appendingPathComponent(DeckIdentity.name, isDirectory: true).appendingPathComponent("config.json")
    }

    static func legacyConfigURL(in applicationSupport: URL) -> URL {
        applicationSupport.appendingPathComponent("OpenStreamDeck", isDirectory: true).appendingPathComponent("config.json")
    }

    /// Copy the former app's draft once, retaining the original as a backup.
    static func migrateLegacyConfiguration(in applicationSupport: URL, fileManager: FileManager = .default) throws {
        let destination = configURL(in: applicationSupport)
        let source = legacyConfigURL(in: applicationSupport)
        guard !fileManager.fileExists(atPath: destination.path), fileManager.fileExists(atPath: source.path) else { return }
        try fileManager.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
        // copyItem refuses to replace a destination created by another launch in the meantime.
        try fileManager.copyItem(at: source, to: destination)
    }
}

enum ActionType: String, Codable, CaseIterable, Identifiable {
    case none, hotkey, text, media, url, app, shell, shortcut, page, delay
    var id: String { rawValue }
    var isHostAction: Bool { [.url, .app, .shell, .shortcut].contains(self) }
    var title: String {
        switch self {
        case .none: return "No action"
        case .hotkey: return "Keyboard shortcut"
        case .text: return "Type text"
        case .media: return "Media control"
        case .url: return "Open website"
        case .app: return "Open application"
        case .shell: return "Run shell command"
        case .shortcut: return "Run Apple Shortcut"
        case .page: return "Switch page"
        case .delay: return "Wait"
        }
    }
}

struct DeckAction: Codable, Equatable {
    var type: ActionType = .none
    var value: String = ""
    var delayMs: Int = 0
}

struct DeckButton: Codable, Equatable, Identifiable {
    var id: String = UUID().uuidString.replacingOccurrences(of: "-", with: "")
    var label: String = "New button"
    var color: String = "#4466EE"
    var icon: String = "key"
    var image: String = ""
    var actions: [DeckAction] = []

    static let icons = ["key", "copy", "paste", "mic", "video", "play", "volume", "music", "globe", "app", "terminal", "bolt", "folder", "star", "heart", "gear"]
    static func empty(index: Int) -> DeckButton { DeckButton(label: "Button \(index + 1)", color: "#334155", icon: "key") }
}

struct DeckPage: Codable, Equatable, Identifiable {
    var id: String = UUID().uuidString.replacingOccurrences(of: "-", with: "")
    var name: String = "New page"
    var buttons: [DeckButton] = (0..<6).map { DeckButton.empty(index: $0) }
}

struct DeckConfig: Codable, Equatable {
    var version: Int = 1
    var brightness: Int = 80
    var sleepSeconds: Int = 300
    var pages: [DeckPage]

    static var defaults: DeckConfig {
        func button(_ id: String, _ label: String, _ icon: String, _ value: String, _ color: String = "#4466EE", type: ActionType = .hotkey) -> DeckButton {
            DeckButton(id: id, label: label, color: color, icon: icon, actions: [DeckAction(type: type, value: value)])
        }
        return DeckConfig(pages: [
            DeckPage(id: "main", name: "Everyday", buttons: [
                button("copy", "Copy", "copy", "cmd+c"),
                button("paste", "Paste", "paste", "cmd+v"),
                button("undo", "Undo", "bolt", "cmd+z", "#7657CC"),
                button("save", "Save", "folder", "cmd+s", "#7657CC"),
                button("screenshot", "Screenshot", "star", "cmd+shift+4", "#008B83"),
                button("media", "Media", "music", "media", "#008B83", type: .page)
            ]),
            DeckPage(id: "media", name: "Media", buttons: [
                button("previous", "Previous", "music", "previous", "#D67A36", type: .media),
                button("play", "Play / Pause", "play", "play_pause", "#D67A36", type: .media),
                button("next", "Next", "music", "next", "#BD4C73", type: .media),
                button("mute", "Mute", "mic", "mute", "#BD4C73", type: .media),
                button("volume_down", "Volume -", "volume", "volume_down", "#008B83", type: .media),
                button("volume_up", "Volume +", "volume", "volume_up", "#008B83", type: .media)
            ])
        ])
    }

    func encoded() throws -> Data {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys, .withoutEscapingSlashes]
        return try encoder.encode(self)
    }

    func validate() throws {
        func require(_ condition: Bool, _ message: String) throws {
            if !condition { throw DeckValidationError(message: message) }
        }
        try require(version == 1, "Only configuration version 1 is supported.")
        try require((5...100).contains(brightness), "Brightness must be between 5 and 100%.")
        try require(sleepSeconds == 0 || (30...3600).contains(sleepSeconds), "Sleep must be 0 (never), or 30–3600 seconds.")
        try require((1...8).contains(pages.count), "A deck needs 1–8 pages.")
        let pageIDs = Set(pages.map(\.id))
        try require(pageIDs.count == pages.count, "Page IDs must be unique.")
        for (pageIndex, page) in pages.enumerated() {
            let pageContext = "Page \(pageIndex + 1)"
            try require(Self.validID(page.id), "\(pageContext) needs a nonempty ASCII ID of at most 32 characters.")
            try require(Self.printableASCII(page.name, max: 24), "\(pageContext)'s name must be at most 24 printable ASCII characters.")
            try require(page.buttons.count == 6, "\(pageContext) must contain exactly six buttons.")
            try require(Set(page.buttons.map(\.id)).count == 6, "\(pageContext)'s button IDs must be unique.")
            for (buttonIndex, button) in page.buttons.enumerated() {
                let context = "\(pageContext), button \(buttonIndex + 1)"
                try require(Self.validID(button.id), "\(context) needs a nonempty ASCII ID of at most 32 characters.")
                try require(Self.printableASCII(button.label, max: 24), "\(context)'s label must be at most 24 printable ASCII characters.")
                try require(button.color.range(of: "^#[0-9A-Fa-f]{6}$", options: .regularExpression) != nil, "\(context)'s color must use #RRGGBB.")
                try require(DeckButton.icons.contains(button.icon), "\(context) has an unknown icon.")
                if !button.image.isEmpty {
                    try require(Data(base64Encoded: button.image)?.count == 3200, "\(context)'s image must be a 40×40 RGB565 image.")
                }
                try require(button.actions.count <= 8, "\(context) can have at most eight actions.")
                for (step, action) in button.actions.enumerated() {
                    let actionContext = "\(context), action \(step + 1)"
                    try require(action.value.utf8.count <= 512, "\(actionContext)'s value exceeds 512 bytes.")
                    try require(!action.value.utf8.contains(0), "\(actionContext) cannot contain a null character.")
                    try require((0...5000).contains(action.delayMs), "\(actionContext)'s delay must be 0–5000 ms.")
                    switch action.type {
                    case .none: break
                    case .hotkey:
                        try require(Self.validHotkey(action.value), "\(actionContext) needs a valid shortcut, such as cmd+shift+4.")
                    case .text:
                        try require(action.value.unicodeScalars.allSatisfy { (32...126).contains($0.value) || $0.value == 9 || $0.value == 10 }, "\(actionContext)'s typed text supports ASCII, tabs and newlines.")
                    case .media:
                        try require(Self.mediaValues.contains(action.value), "\(actionContext) needs a valid media control.")
                    case .url:
                        let url = URLComponents(string: action.value)
                        try require(["http", "https"].contains(url?.scheme?.lowercased() ?? "") && !(url?.host?.isEmpty ?? true) && !action.value.unicodeScalars.contains(where: { $0.value < 32 }), "\(actionContext) needs a complete http:// or https:// website address.")
                    case .app:
                        let value = action.value.trimmingCharacters(in: .whitespacesAndNewlines)
                        let validPath = value.hasPrefix("/") && value.lowercased().hasSuffix(".app")
                        let validName = !value.contains("/") && !value.hasPrefix("-")
                        try require(!value.isEmpty && (validPath || validName) && !value.contains("\n"), "\(actionContext) needs an application name or an absolute .app path.")
                    case .shell, .shortcut:
                        try require(!action.value.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty, "\(actionContext) needs a value.")
                    case .page:
                        try require(pageIDs.contains(action.value), "\(actionContext) refers to a page that does not exist.")
                    case .delay:
                        try require(action.value.isEmpty, "\(actionContext)'s wait action must have an empty value.")
                    }
                }
            }
        }
        try require(try encoded().count <= 280000, "The configuration exceeds the device's 280 KB limit.")
    }

    static let mediaValues = ["play_pause", "next", "previous", "mute", "volume_up", "volume_down", "stop"]

    static func validHotkey(_ raw: String) -> Bool {
        // Preserve a final literal '+' (cmd++), while rejecting empty tokens elsewhere.
        var value = raw.lowercased()
        let modifiers: Set<String> = ["cmd", "gui", "meta", "super", "ctrl", "control", "alt", "option", "shift"]
        while let separator = value.firstIndex(of: "+"), separator != value.startIndex {
            let prefix = String(value[..<separator])
            guard modifiers.contains(prefix) else { return false }
            value = String(value[value.index(after: separator)...])
        }
        if value.utf8.count == 1, let byte = value.utf8.first, (32...126).contains(byte) { return true }
        let names: Set<String> = ["space", "enter", "return", "tab", "escape", "esc", "backspace", "delete", "up", "down", "left", "right", "home", "end", "pageup", "pagedown"]
        if names.contains(value) { return true }
        if value.hasPrefix("f"), let number = Int(value.dropFirst()), (1...24).contains(number), value == "f\(number)" { return true }
        return false
    }

    private static func validID(_ value: String) -> Bool {
        !value.isEmpty && value.utf8.count <= 32 && value.unicodeScalars.allSatisfy { (33...126).contains($0.value) }
    }
    private static func printableASCII(_ value: String, max: Int) -> Bool {
        value.utf8.count <= max && value.unicodeScalars.allSatisfy { (32...126).contains($0.value) }
    }
}

struct DeckValidationError: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}

/// Resolves commands from a validated snapshot; incoming serial data only supplies indices.
enum HostActionResolver {
    static func resolve(config: DeckConfig?, page: Int, button: Int, step: Int) -> DeckAction? {
        guard let config, config.pages.indices.contains(page),
              config.pages[page].buttons.indices.contains(button),
              config.pages[page].buttons[button].actions.indices.contains(step) else { return nil }
        let action = config.pages[page].buttons[button].actions[step]
        return action.type.isHostAction ? action : nil
    }
}

/// One reading of the device's no-action touch check.
///
/// `alignment` errors are a measurement only: the firmware reports
/// `calibrationApplied == false` and stores nothing, so this verifies that
/// touch lands where it is drawn without becoming a calibration.
struct TouchCheckReport: Equatable {
    struct Target: Equatable {
        var expectedX = 0, expectedY = 0, x = 0, y = 0, dx = 0, dy = 0
        var measured = false
    }
    var enabled = false
    var mode = "off"
    var contacts = 0, downSamples = 0, readErrors = 0, bootPresses = 0, attempts = 0
    var rawX = 0, rawY = 0, x = 0, y = 0
    var down = false
    var minIntervalMs = 0, maxIntervalMs = 0
    var irqs = 0, polls = 0
    var lastReject = "none"
    var target = -1, hits = 0, radiusPx = 0
    var calibrationApplied = false
    var points: [Target] = []

    var alignmentMode: Bool { mode == "targets" }
    var complete: Bool { alignmentMode && target >= points.count && !points.isEmpty }

    init(_ object: [String: Any]) {
        func int(_ key: String) -> Int { (object[key] as? NSNumber)?.intValue ?? 0 }
        enabled = (object["enabled"] as? NSNumber)?.boolValue ?? false
        mode = object["mode"] as? String ?? "off"
        contacts = int("contacts"); downSamples = int("downSamples"); readErrors = int("readErrors")
        bootPresses = int("bootPresses"); attempts = int("attempts")
        rawX = int("rawX"); rawY = int("rawY"); x = int("x"); y = int("y")
        down = (object["down"] as? NSNumber)?.boolValue ?? false
        minIntervalMs = int("minIntervalMs"); maxIntervalMs = int("maxIntervalMs")
        irqs = int("irqs"); polls = int("polls")
        lastReject = object["lastReject"] as? String ?? "none"
        target = (object["target"] as? NSNumber)?.intValue ?? -1
        hits = int("hits"); radiusPx = int("radiusPx")
        calibrationApplied = (object["calibrationApplied"] as? NSNumber)?.boolValue ?? false
        points = ((object["points"] as? [[String: Any]]) ?? []).map { entry in
            func value(_ key: String) -> Int { (entry[key] as? NSNumber)?.intValue ?? 0 }
            var point = Target()
            point.expectedX = value("expectedX"); point.expectedY = value("expectedY")
            point.x = value("x"); point.y = value("y")
            point.dx = value("dx"); point.dy = value("dy")
            point.measured = (entry["measured"] as? NSNumber)?.boolValue ?? false
            return point
        }
    }
}
