import SwiftUI
import AppKit

@main
struct NiceDeckApp: App {
    @StateObject private var store = DeckStore()

    var body: some Scene {
        WindowGroup {
            DeckEditorView(store: store)
                .frame(minWidth: 1080, minHeight: 760)
                .preferredColorScheme(.dark)
                .tint(DeckStyle.accent)
                .onAppear { NSApp.activate(ignoringOtherApps: true) }
        }
        .windowStyle(.hiddenTitleBar)
        .defaultSize(width: 1240, height: 870)
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("Add Page") { store.addPage() }
                    .keyboardShortcut("n")
                    .disabled(store.config.pages.count >= 8)
                Divider()
                Button("Import Configuration…") { store.importConfig() }
                    .keyboardShortcut("o")
                Button("Export Configuration…") { store.exportConfig() }
                    .keyboardShortcut("e", modifiers: [.command, .shift])
            }
            CommandGroup(after: .saveItem) {
                Button("Sync to Deck") { store.sync() }
                    .keyboardShortcut("s")
                    .disabled(!store.connected || store.isSyncing)
            }
        }
    }
}
