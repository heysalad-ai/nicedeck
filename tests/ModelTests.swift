import Foundation

@main
struct ModelTests {
    static var count = 0
    static func check(_ condition: @autoclosure () throws -> Bool, _ message: String) throws {
        guard try condition() else { throw DeckValidationError(message: "TEST FAILED: \(message)") }
        count += 1
    }
    static func rejects(_ message: String, _ change: (inout DeckConfig) -> Void) throws {
        var config = DeckConfig.defaults
        change(&config)
        do {
            try config.validate()
        } catch { count += 1; return }
        throw DeckValidationError(message: "TEST FAILED: accepted \(message)")
    }

    static func checkBrandMigration() throws {
        try check(DeckIdentity.supports(device: "NiceDeck", protocolVersion: 1), "Accept renamed firmware")
        try check(DeckIdentity.supports(device: "OpenStreamDeck", protocolVersion: 1), "Accept legacy firmware during upgrade")
        try check(!DeckIdentity.supports(device: "NiceDeck", protocolVersion: 2), "Reject incompatible renamed firmware")
        try check(!DeckIdentity.supports(device: "OpenStreamDeck", protocolVersion: 2), "Reject incompatible legacy firmware")
        try check(!DeckIdentity.supports(device: "AnotherDeck", protocolVersion: 1), "Reject unrelated serial device")
        try check(!DeckIdentity.supports(device: nil, protocolVersion: nil), "Reject missing device identity")

        let files = FileManager.default
        let support = files.temporaryDirectory.appendingPathComponent("nicedeck-migration-\(UUID().uuidString)", isDirectory: true)
        defer { try? files.removeItem(at: support) }
        let source = DeckConfigurationStorage.legacyConfigURL(in: support)
        let destination = DeckConfigurationStorage.configURL(in: support)
        try DeckConfigurationStorage.migrateLegacyConfiguration(in: support)
        try check(!files.fileExists(atPath: destination.path), "Fresh installation does not invent a saved configuration")

        var legacy = DeckConfig.defaults
        legacy.brightness = 47
        legacy.pages[0].name = "My custom deck"
        let legacyData = try legacy.encoded()
        try files.createDirectory(at: source.deletingLastPathComponent(), withIntermediateDirectories: true)
        try legacyData.write(to: source)
        try DeckConfigurationStorage.migrateLegacyConfiguration(in: support)
        try check(try Data(contentsOf: destination) == legacyData, "Migration preserves the user's exact configuration")
        try check(try Data(contentsOf: source) == legacyData, "Migration retains the legacy backup unchanged")
        try DeckConfigurationStorage.migrateLegacyConfiguration(in: support)
        try check(try Data(contentsOf: destination) == legacyData, "Repeated migration is harmless")

        var newer = legacy
        newer.brightness = 63
        let newerData = try newer.encoded()
        try newerData.write(to: destination)
        try DeckConfigurationStorage.migrateLegacyConfiguration(in: support)
        try check(try Data(contentsOf: destination) == newerData, "Existing NiceDeck configuration takes precedence")
        try check(try Data(contentsOf: source) == legacyData, "Later launches do not change the legacy backup")
    }

    static func main() throws {
        try checkBrandMigration()
        let defaults = DeckConfig.defaults
        try defaults.validate()
        let decoded = try JSONDecoder().decode(DeckConfig.self, from: defaults.encoded())
        try check(defaults == decoded, "JSON roundtrip preserves complete configuration")
        try check(defaults.pages.flatMap(\.buttons).flatMap(\.actions).allSatisfy { !$0.type.isHostAction }, "Defaults contain only standalone device actions")
        for hotkey in ["cmd+c", "cmd+shift+4", "ctrl+alt+delete", "F24", "F13", "cmd++", "+", "space", "option+left"] {
            try check(DeckConfig.validHotkey(hotkey), "Valid hotkey \(hotkey)")
        }
        for hotkey in ["", "cmd+", "command+c", "f25", "f0", "cmd+banana", "a+b", "cmd++c", "cmd+é"] {
            try check(!DeckConfig.validHotkey(hotkey), "Reject hotkey \(hotkey)")
        }
        try rejects("version") { $0.version = 2 }
        try rejects("zero pages") { $0.pages = [] }
        try rejects("nine pages") { config in config.pages = (0..<9).map { i in DeckPage(id: "page\(i)") } }
        try rejects("duplicate page IDs") { $0.pages[1].id = $0.pages[0].id }
        try rejects("five buttons") { $0.pages[0].buttons.removeLast() }
        try rejects("duplicate button IDs") { $0.pages[0].buttons[1].id = $0.pages[0].buttons[0].id }
        try rejects("brightness") { $0.brightness = 101 }
        try rejects("sleep") { $0.sleepSeconds = 1 }
        try rejects("non-ASCII label") { $0.pages[0].buttons[0].label = "🎵" }
        try rejects("long label") { $0.pages[0].buttons[0].label = String(repeating: "a", count: 25) }
        try rejects("color") { $0.pages[0].buttons[0].color = "red" }
        try rejects("icon") { $0.pages[0].buttons[0].icon = "missing" }
        try rejects("image") { $0.pages[0].buttons[0].image = Data([1, 2, 3]).base64EncodedString() }
        try rejects("long value") { $0.pages[0].buttons[0].actions = [DeckAction(type: .text, value: String(repeating: "a", count: 513))] }
        try rejects("null") { $0.pages[0].buttons[0].actions = [DeckAction(type: .shell, value: "echo\u{0}hi")] }
        try rejects("nine actions") { $0.pages[0].buttons[0].actions = Array(repeating: DeckAction(), count: 9) }
        try rejects("non-ASCII text") { $0.pages[0].buttons[0].actions = [DeckAction(type: .text, value: "café")] }
        try rejects("bad URL") { $0.pages[0].buttons[0].actions = [DeckAction(type: .url, value: "javascript:alert(1)")] }
        try rejects("relative application path") { $0.pages[0].buttons[0].actions = [DeckAction(type: .app, value: "../app.app")] }
        try rejects("empty shell") { $0.pages[0].buttons[0].actions = [DeckAction(type: .shell, value: " ")] }
        try rejects("missing page target") { $0.pages[0].buttons[0].actions = [DeckAction(type: .page, value: "missing")] }
        try rejects("negative delay") { $0.pages[0].buttons[0].actions[0].delayMs = -1 }
        var large = DeckConfig(pages: (0..<8).map { i in DeckPage(id: "p\(i)") })
        let image = Data(repeating: 127, count: 3200).base64EncodedString()
        for p in large.pages.indices { for b in large.pages[p].buttons.indices { large.pages[p].buttons[b].image = image } }
        try large.validate()
        try check(try large.encoded().count > 200000, "Maximum custom images are exercised")
        var config = defaults
        config.pages[0].buttons[0].actions = [DeckAction(type: .url, value: "https://example.com")]
        try check(HostActionResolver.resolve(config: config, page: 0, button: 0, step: 0)?.value == "https://example.com", "Host commands resolved from snapshot")
        try check(HostActionResolver.resolve(config: nil, page: 0, button: 0, step: 0) == nil, "No commands without snapshot")
        try check(HostActionResolver.resolve(config: config, page: -1, button: 0, step: 0) == nil, "Negative page rejected")
        try check(HostActionResolver.resolve(config: config, page: 0, button: 6, step: 0) == nil, "Out of bounds button rejected")
        try check(HostActionResolver.resolve(config: config, page: 0, button: 0, step: 9) == nil, "Out of bounds step rejected")
        try check(HostActionResolver.resolve(config: defaults, page: 0, button: 0, step: 0) == nil, "HID action not executed as host command")
        print("Passed \(count) model, migration, compatibility, and command-resolution checks")
    }
}
