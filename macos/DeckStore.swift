import AppKit
import Combine
import Foundation
import UniformTypeIdentifiers

final class DeckStore: ObservableObject {
    @Published var config: DeckConfig { didSet { configChanged() } }
    @Published var selectedPageIndex = 0
    @Published var selectedButtonIndex = 0
    @Published var selectedPort = "" { didSet { if selectedPort != oldValue { reconnect() } } }
    @Published private(set) var ports: [String] = []
    @Published private(set) var connected = false
    @Published private(set) var isSyncing = false
    @Published private(set) var connectionStatus = "Looking for your deck…"
    @Published private(set) var syncState = "Saved locally • device offline"
    @Published var errorMessage: String?
    /// Live readout of the device's no-action touch check, or nil when it is off.
    @Published private(set) var touchCheck: TouchCheckReport?

    private let transport = SerialTransport()
    private let executor = HostActionExecutor()
    private var pollTimer: Timer?
    private var saveWork: DispatchWorkItem?
    private var terminationObserver: NSObjectProtocol?
    private var activeSession: UUID?
    private var activePort: String?
    private var acknowledgedConfig: DeckConfig?
    private var locallyAuthorizedConfig: DeckConfig?
    private var pending: [String: Request] = [:]
    private var actionResults: [String: (Bool, String?)] = [:]
    private var actionsInFlight = Set<String>()
    private var candidateIndex = 0
    private var lastScan = Date.distantPast
    private var nextConnect = Date.distantPast
    private var lastHeartbeat = Date.distantPast
    private var lastTouchCheckPoll = Date.distantPast
    private var saveURL: URL

    private struct Request {
        var operation: String
        var deadline: Date
        var snapshot: DeckConfig?
    }

    init() {
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        saveURL = DeckConfigurationStorage.configURL(in: support)
        var loadURL = saveURL
        var loaded = DeckConfig.defaults
        var loadError: String?
        do {
            try DeckConfigurationStorage.migrateLegacyConfiguration(in: support)
        } catch {
            // A failed migration must still open the user's existing draft.
            loadURL = FileManager.default.fileExists(atPath: saveURL.path) ? saveURL : DeckConfigurationStorage.legacyConfigURL(in: support)
            loadError = "Could not copy your previous configuration into NiceDeck: \(error.localizedDescription)"
        }
        if FileManager.default.fileExists(atPath: loadURL.path) {
            do {
                let data = try Data(contentsOf: loadURL)
                guard data.count <= 600000 else { throw DeckValidationError(message: "The saved configuration is too large.") }
                loaded = try JSONDecoder().decode(DeckConfig.self, from: data)
                do { try loaded.validate() } catch { loadError = "Your saved draft needs attention: \(error.localizedDescription)" }
            } catch { loadError = "Could not read the saved configuration: \(error.localizedDescription)" }
        }
        config = loaded
        errorMessage = loadError
        transport.onOpened = { [weak self] session in self?.serialOpened(session) }
        transport.onLine = { [weak self] session, data in self?.receive(data, session: session) }
        transport.onClosed = { [weak self] session, reason in
            guard let self, self.activeSession == session else { return }
            self.disconnect(status: reason ?? "Device disconnected")
        }
        executor.onError = { [weak self] in self?.errorMessage = $0 }
        terminationObserver = NotificationCenter.default.addObserver(forName: NSApplication.willTerminateNotification, object: nil, queue: .main) { [weak self] _ in self?.saveNow() }
        pollTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in self?.tick() }
        tick()
    }

    deinit {
        pollTimer?.invalidate()
        saveWork?.cancel()
        if let terminationObserver { NotificationCenter.default.removeObserver(terminationObserver) }
        transport.close()
        executor.setSession(nil)
    }

    func dismissError() { errorMessage = nil }

    // MARK: Touch check
    //
    // The device runs this mode on a screen with no event handlers and refuses
    // to start any action sequence while it is active, so nothing here can send
    // a keystroke or run a Mac command. The alignment check measures where
    // touches land; it never derives or stores a correction.
    func startTouchCheck(alignment: Bool) {
        guard connected else { return }
        request("touch_test", fields: ["mode": alignment ? "targets" : "motion"], timeout: 8)
    }

    func refreshTouchCheck() {
        guard connected else { return }
        request("touch_test", fields: ["mode": "status"], timeout: 8)
    }

    func endTouchCheck() {
        guard connected else { return }
        request("touch_test", fields: ["mode": "exit"], timeout: 8)
    }

    func sync() {
        do { try config.validate() } catch { errorMessage = error.localizedDescription; return }
        guard connected, activeSession != nil else { errorMessage = "Connect a deck running NiceDeck before syncing."; return }
        guard !isSyncing else { return }
        do {
            let object = try JSONSerialization.jsonObject(with: config.encoded())
            isSyncing = true
            executor.setSession(nil)
            refreshSyncState()
            request("config", fields: ["config": object], timeout: 20, snapshot: config)
        } catch { isSyncing = false; errorMessage = error.localizedDescription; refreshSyncState() }
    }

    func reconnect() {
        disconnect(status: "Reconnecting…")
        nextConnect = .distantPast
        tick()
    }

    func selectPage(_ index: Int) {
        guard config.pages.indices.contains(index) else { return }
        selectedPageIndex = index
        selectedButtonIndex = 0
        // Local page reordering is a draft until Sync. Avoid navigating to the wrong board page.
        if connected, !isSyncing, let acknowledgedConfig,
           let deviceIndex = acknowledgedConfig.pages.firstIndex(where: { $0.id == config.pages[index].id }) {
            request("page", fields: ["page": deviceIndex])
        }
    }

    func addPage() {
        guard config.pages.count < 8 else { return }
        config.pages.append(DeckPage(name: "Page \(config.pages.count + 1)"))
        selectedPageIndex = config.pages.count - 1
        selectedButtonIndex = 0
    }

    func deleteSelectedPage() {
        guard config.pages.count > 1, config.pages.indices.contains(selectedPageIndex) else { return }
        let removedID = config.pages[selectedPageIndex].id
        var updated = config
        updated.pages.remove(at: selectedPageIndex)
        for page in updated.pages.indices {
            for button in updated.pages[page].buttons.indices {
                updated.pages[page].buttons[button].actions.removeAll { $0.type == .page && $0.value == removedID }
            }
        }
        selectedPageIndex = min(selectedPageIndex, updated.pages.count - 1)
        selectedButtonIndex = 0
        config = updated
    }

    func movePage(by offset: Int) {
        let destination = selectedPageIndex + offset
        guard config.pages.indices.contains(selectedPageIndex), config.pages.indices.contains(destination) else { return }
        config.pages.swapAt(selectedPageIndex, destination)
        selectedPageIndex = destination
    }

    func clearSelectedButton() {
        guard validSelection else { return }
        let previousID = config.pages[selectedPageIndex].buttons[selectedButtonIndex].id
        var button = DeckButton.empty(index: selectedButtonIndex)
        button.id = previousID
        config.pages[selectedPageIndex].buttons[selectedButtonIndex] = button
    }

    func importConfig() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.json]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.message = "Choose a NiceDeck configuration. Import saves a local draft; Sync applies it to your deck."
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            let data = try Data(contentsOf: url)
            guard data.count <= 600000 else { throw DeckValidationError(message: "The selected file is too large.") }
            let imported = try JSONDecoder().decode(DeckConfig.self, from: data)
            try imported.validate()
            selectedPageIndex = 0
            selectedButtonIndex = 0
            config = imported
        } catch { errorMessage = "Import failed: \(error.localizedDescription)" }
    }

    func exportConfig() {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.json]
        panel.nameFieldStringValue = "NiceDeck.json"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do { try config.encoded().write(to: url, options: .atomic) }
        catch { errorMessage = "Export failed: \(error.localizedDescription)" }
    }

    func clearImage() {
        guard validSelection else { return }
        config.pages[selectedPageIndex].buttons[selectedButtonIndex].image = ""
    }

    func chooseImage() {
        guard validSelection else { return }
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.image]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        guard let image = NSImage(contentsOf: url), image.size.width > 0, image.size.height > 0,
              let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 40, pixelsHigh: 40, bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 160, bitsPerPixel: 32),
              let context = NSGraphicsContext(bitmapImageRep: bitmap), let pixels = bitmap.bitmapData else {
            errorMessage = "That image could not be opened. Try a PNG or JPEG."
            return
        }
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = context
        NSColor.black.setFill()
        NSRect(x: 0, y: 0, width: 40, height: 40).fill()
        let scale = min(40 / image.size.width, 40 / image.size.height)
        let size = NSSize(width: image.size.width * scale, height: image.size.height * scale)
        image.draw(in: NSRect(x: (40 - size.width) / 2, y: (40 - size.height) / 2, width: size.width, height: size.height), from: .zero, operation: .sourceOver, fraction: 1)
        NSGraphicsContext.restoreGraphicsState()
        var rgb565 = Data(capacity: 3200)
        for offset in stride(from: 0, to: 6400, by: 4) {
            let pixel = (UInt16(pixels[offset] >> 3) << 11) | (UInt16(pixels[offset + 1] >> 2) << 5) | UInt16(pixels[offset + 2] >> 3)
            rgb565.append(UInt8(pixel >> 8))
            rgb565.append(UInt8(pixel & 255))
        }
        config.pages[selectedPageIndex].buttons[selectedButtonIndex].image = rgb565.base64EncodedString()
    }

    private var validSelection: Bool {
        config.pages.indices.contains(selectedPageIndex) && config.pages[selectedPageIndex].buttons.indices.contains(selectedButtonIndex)
    }

    private func configChanged() {
        refreshSyncState()
        saveWork?.cancel()
        let work = DispatchWorkItem { [weak self] in
            self?.saveNow()
        }
        saveWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.25, execute: work)
    }

    private func saveNow() {
        saveWork?.cancel()
        do {
            try FileManager.default.createDirectory(at: saveURL.deletingLastPathComponent(), withIntermediateDirectories: true)
            try config.encoded().write(to: saveURL, options: .atomic)
        } catch { errorMessage = "Local save failed: \(error.localizedDescription)" }
    }

    private func tick() {
        let now = Date()
        if now.timeIntervalSince(lastScan) > 2 {
            lastScan = now
            let names = (try? FileManager.default.contentsOfDirectory(atPath: "/dev")) ?? []
            ports = names.filter { $0.hasPrefix("cu.usbmodem") }.map { "/dev/\($0)" }.sorted()
            if let activePort, !ports.contains(activePort) { disconnect(status: "Device disconnected") }
        }
        for (id, item) in pending where item.deadline <= now {
            pending.removeValue(forKey: id)
            if item.operation == "page" { errorMessage = "The device did not confirm the page change."; continue }
            if item.operation == "config" { errorMessage = "Sync was not confirmed. Reconnecting to read the device's saved configuration." }
            disconnect(status: item.operation == "hello" ? "Waiting for NiceDeck firmware…" : "Connection timed out — reconnecting…")
            break
        }
        if activeSession == nil, now >= nextConnect {
            let candidates = selectedPort.isEmpty ? ports : ports.filter { $0 == selectedPort }
            if !candidates.isEmpty {
                let path = candidates[candidateIndex % candidates.count]
                candidateIndex += 1
                let session = UUID()
                activeSession = session
                activePort = path
                connectionStatus = "Connecting to \(URL(fileURLWithPath: path).lastPathComponent)…"
                transport.open(path: path, session: session)
            } else { connectionStatus = "Device offline • connect USB" }
        }
        if connected, touchCheck != nil, now.timeIntervalSince(lastTouchCheckPoll) > 0.4,
           !pending.values.contains(where: { $0.operation == "touch_test" }) {
            lastTouchCheckPoll = now
            refreshTouchCheck()
        }
        if connected, !isSyncing, acknowledgedConfig != nil, now.timeIntervalSince(lastHeartbeat) > 3,
           !pending.values.contains(where: { $0.operation == "ping" }) {
            lastHeartbeat = now
            request("ping", timeout: 6)
        }
    }

    private func serialOpened(_ session: UUID) {
        guard session == activeSession else { return }
        request("hello", timeout: 5)
    }

    private func request(_ operation: String, fields: [String: Any] = [:], timeout: TimeInterval = 8, snapshot: DeckConfig? = nil) {
        guard activeSession != nil else { return }
        let id = UUID().uuidString
        pending[id] = Request(operation: operation, deadline: Date().addingTimeInterval(timeout), snapshot: snapshot)
        var packet = fields
        packet["op"] = operation
        packet["id"] = id
        send(packet)
    }

    private func send(_ packet: [String: Any]) {
        guard let session = activeSession, var data = try? JSONSerialization.data(withJSONObject: packet, options: [.sortedKeys, .withoutEscapingSlashes]) else { return }
        data.append(10)
        transport.send(data, session: session)
    }

    private func receive(_ data: Data, session: UUID) {
        guard session == activeSession,
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let event = object["event"] as? String else { return }
        if event == "hello" {
            guard DeckIdentity.supports(device: object["device"] as? String, protocolVersion: integer(object["protocol"])) else {
                disconnect(status: "This port is not a compatible NiceDeck device.")
                return
            }
            // A repeated hello can indicate an in-place device reboot. Read its config again.
            pending.removeAll()
            acknowledgedConfig = nil
            locallyAuthorizedConfig = nil
            isSyncing = false
            touchCheck = nil
            connected = true
            connectionStatus = "Connected • NiceDeck"
            executor.setSession(nil)
            actionResults.removeAll()
            actionsInFlight.removeAll()
            refreshSyncState()
            request("get_config", timeout: 12)
            return
        }
        guard connected else { return }
        switch event {
        case "config":
            guard let id = object["id"] as? String, pending[id]?.operation == "get_config", let configObject = object["config"] else { return }
            do {
                let bytes = try JSONSerialization.data(withJSONObject: configObject)
                let deviceConfig = try JSONDecoder().decode(DeckConfig.self, from: bytes)
                try deviceConfig.validate()
                pending.removeValue(forKey: id)
                acknowledgedConfig = deviceConfig
                // Board-provided commands are never new authorization for a shell/URL/app action.
                // Compare with this local snapshot, preserving later unsynced edits separately.
                locallyAuthorizedConfig = config
                executor.setSession(session)
                refreshSyncState()
            } catch {
                errorMessage = "The deck returned an invalid configuration: \(error.localizedDescription)"
                disconnect(status: "Device configuration could not be verified")
            }
        case "ack":
            guard let id = object["id"] as? String, let request = pending[id],
                  object["op"] as? String == request.operation, let ok = boolean(object["ok"]) else { return }
            pending.removeValue(forKey: id)
            if request.operation == "config" {
                isSyncing = false
                if ok, let snapshot = request.snapshot {
                    acknowledgedConfig = snapshot
                    locallyAuthorizedConfig = snapshot
                    executor.setSession(session)
                } else {
                    // A negative ack is explicit rejection; the preceding acknowledged mapping remains valid.
                    executor.setSession(acknowledgedConfig == nil ? nil : session)
                    errorMessage = object["error"] as? String ?? "The device rejected this configuration."
                }
                refreshSyncState()
            } else if !ok { errorMessage = object["error"] as? String ?? "The device rejected \(request.operation)." }
        case "touch_test":
            if let id = object["id"] as? String { pending.removeValue(forKey: id) }
            // Drop the readout as soon as the device reports it has left the
            // mode, so polling stops and the panel cannot show stale numbers.
            let report = TouchCheckReport(object)
            touchCheck = report.enabled ? report : nil
        case "host_action": handleHostAction(object, session: session)
        case "page":
            guard let page = integer(object["page"]), let acknowledgedConfig,
                  acknowledgedConfig.pages.indices.contains(page),
                  let localIndex = config.pages.firstIndex(where: { $0.id == acknowledgedConfig.pages[page].id }) else { return }
            selectedPageIndex = localIndex
            selectedButtonIndex = 0
        case "error": errorMessage = object["error"] as? String ?? "The device reported an error."
        default: break // A press is informational and never triggers a host command.
        }
    }

    private func handleHostAction(_ packet: [String: Any], session: UUID) {
        guard let id = packet["id"] as? String, !id.isEmpty, id.utf8.count <= 64 else { return }
        if let result = actionResults[id] { sendActionResult(id: id, ok: result.0, error: result.1); return }
        if actionsInFlight.contains(id) { return }
        guard !isSyncing, let page = integer(packet["page"]), let button = integer(packet["button"]), let step = integer(packet["step"]),
              let action = HostActionResolver.resolve(config: acknowledgedConfig, page: page, button: button, step: step),
              action == HostActionResolver.resolve(config: locallyAuthorizedConfig, page: page, button: button, step: step) else {
            sendActionResult(id: id, ok: false, error: "Sync from the Mac to authorize this host action.")
            errorMessage = "A device host action could not be verified against your local configuration. Review your buttons, then Sync."
            return
        }
        actionsInFlight.insert(id)
        executor.enqueue(action, session: session) { [weak self] ok, message in
            guard let self, self.activeSession == session, self.connected else { return }
            self.actionsInFlight.remove(id)
            // Bound replay history per connection. IDs are unique per device macro run.
            if self.actionResults.count >= 512 { self.actionResults.removeAll(keepingCapacity: true) }
            self.actionResults[id] = (ok, message)
            self.sendActionResult(id: id, ok: ok, error: message)
        }
    }

    private func sendActionResult(id: String, ok: Bool, error: String?) {
        var packet: [String: Any] = ["op": "host_action_result", "id": id, "ok": ok]
        if let error { packet["error"] = String(error.prefix(160)) }
        send(packet)
    }

    private func disconnect(status: String) {
        activeSession = nil
        activePort = nil
        connected = false
        isSyncing = false
        acknowledgedConfig = nil
        locallyAuthorizedConfig = nil
        pending.removeAll()
        actionResults.removeAll()
        actionsInFlight.removeAll()
        executor.setSession(nil)
        transport.close()
        connectionStatus = status
        nextConnect = Date().addingTimeInterval(2)
        refreshSyncState()
    }

    private func refreshSyncState() {
        if isSyncing { syncState = "Syncing… waiting for device confirmation" }
        else if !connected { syncState = "Saved locally • device offline" }
        else if acknowledgedConfig == nil { syncState = "Reading device configuration…" }
        else if config == acknowledgedConfig { syncState = "Synced to device" }
        else {
            let hasUntrustedActions = acknowledgedConfig?.pages.enumerated().contains { pageIndex, page in
                page.buttons.enumerated().contains { buttonIndex, button in
                    button.actions.enumerated().contains { step, action in
                        action.type.isHostAction && action != HostActionResolver.resolve(config: locallyAuthorizedConfig, page: pageIndex, button: buttonIndex, step: step)
                    }
                }
            } ?? false
            syncState = hasUntrustedActions ? "Unsynced changes • Sync to enable host actions" : "Unsynced changes • saved locally"
        }
    }

    private func integer(_ value: Any?) -> Int? {
        guard let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID(),
              number.doubleValue.isFinite, number.doubleValue.rounded(.towardZero) == number.doubleValue,
              number.doubleValue >= Double(Int.min), number.doubleValue < Double(Int.max) else { return nil }
        return number.intValue
    }

    private func boolean(_ value: Any?) -> Bool? {
        guard let number = value as? NSNumber, CFGetTypeID(number) == CFBooleanGetTypeID() else { return nil }
        return number.boolValue
    }
}
