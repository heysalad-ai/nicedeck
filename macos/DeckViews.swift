import SwiftUI
import AppKit

enum DeckStyle {
    static let background = Color(hex: "#111215")
    static let sidebar = Color(hex: "#16171B")
    static let panel = Color(hex: "#1C1D22")
    static let raised = Color(hex: "#25262D")
    static let line = Color.white.opacity(0.075)
    static let accent = Color(hex: "#FA7958")
    static let muted = Color(hex: "#999AA6")
    static let palette = ["#FA7958", "#576BEF", "#925BE0", "#C6548B", "#2DA88A", "#2E94C5", "#DBAA43", "#596271"]
    static let icons: [(String, String)] = [
        ("key", "command"), ("copy", "doc.on.doc"), ("paste", "doc.on.clipboard"),
        ("mic", "mic.fill"), ("video", "video.fill"), ("play", "play.fill"),
        ("volume", "speaker.wave.2.fill"), ("music", "music.note"), ("globe", "globe"),
        ("app", "app.fill"), ("terminal", "terminal.fill"), ("bolt", "bolt.fill"),
        ("folder", "folder.fill"), ("star", "star.fill"), ("heart", "heart.fill"), ("gear", "gearshape.fill")
    ]
    static func symbol(_ icon: String) -> String { icons.first { $0.0 == icon }?.1 ?? "command" }
}

extension Color {
    init(hex: String) {
        let value = UInt32(hex.trimmingCharacters(in: CharacterSet(charactersIn: "#")), radix: 16) ?? 0x576BEF
        self.init(red: Double((value >> 16) & 255) / 255, green: Double((value >> 8) & 255) / 255, blue: Double(value & 255) / 255)
    }
    var deckHex: String {
        guard let color = NSColor(self).usingColorSpace(.deviceRGB) else { return "#576BEF" }
        return String(format: "#%02X%02X%02X", Int((color.redComponent * 255).rounded()), Int((color.greenComponent * 255).rounded()), Int((color.blueComponent * 255).rounded()))
    }
}

extension ActionType {
    var symbol: String {
        switch self {
        case .none: return "circle.dashed"
        case .hotkey: return "command"
        case .text: return "text.cursor"
        case .media: return "playpause.fill"
        case .url: return "globe"
        case .app: return "app"
        case .shell: return "terminal"
        case .shortcut: return "square.stack.3d.up"
        case .page: return "rectangle.on.rectangle"
        case .delay: return "clock"
        }
    }
    var requiresCompanion: Bool { [.url, .app, .shell, .shortcut].contains(self) }
    var placeholder: String {
        switch self {
        case .hotkey: return "cmd+shift+4"
        case .text: return "Text to type…"
        case .url: return "https://example.com"
        case .app: return "Safari or /Applications/Safari.app"
        case .shell: return "Your shell command…"
        case .shortcut: return "Name from the Shortcuts app"
        default: return "Value"
        }
    }
}

struct DeckEditorView: View {
    @ObservedObject var store: DeckStore
    @State private var showsSettings = false
    @State private var showsHelp = false

    var body: some View {
        VStack(spacing: 0) {
            header
            Rectangle().fill(DeckStyle.line).frame(height: 1)
            HStack(spacing: 0) {
                pageSidebar.frame(width: 210)
                Rectangle().fill(DeckStyle.line).frame(width: 1)
                previewWorkspace.frame(maxWidth: .infinity, maxHeight: .infinity)
                Rectangle().fill(DeckStyle.line).frame(width: 1)
                ButtonInspector(store: store).frame(width: 355)
            }
            footer
        }
        .background(DeckStyle.background)
        .sheet(isPresented: $showsSettings) { DeckSettingsView(store: store) }
        .sheet(isPresented: $showsHelp) { DeckHelpView() }
        .alert("NiceDeck", isPresented: Binding(get: { store.errorMessage != nil }, set: { if !$0 { store.dismissError() } })) {
            Button("OK") { store.dismissError() }
        } message: { Text(store.errorMessage ?? "") }
    }

    private var header: some View {
        HStack(spacing: 12) {
            Image(systemName: "square.grid.2x2.fill")
                .font(.system(size: 19, weight: .semibold)).foregroundStyle(DeckStyle.accent)
                .frame(width: 38, height: 38).background(DeckStyle.accent.opacity(0.12), in: RoundedRectangle(cornerRadius: 11))
            VStack(alignment: .leading, spacing: 2) {
                Text("NiceDeck").font(.system(size: 16, weight: .semibold))
                Text("Your shortcuts. At your fingertips.").font(.system(size: 11)).foregroundStyle(DeckStyle.muted)
            }
            Spacer()
            HStack(spacing: 7) {
                Circle().fill(store.connected ? Color.green : DeckStyle.muted).frame(width: 6, height: 6)
                Text(store.connected ? "Deck connected" : "Editing offline").font(.system(size: 12, weight: .medium)).foregroundStyle(DeckStyle.muted)
            }.padding(.trailing, 10)
            Button { showsSettings = true } label: { Image(systemName: "slider.horizontal.3").frame(width: 30, height: 30) }
                .buttonStyle(.plain).help("Device settings and USB connection")
            Button { store.sync() } label: {
                HStack(spacing: 8) {
                    if store.isSyncing { ProgressView().controlSize(.small).scaleEffect(0.8) }
                    else { Image(systemName: "arrow.down.to.line.compact") }
                    Text(store.isSyncing ? "Syncing…" : "Sync to deck").fontWeight(.semibold)
                }.font(.system(size: 12)).padding(.horizontal, 16).frame(height: 36)
            }
            .buttonStyle(AccentButtonStyle())
            .disabled(!store.connected || store.isSyncing)
            .help("Save this configuration to your physical deck (⌘S). Changes take effect after the deck confirms the sync.")
        }.padding(.leading, 25).padding(.trailing, 22).padding(.top, 29).padding(.bottom, 18)
    }

    private var pageSidebar: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("YOUR PAGES").font(.system(size: 10, weight: .bold)).tracking(1.5).foregroundStyle(DeckStyle.muted)
                Spacer()
                Text("\(store.config.pages.count)/8").font(.system(size: 10, design: .monospaced)).foregroundStyle(DeckStyle.muted)
            }.padding(.horizontal, 20).padding(.top, 25).padding(.bottom, 17)
            ScrollView {
                VStack(spacing: 6) {
                    ForEach(Array(store.config.pages.enumerated()), id: \.element.id) { index, page in
                        Button { store.selectPage(index) } label: {
                            HStack(spacing: 11) {
                                Image(systemName: index == store.selectedPageIndex ? "rectangle.grid.2x2.fill" : "rectangle.grid.2x2")
                                    .font(.system(size: 15)).foregroundStyle(index == store.selectedPageIndex ? DeckStyle.accent : DeckStyle.muted)
                                VStack(alignment: .leading, spacing: 4) {
                                    Text(page.name.isEmpty ? "Untitled page" : page.name).font(.system(size: 12, weight: .medium)).lineLimit(1)
                                    Text("6 buttons").font(.system(size: 10)).foregroundStyle(DeckStyle.muted)
                                }
                                Spacer(minLength: 1)
                                Text(String(index + 1)).font(.system(size: 10, design: .monospaced)).foregroundStyle(DeckStyle.muted)
                            }.padding(12).frame(maxWidth: .infinity, alignment: .leading)
                                .background(index == store.selectedPageIndex ? DeckStyle.raised : .clear, in: RoundedRectangle(cornerRadius: 10))
                        }.buttonStyle(.plain)
                            .contextMenu {
                                Button("Move Up") { store.selectPage(index); store.movePage(by: -1) }.disabled(index == 0)
                                Button("Move Down") { store.selectPage(index); store.movePage(by: 1) }.disabled(index == store.config.pages.count - 1)
                                Divider()
                                Button("Delete Page", role: .destructive) { store.selectPage(index); store.deleteSelectedPage() }.disabled(store.config.pages.count == 1)
                            }
                    }
                    Button { store.addPage() } label: {
                        Label("Add page", systemImage: "plus").font(.system(size: 12)).foregroundStyle(DeckStyle.muted)
                            .frame(maxWidth: .infinity, alignment: .leading).padding(13)
                    }.buttonStyle(.plain).disabled(store.config.pages.count >= 8)
                }.padding(.horizontal, 10)
            }
            Spacer(minLength: 15)
            if store.config.pages.indices.contains(store.selectedPageIndex) {
                VStack(alignment: .leading, spacing: 9) {
                    Text("PAGE NAME").font(.system(size: 9, weight: .bold)).tracking(1).foregroundStyle(DeckStyle.muted)
                    TextField("Page name", text: pageName).textFieldStyle(.plain).font(.system(size: 12))
                        .padding(10).background(DeckStyle.background, in: RoundedRectangle(cornerRadius: 7))
                    HStack(spacing: 6) {
                        Button { store.movePage(by: -1) } label: { Image(systemName: "arrow.up").frame(width: 27, height: 24) }.disabled(store.selectedPageIndex == 0)
                        Button { store.movePage(by: 1) } label: { Image(systemName: "arrow.down").frame(width: 27, height: 24) }.disabled(store.selectedPageIndex == store.config.pages.count - 1)
                        Spacer()
                        Button(role: .destructive) { store.deleteSelectedPage() } label: { Image(systemName: "trash").frame(width: 27, height: 24) }.disabled(store.config.pages.count <= 1)
                    }.buttonStyle(.borderless).foregroundStyle(DeckStyle.muted)
                }.padding(18)
            }
            Rectangle().fill(DeckStyle.line).frame(height: 1)
            VStack(alignment: .leading, spacing: 16) {
                Button { store.importConfig() } label: { Label("Import configuration", systemImage: "square.and.arrow.down") }
                Button { store.exportConfig() } label: { Label("Export configuration", systemImage: "square.and.arrow.up") }
                Button { showsHelp = true } label: { Label("Getting started", systemImage: "questionmark.circle") }
            }.font(.system(size: 11)).foregroundStyle(DeckStyle.muted).buttonStyle(.plain).padding(20)
        }.background(DeckStyle.sidebar)
    }

    private var pageName: Binding<String> {
        Binding(get: { store.config.pages.indices.contains(store.selectedPageIndex) ? store.config.pages[store.selectedPageIndex].name : "" }, set: { value in
            guard store.config.pages.indices.contains(store.selectedPageIndex) else { return }
            store.config.pages[store.selectedPageIndex].name = printableLabel(value)
        })
    }

    private var previewWorkspace: some View {
        ScrollView {
            VStack(spacing: 0) {
                HStack {
                    VStack(alignment: .leading, spacing: 7) {
                        Text("Make it yours.").font(.system(size: 28, weight: .semibold, design: .rounded))
                        Text("Select a button to shape your workflow.").font(.system(size: 12)).foregroundStyle(DeckStyle.muted)
                    }
                    Spacer()
                }.padding(.horizontal, 30).padding(.top, 30)
                HStack(spacing: 7) {
                    Image(systemName: "hand.tap").foregroundStyle(DeckStyle.accent)
                    Text("LIVE LAYOUT").tracking(1.4)
                    Text("·  ESP32-S3  /  2″ TOUCH").foregroundStyle(DeckStyle.muted)
                }.font(.system(size: 9, weight: .semibold)).padding(.top, 35).padding(.bottom, 18)
                DevicePreview(store: store)
                HStack(spacing: 7) {
                    Image(systemName: "cursorarrow.click")
                    Text("Click to edit. Actions run from your physical deck.")
                }.font(.system(size: 10)).foregroundStyle(DeckStyle.muted).padding(.top, 22)
                HStack(spacing: 6) {
                    Image(systemName: "externaldrive").foregroundStyle(DeckStyle.accent)
                    Text("Saved automatically on this Mac")
                }.font(.system(size: 10)).foregroundStyle(DeckStyle.muted).padding(.top, 11).padding(.bottom, 28)
            }.frame(maxWidth: .infinity)
        }
    }

    private var footer: some View {
        HStack(spacing: 8) {
            Image(systemName: store.connected ? "cable.connector" : "cable.connector.slash")
            Text(store.connectionStatus).lineLimit(1)
            Spacer()
            Text(store.syncState).lineLimit(1)
        }.font(.system(size: 10)).foregroundStyle(DeckStyle.muted).padding(.horizontal, 20).frame(height: 32)
            .background(DeckStyle.sidebar).overlay(alignment: .top) { Rectangle().fill(DeckStyle.line).frame(height: 1) }
    }
}

struct DevicePreview: View {
    @ObservedObject var store: DeckStore
    private var page: DeckPage? { store.config.pages.indices.contains(store.selectedPageIndex) ? store.config.pages[store.selectedPageIndex] : nil }
    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text(page?.name.isEmpty == false ? page!.name : "Untitled page").font(.system(size: 12, weight: .semibold)).lineLimit(1)
                Spacer()
                Text("\(store.selectedPageIndex + 1) / \(store.config.pages.count)").font(.system(size: 10, design: .monospaced)).foregroundStyle(DeckStyle.muted)
            }.padding(.horizontal, 8).frame(height: 36)
            LazyVGrid(columns: [GridItem(.flexible(), spacing: 9), GridItem(.flexible(), spacing: 9)], spacing: 9) {
                if let page = page {
                    ForEach(Array(page.buttons.enumerated()), id: \.element.id) { index, button in
                        Button { store.selectedButtonIndex = index } label: {
                            VStack(spacing: 10) {
                                if let image = decodeDeckImage(button.image) {
                                    Image(nsImage: image).interpolation(.none).resizable().scaledToFit().frame(width: 42, height: 42)
                                } else {
                                    Image(systemName: DeckStyle.symbol(button.icon)).font(.system(size: 28, weight: .medium)).frame(height: 42)
                                }
                                Text(button.label.isEmpty ? "Button \(index + 1)" : button.label)
                                    .font(.system(size: 11, weight: .semibold)).lineLimit(2).multilineTextAlignment(.center)
                                    .frame(height: 28).padding(.horizontal, 3)
                            }
                            .foregroundStyle(.white)
                            .frame(maxWidth: .infinity).frame(height: 118)
                            .background(LinearGradient(colors: [Color(hex: button.color), Color(hex: button.color).opacity(0.67)], startPoint: .topLeading, endPoint: .bottomTrailing), in: RoundedRectangle(cornerRadius: 12))
                            .overlay { RoundedRectangle(cornerRadius: 12).strokeBorder(store.selectedButtonIndex == index ? Color.white : Color.white.opacity(0.08), lineWidth: store.selectedButtonIndex == index ? 2 : 1) }
                            .overlay(alignment: .topTrailing) {
                                if store.selectedButtonIndex == index {
                                    Image(systemName: "checkmark.circle.fill").font(.system(size: 13)).symbolRenderingMode(.palette).foregroundStyle(Color(hex: button.color), .white).padding(7)
                                }
                            }
                        }.buttonStyle(.plain).help("Edit button \(index + 1): \(button.label)")
                    }
                }
            }
            HStack {
                Button { store.selectPage(max(0, store.selectedPageIndex - 1)) } label: { Image(systemName: "chevron.left").frame(width: 40, height: 35) }.disabled(store.selectedPageIndex == 0)
                Spacer()
                HStack(spacing: 5) {
                    ForEach(store.config.pages.indices, id: \.self) { index in
                        Circle().fill(index == store.selectedPageIndex ? DeckStyle.accent : DeckStyle.muted.opacity(0.35)).frame(width: 4, height: 4)
                    }
                }
                Spacer()
                Button { store.selectPage(min(store.config.pages.count - 1, store.selectedPageIndex + 1)) } label: { Image(systemName: "chevron.right").frame(width: 40, height: 35) }.disabled(store.selectedPageIndex == store.config.pages.count - 1)
            }.font(.system(size: 11, weight: .semibold)).foregroundStyle(DeckStyle.muted).buttonStyle(.plain).padding(.top, 4)
        }
        .padding(12).frame(width: 300)
        .background(Color(hex: "#08090C"), in: RoundedRectangle(cornerRadius: 27))
        .padding(9)
        .background(LinearGradient(colors: [Color(hex: "#35363D"), Color(hex: "#1B1C21")], startPoint: .topLeading, endPoint: .bottomTrailing), in: RoundedRectangle(cornerRadius: 35))
        .overlay { RoundedRectangle(cornerRadius: 35).strokeBorder(.white.opacity(0.09), lineWidth: 1) }
        .shadow(color: .black.opacity(0.45), radius: 28, y: 18)
    }
}

struct ButtonInspector: View {
    @ObservedObject var store: DeckStore
    private var valid: Bool { store.config.pages.indices.contains(store.selectedPageIndex) && store.config.pages[store.selectedPageIndex].buttons.indices.contains(store.selectedButtonIndex) }
    private var selected: DeckButton? { valid ? store.config.pages[store.selectedPageIndex].buttons[store.selectedButtonIndex] : nil }
    private func field<T>(_ keyPath: WritableKeyPath<DeckButton, T>, fallback: T) -> Binding<T> {
        Binding(get: { selected?[keyPath: keyPath] ?? fallback }, set: { value in
            guard valid else { return }
            store.config.pages[store.selectedPageIndex].buttons[store.selectedButtonIndex][keyPath: keyPath] = value
        })
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 22) {
                HStack {
                    VStack(alignment: .leading, spacing: 5) {
                        Text("BUTTON \(store.selectedButtonIndex + 1)").font(.system(size: 9, weight: .bold)).tracking(1.4).foregroundStyle(DeckStyle.accent)
                        Text("Button editor").font(.system(size: 19, weight: .semibold))
                    }
                    Spacer()
                    Menu {
                        Menu("Duplicate to…") {
                            ForEach(0..<6, id: \.self) { index in
                                if index != store.selectedButtonIndex {
                                    Button("Button \(index + 1)") { duplicate(to: index) }
                                }
                            }
                        }
                        Divider()
                        Button("Reset Button", role: .destructive) { store.clearSelectedButton() }
                    } label: { Image(systemName: "ellipsis").frame(width: 24, height: 24) }
                    .menuStyle(.borderlessButton).fixedSize().help("Duplicate or reset this button")
                }
                if let selected = selected {
                    appearance(selected)
                    separator
                    actions(selected)
                    separator
                    positionControls
                }
            }.padding(23)
        }.background(DeckStyle.sidebar)
    }

    private var separator: some View { Rectangle().fill(DeckStyle.line).frame(height: 1) }

    private func appearance(_ selected: DeckButton) -> some View {
        VStack(alignment: .leading, spacing: 16) {
            InspectorHeading("Appearance", symbol: "paintpalette")
            VStack(alignment: .leading, spacing: 7) {
                fieldLabel("Label")
                TextField("Button label", text: Binding(get: { self.selected?.label ?? "" }, set: { field(\.label, fallback: "").wrappedValue = printableLabel($0) }))
                    .textFieldStyle(.plain).padding(10).background(DeckStyle.background, in: RoundedRectangle(cornerRadius: 7))
                    .font(.system(size: 12)).help("Up to 24 printable ASCII characters")
            }
            VStack(alignment: .leading, spacing: 9) {
                HStack {
                    fieldLabel("Button colour")
                    Spacer()
                    ColorPicker("Custom colour", selection: Binding(get: { Color(hex: self.selected?.color ?? "#576BEF") }, set: { field(\.color, fallback: "#576BEF").wrappedValue = $0.deckHex }), supportsOpacity: false)
                        .labelsHidden().frame(width: 26).help("Choose any colour")
                }
                HStack(spacing: 10) {
                    ForEach(DeckStyle.palette, id: \.self) { hex in
                        Button { field(\.color, fallback: hex).wrappedValue = hex } label: {
                            Circle().fill(Color(hex: hex)).frame(width: 26, height: 26)
                                .overlay { if selected.color.uppercased() == hex { Image(systemName: "checkmark").font(.system(size: 10, weight: .bold)).foregroundStyle(.white) } }
                        }.buttonStyle(.plain).help(hex)
                    }
                }
            }
            VStack(alignment: .leading, spacing: 9) {
                HStack {
                    fieldLabel("Icon")
                    Spacer()
                    if !selected.image.isEmpty {
                        Button("Use built-in icon") { store.clearImage() }.font(.system(size: 10)).buttonStyle(.plain).foregroundStyle(DeckStyle.accent)
                    }
                }
                LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 6), count: 8), spacing: 6) {
                    ForEach(DeckStyle.icons, id: \.0) { icon in
                        Button { field(\.icon, fallback: "key").wrappedValue = icon.0; store.clearImage() } label: {
                            Image(systemName: icon.1).font(.system(size: 14)).frame(maxWidth: .infinity).frame(height: 33)
                                .foregroundStyle(selected.icon == icon.0 && selected.image.isEmpty ? DeckStyle.accent : DeckStyle.muted)
                                .background(selected.icon == icon.0 && selected.image.isEmpty ? DeckStyle.accent.opacity(0.12) : DeckStyle.raised, in: RoundedRectangle(cornerRadius: 6))
                        }.buttonStyle(.plain).help(icon.0.capitalized)
                    }
                }
                Button { store.chooseImage() } label: {
                    HStack(spacing: 8) {
                        if let image = decodeDeckImage(selected.image) {
                            Image(nsImage: image).resizable().interpolation(.none).frame(width: 23, height: 23).clipShape(RoundedRectangle(cornerRadius: 4))
                        } else { Image(systemName: "photo.badge.plus") }
                        Text(selected.image.isEmpty ? "Upload your own icon…" : "Replace custom icon…")
                        Spacer()
                        Text("40 × 40").font(.system(size: 9, design: .monospaced)).foregroundStyle(DeckStyle.muted)
                    }.font(.system(size: 11)).padding(10).background(DeckStyle.raised, in: RoundedRectangle(cornerRadius: 7))
                }.buttonStyle(.plain).help("Choose an image. It will be resized to the deck’s 40 × 40 pixel icon format.")
            }
        }
    }

    private func actions(_ selected: DeckButton) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                InspectorHeading("Actions", symbol: "bolt")
                Spacer()
                Text("\(selected.actions.count) / 8").font(.system(size: 10, design: .monospaced)).foregroundStyle(DeckStyle.muted)
            }
            Text("Run these steps in order with one tap.").font(.system(size: 10)).foregroundStyle(DeckStyle.muted)
            if selected.actions.isEmpty {
                VStack(spacing: 7) {
                    Image(systemName: "bolt.badge.plus").font(.system(size: 22)).foregroundStyle(DeckStyle.muted)
                    Text("Make this button do something.").font(.system(size: 11)).foregroundStyle(DeckStyle.muted)
                }.frame(maxWidth: .infinity).padding(.vertical, 18).background(DeckStyle.background, in: RoundedRectangle(cornerRadius: 9))
            }
            ForEach(selected.actions.indices, id: \.self) { index in
                ActionStepEditor(action: actionBinding(index), pages: store.config.pages, index: index, total: selected.actions.count, remove: { removeAction(index) }, move: { offset in moveAction(index, offset: offset) })
            }
            Button {
                guard valid, (self.selected?.actions.count ?? 8) < 8 else { return }
                store.config.pages[store.selectedPageIndex].buttons[store.selectedButtonIndex].actions.append(DeckAction(type: .none, value: "", delayMs: 0))
            } label: {
                Label("Add action", systemImage: "plus").font(.system(size: 11, weight: .medium)).foregroundStyle(DeckStyle.accent)
                    .frame(maxWidth: .infinity).frame(height: 34)
                    .background(DeckStyle.accent.opacity(0.07), in: RoundedRectangle(cornerRadius: 7))
                    .overlay { RoundedRectangle(cornerRadius: 7).strokeBorder(DeckStyle.accent.opacity(0.17), style: StrokeStyle(lineWidth: 1, dash: [4, 3])) }
            }.buttonStyle(.plain).disabled(selected.actions.count >= 8)
            if selected.actions.contains(where: { $0.type.requiresCompanion }) {
                Label("Keep this app open for Mac actions.", systemImage: "desktopcomputer")
                    .font(.system(size: 10)).foregroundStyle(DeckStyle.muted)
            }
        }
    }

    private var positionControls: some View {
        VStack(alignment: .leading, spacing: 10) {
            InspectorHeading("Position", symbol: "arrow.up.and.down.and.arrow.left.and.right")
            HStack(spacing: 8) {
                moveButton("arrow.left", offset: -1, enabled: store.selectedButtonIndex % 2 == 1)
                moveButton("arrow.right", offset: 1, enabled: store.selectedButtonIndex % 2 == 0)
                moveButton("arrow.up", offset: -2, enabled: store.selectedButtonIndex >= 2)
                moveButton("arrow.down", offset: 2, enabled: store.selectedButtonIndex < 4)
                Spacer()
                Text("Swap buttons").font(.system(size: 10)).foregroundStyle(DeckStyle.muted)
            }
        }
    }
    private func moveButton(_ symbol: String, offset: Int, enabled: Bool) -> some View {
        Button {
            guard valid else { return }
            let target = store.selectedButtonIndex + offset
            guard (0..<6).contains(target) else { return }
            store.config.pages[store.selectedPageIndex].buttons.swapAt(store.selectedButtonIndex, target)
            store.selectedButtonIndex = target
        } label: { Image(systemName: symbol).font(.system(size: 11)).frame(width: 30, height: 28).background(DeckStyle.raised, in: RoundedRectangle(cornerRadius: 6)) }
            .buttonStyle(.plain).disabled(!enabled).opacity(enabled ? 1 : 0.3)
    }
    private func duplicate(to target: Int) {
        guard valid, var copy = selected, store.config.pages[store.selectedPageIndex].buttons.indices.contains(target) else { return }
        copy.id = store.config.pages[store.selectedPageIndex].buttons[target].id
        store.config.pages[store.selectedPageIndex].buttons[target] = copy
        store.selectedButtonIndex = target
    }
    private func actionBinding(_ index: Int) -> Binding<DeckAction> {
        Binding(get: { guard let selected = selected, selected.actions.indices.contains(index) else { return DeckAction(type: .none, value: "", delayMs: 0) }; return selected.actions[index] }, set: { value in
            guard valid, selected?.actions.indices.contains(index) == true else { return }
            store.config.pages[store.selectedPageIndex].buttons[store.selectedButtonIndex].actions[index] = value
        })
    }
    private func removeAction(_ index: Int) {
        guard valid, selected?.actions.indices.contains(index) == true else { return }
        store.config.pages[store.selectedPageIndex].buttons[store.selectedButtonIndex].actions.remove(at: index)
    }
    private func moveAction(_ index: Int, offset: Int) {
        guard valid, let selected = selected, selected.actions.indices.contains(index + offset) else { return }
        store.config.pages[store.selectedPageIndex].buttons[store.selectedButtonIndex].actions.swapAt(index, index + offset)
    }
    private func fieldLabel(_ label: String) -> some View { Text(label).font(.system(size: 11, weight: .medium)).foregroundStyle(DeckStyle.muted) }
}

struct ActionStepEditor: View {
    @Binding var action: DeckAction
    let pages: [DeckPage]
    let index: Int
    let total: Int
    let remove: () -> Void
    let move: (Int) -> Void
    private let mediaOptions = [("play_pause", "Play / pause"), ("next", "Next track"), ("previous", "Previous track"), ("mute", "Mute / unmute"), ("volume_up", "Volume up"), ("volume_down", "Volume down"), ("stop", "Stop")]

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 7) {
                Text(String(format: "%02d", index + 1)).font(.system(size: 9, weight: .medium, design: .monospaced)).foregroundStyle(DeckStyle.muted)
                Picker("Action type", selection: Binding(get: { action.type }, set: { type in
                    action.type = type
                    switch type {
                    case .media: action.value = "play_pause"
                    case .page: action.value = pages.first?.id ?? ""
                    default: action.value = ""
                    }
                })) {
                    ForEach(ActionType.allCases, id: \.rawValue) { type in Text(type.title).tag(type) }
                }.labelsHidden().controlSize(.small).frame(maxWidth: .infinity)
                Menu {
                    Button("Move Step Up") { move(-1) }.disabled(index == 0)
                    Button("Move Step Down") { move(1) }.disabled(index >= total - 1)
                    Divider()
                    Button("Remove Step", role: .destructive, action: remove)
                } label: { Image(systemName: "ellipsis").frame(width: 16, height: 16) }.menuStyle(.borderlessButton).fixedSize()
            }
            valueEditor
            if action.type != .none {
                HStack(spacing: 6) {
                    Image(systemName: "clock").font(.system(size: 10)).foregroundStyle(DeckStyle.muted)
                    Text(action.type == .delay ? "Wait for" : "Delay before").font(.system(size: 10)).foregroundStyle(DeckStyle.muted)
                    Spacer()
                    TextField("0", value: $action.delayMs, format: .number).textFieldStyle(.plain).multilineTextAlignment(.trailing)
                        .frame(width: 48).font(.system(size: 10, design: .monospaced)).padding(5)
                        .background(DeckStyle.background, in: RoundedRectangle(cornerRadius: 4))
                        .onChange(of: action.delayMs) { value in if value < 0 || value > 5000 { action.delayMs = min(5000, max(0, value)) } }
                    Text("ms").font(.system(size: 9)).foregroundStyle(DeckStyle.muted)
                }
            }
        }.padding(11).background(DeckStyle.raised, in: RoundedRectangle(cornerRadius: 9))
    }

    @ViewBuilder private var valueEditor: some View {
        switch action.type {
        case .none:
            Text("Choose an action from the menu above.").font(.system(size: 10)).foregroundStyle(DeckStyle.muted)
        case .media:
            Picker("Media control", selection: $action.value) {
                ForEach(mediaOptions, id: \.0) { item in Text(item.1).tag(item.0) }
            }.labelsHidden().controlSize(.small)
        case .page:
            Picker("Destination page", selection: $action.value) {
                ForEach(pages, id: \.id) { page in Text(page.name).tag(page.id) }
            }.labelsHidden().controlSize(.small)
            Text("Switching pages ends this action sequence.").font(.system(size: 9)).foregroundStyle(DeckStyle.muted)
        case .text, .shell:
            TextEditor(text: $action.value).font(.system(size: 11, design: action.type == .shell ? .monospaced : .default))
                .scrollContentBackground(.hidden).padding(6).frame(height: 78)
                .background(DeckStyle.background, in: RoundedRectangle(cornerRadius: 6))
                .overlay(alignment: .topLeading) { if action.value.isEmpty { Text(action.type.placeholder).font(.system(size: 10)).foregroundStyle(DeckStyle.muted).padding(11).allowsHitTesting(false) } }
            if action.type == .shell { Text("Runs your command on this Mac when tapped.").font(.system(size: 9)).foregroundStyle(DeckStyle.muted) }
            else { Text("US keyboard mapping · ASCII, newline and tab").font(.system(size: 9)).foregroundStyle(DeckStyle.muted) }
        case .delay:
            EmptyView()
        default:
            TextField(action.type.placeholder, text: $action.value).textFieldStyle(.plain).font(.system(size: 11))
                .padding(9).background(DeckStyle.background, in: RoundedRectangle(cornerRadius: 6))
            if action.type == .hotkey { Text("cmd, ctrl, alt, shift + a key · e.g. cmd+shift+4").font(.system(size: 9)).foregroundStyle(DeckStyle.muted) }
        }
    }
}

struct DeckSettingsView: View {
    @ObservedObject var store: DeckStore
    @Environment(\.dismiss) private var dismiss
    var body: some View {
        VStack(alignment: .leading, spacing: 24) {
            HStack { Text("Deck settings").font(.system(size: 22, weight: .semibold)); Spacer(); Button("Done") { dismiss() }.keyboardShortcut(.defaultAction) }
            VStack(alignment: .leading, spacing: 12) {
                InspectorHeading("Display", symbol: "sun.max")
                HStack {
                    Text("Brightness").font(.system(size: 12))
                    Slider(value: Binding(get: { Double(store.config.brightness) }, set: { store.config.brightness = Int($0.rounded()) }), in: 5...100, step: 1)
                    Text("\(store.config.brightness)%").font(.system(size: 11, design: .monospaced)).frame(width: 36)
                }
                Picker("Sleep after", selection: $store.config.sleepSeconds) {
                    Text("Never").tag(0); Text("30 seconds").tag(30); Text("1 minute").tag(60); Text("2 minutes").tag(120)
                    Text("5 minutes").tag(300); Text("10 minutes").tag(600); Text("30 minutes").tag(1800); Text("1 hour").tag(3600)
                    if ![0, 30, 60, 120, 300, 600, 1800, 3600].contains(store.config.sleepSeconds) { Text("\(store.config.sleepSeconds) seconds").tag(store.config.sleepSeconds) }
                }.font(.system(size: 12))
                Text("The first tap wakes the display. Sync to apply display settings.").font(.system(size: 10)).foregroundStyle(DeckStyle.muted)
            }
            Divider()
            VStack(alignment: .leading, spacing: 12) {
                InspectorHeading("USB connection", symbol: "cable.connector")
                Text(store.connectionStatus).font(.system(size: 11)).foregroundStyle(DeckStyle.muted)
                HStack {
                    Picker("Port", selection: $store.selectedPort) {
                        Text("Automatic").tag("")
                        ForEach(store.ports, id: \.self) { port in Text(port.replacingOccurrences(of: "/dev/", with: "")).tag(port) }
                    }.font(.system(size: 11))
                    Button("Reconnect") { store.reconnect() }.controlSize(.small)
                }
                Text("Connect the deck with a USB data cable. It appears as a keyboard and a serial device; Wi-Fi is not required.").font(.system(size: 11)).foregroundStyle(DeckStyle.muted).fixedSize(horizontal: false, vertical: true)
            }
            Divider()
            TouchCheckSection(store: store)
        }.padding(28).frame(width: 475).background(DeckStyle.sidebar).preferredColorScheme(.dark)
    }
}

/// The device's no-action touch check. While it runs, the deck shows a
/// diagnostic screen that carries no event handlers and refuses to start any
/// action sequence, so no shortcut, keystroke or Mac command can be triggered
/// from it. The alignment part reports where taps landed relative to where the
/// targets were drawn; it is a measurement and never stores a correction.
struct TouchCheckSection: View {
    @ObservedObject var store: DeckStore

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            InspectorHeading("Touch check", symbol: "hand.tap")
            if let report = store.touchCheck {
                HStack(spacing: 8) {
                    Text("SHORTCUTS DISABLED").font(.system(size: 10, weight: .semibold)).foregroundStyle(.orange)
                    Spacer()
                    Button("Finish and re-enable") { store.endTouchCheck() }.controlSize(.small).keyboardShortcut(.cancelAction)
                }
                Text(prompt(for: report)).font(.system(size: 11)).fixedSize(horizontal: false, vertical: true)
                readout(report)
                if report.alignmentMode && report.points.contains(where: { $0.measured }) {
                    VStack(alignment: .leading, spacing: 2) {
                        ForEach(Array(report.points.enumerated()), id: \.offset) { index, point in
                            Text(describe(index: index, point: point))
                                .font(.system(size: 10, design: .monospaced))
                                .foregroundStyle(point.measured ? DeckStyle.muted : DeckStyle.muted.opacity(0.5))
                        }
                        Text("Measurement only — no calibration is applied or saved.")
                            .font(.system(size: 10)).foregroundStyle(DeckStyle.muted)
                    }
                }
            } else {
                Text("Runs on the device with shortcuts switched off, so you can check touch without anything firing.")
                    .font(.system(size: 11)).foregroundStyle(DeckStyle.muted).fixedSize(horizontal: false, vertical: true)
                HStack {
                    Button("Movement check") { store.startTouchCheck(alignment: false) }.controlSize(.small)
                    Button("Alignment check") { store.startTouchCheck(alignment: true) }.controlSize(.small)
                    Spacer()
                }.disabled(!store.connected)
                if !store.connected {
                    Text("Connect the deck to run a touch check.").font(.system(size: 10)).foregroundStyle(DeckStyle.muted)
                }
            }
        }
    }

    private func prompt(for report: TouchCheckReport) -> String {
        if !report.alignmentMode {
            return "Move the case and the cable without touching the glass. Any contacts counted below are unwanted touch reports."
        }
        if report.complete { return "Alignment check complete." }
        return "Tap each crosshair on the device in turn (\(report.hits) of \(report.points.count) done)."
    }

    private func readout(_ report: TouchCheckReport) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text("contacts \(report.contacts)   samples \(report.downSamples)   read errors \(report.readErrors)   BOOT \(report.bootPresses)")
            Text("raw \(report.rawX), \(report.rawY)   mapped \(report.x), \(report.y)   \(report.down ? "DOWN" : "up")")
            Text("sample interval \(report.minIntervalMs)–\(report.maxIntervalMs) ms   interrupts \(report.irqs)   last reject \(report.lastReject)")
        }
        .font(.system(size: 10, design: .monospaced))
        .foregroundStyle(DeckStyle.muted)
    }

    private func describe(index: Int, point: TouchCheckReport.Target) -> String {
        guard point.measured else { return "target \(index + 1): not reached" }
        return String(format: "target %d: expected (%d,%d)  measured (%d,%d)  error (%+d,%+d) px",
                      index + 1, point.expectedX, point.expectedY, point.x, point.y, point.dx, point.dy)
    }
}

struct DeckHelpView: View {
    @Environment(\.dismiss) private var dismiss
    var body: some View {
        VStack(alignment: .leading, spacing: 23) {
            HStack { Text("A little deck. Your whole workflow.").font(.system(size: 21, weight: .semibold)); Spacer() }
            helpRow("1", "Design your buttons", "Select a page and a button. Add a label, colour and icon, then choose up to eight actions to run with one tap.")
            helpRow("2", "Sync to your deck", "Changes save automatically on this Mac. Connect the device and click Sync to deck to store the whole configuration on the board.")
            helpRow("3", "Tap and get things done", "Tap a button to run its actions. It runs when you lift your finger on the same button; sliding off cancels. Use the footer arrows or press BOOT to change pages. The first tap after sleep only wakes the display.")
            Divider()
            Label("Works directly over USB", systemImage: "keyboard").font(.system(size: 12, weight: .semibold))
            Text("Keyboard shortcuts, typed text, media keys and page navigation work without this app running. Keyboard actions use US keyboard mapping; typed text supports ASCII, newline and tab.").font(.system(size: 11)).foregroundStyle(DeckStyle.muted).fixedSize(horizontal: false, vertical: true)
            Label("Keep the app open for Mac actions", systemImage: "desktopcomputer").font(.system(size: 12, weight: .semibold))
            Text("Opening websites, applications, Apple Shortcuts and running your shell commands requires this app to be open and connected. Selecting a button in this editor only edits it.").font(.system(size: 11)).foregroundStyle(DeckStyle.muted).fixedSize(horizontal: false, vertical: true)
            HStack { Spacer(); Button("Let’s make it yours") { dismiss() }.keyboardShortcut(.defaultAction).buttonStyle(.borderedProminent) }
        }.padding(30).frame(width: 490).background(DeckStyle.sidebar).preferredColorScheme(.dark)
    }
    private func helpRow(_ number: String, _ title: String, _ body: String) -> some View {
        HStack(alignment: .top, spacing: 13) {
            Text(number).font(.system(size: 12, weight: .semibold, design: .monospaced)).foregroundStyle(DeckStyle.accent).frame(width: 27, height: 27).background(DeckStyle.accent.opacity(0.12), in: Circle())
            VStack(alignment: .leading, spacing: 5) { Text(title).font(.system(size: 13, weight: .semibold)); Text(body).font(.system(size: 11)).foregroundStyle(DeckStyle.muted).fixedSize(horizontal: false, vertical: true) }
        }
    }
}

struct InspectorHeading: View {
    let title: String
    let symbol: String
    init(_ title: String, symbol: String) { self.title = title; self.symbol = symbol }
    var body: some View { Label(title, systemImage: symbol).font(.system(size: 12, weight: .semibold)) }
}

struct AccentButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var isEnabled
    func makeBody(configuration: Configuration) -> some View {
        configuration.label.foregroundStyle(Color(hex: "#21130F"))
            .background(DeckStyle.accent.opacity(isEnabled ? (configuration.isPressed ? 0.8 : 1) : 0.3), in: RoundedRectangle(cornerRadius: 9))
            .opacity(isEnabled ? 1 : 0.65)
    }
}

private func printableLabel(_ value: String) -> String {
    String(value.unicodeScalars.filter { $0.value >= 32 && $0.value <= 126 }.prefix(24).map(Character.init))
}

private func decodeDeckImage(_ encoded: String) -> NSImage? {
    guard !encoded.isEmpty, let data = Data(base64Encoded: encoded), data.count == 3200,
          let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 40, pixelsHigh: 40, bitsPerSample: 8, samplesPerPixel: 3, hasAlpha: false, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 120, bitsPerPixel: 24),
          let output = bitmap.bitmapData else { return nil }
    let bytes = [UInt8](data)
    for index in 0..<1600 {
        let pixel = UInt16(bytes[index * 2]) << 8 | UInt16(bytes[index * 2 + 1])
        output[index * 3] = UInt8(((pixel >> 11) & 31) * 255 / 31)
        output[index * 3 + 1] = UInt8(((pixel >> 5) & 63) * 255 / 63)
        output[index * 3 + 2] = UInt8((pixel & 31) * 255 / 31)
    }
    let image = NSImage(size: NSSize(width: 40, height: 40))
    image.addRepresentation(bitmap)
    return image
}
