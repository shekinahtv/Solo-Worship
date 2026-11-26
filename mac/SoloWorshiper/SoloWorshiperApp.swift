import SwiftUI

@main
struct SoloWorshiperApp: App {
    @StateObject private var model = ConsoleModel()

    var body: some Scene {
        WindowGroup("Solo Worshiper") {
            ConsoleView(model: model)
                .frame(minWidth: 940, minHeight: 700)
        }
        .windowResizability(.contentMinSize)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
    }
}
