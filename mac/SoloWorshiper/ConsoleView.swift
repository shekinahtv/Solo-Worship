import AVFoundation
import SwiftUI

enum Theme {
    static let bg = Color(red: 0.043, green: 0.051, blue: 0.063)
    static let panel = Color(red: 0.078, green: 0.094, blue: 0.114)
    static let accent = Color(red: 0.898, green: 0.725, blue: 0.435)
    static let text = Color(red: 0.910, green: 0.925, blue: 0.945)
    static let dim = Color(red: 0.541, green: 0.580, blue: 0.635)
}

enum MicScale {
    case decibels
    case percent

    func text(_ value: Double) -> String {
        switch self {
        case .decibels: return String(format: "%+.1f dB", value)
        case .percent: return String(format: "%.0f%%", value)
        }
    }
}

struct ConsoleView: View {
    @ObservedObject var model: ConsoleModel

    var body: some View {
        VStack(spacing: 8) {
            header
            sourceRow
            Text(model.status)
                .font(.system(size: 11))
                .foregroundColor(Theme.dim)
                .frame(maxWidth: .infinity, alignment: .leading)
            stage
            transportRow
            Text(model.upNext)
                .font(.system(size: 11))
                .foregroundColor(Theme.dim)
                .frame(maxWidth: .infinity, alignment: .leading)
            queue
            if model.showMic { micPanel }
        }
        .padding(10)
        .background(Theme.bg)
        .preferredColorScheme(.dark)
        .onAppear { model.start() }
        .onDisappear { model.shutdown() }
    }

    // MARK: header

    private var header: some View {
        HStack(spacing: 12) {
            Text("SOLO WORSHIPER")
                .font(.system(size: 18, weight: .bold))
                .foregroundColor(Theme.text)
            Spacer()
            devicePicker(title: "IN",
                         devices: model.inputDevices,
                         selection: Binding(get: { model.inputDeviceId },
                                            set: { model.selectInput($0) }))
            devicePicker(title: "OUT",
                         devices: model.outputDevices,
                         selection: Binding(get: { model.outputDeviceId },
                                            set: { model.selectOutput($0) }))
        }
    }

    private func devicePicker(title: String,
                              devices: [AudioDevice],
                              selection: Binding<AudioDeviceID>) -> some View {
        HStack(spacing: 4) {
            Text(title)
                .font(.system(size: 10))
                .foregroundColor(Theme.dim)
            Picker("", selection: selection) {
                ForEach(devices) { device in
                    Text(device.name).tag(device.id)
                }
            }
            .labelsHidden()
            .frame(width: 210)
        }
        .controlSize(.small)
    }

    // MARK: sources

    private var sourceRow: some View {
        HStack(spacing: 8) {
            TextField("YouTube video or playlist link", text: $model.urlText)
                .textFieldStyle(.roundedBorder)
                .onSubmit { model.loadFromField() }
            Button("LOAD") { model.loadFromField() }
            Button("ADD FILES") { model.addFiles() }
            Button(model.micRunning ? "MIC ON" : "MIC") { model.toggleMic() }
        }
    }

    // MARK: stage

    private var stage: some View {
        ZStack {
            Theme.panel
            if let song = model.currentSong, !song.isYouTube {
                LocalPlayerView(player: model.localPlayer)
            } else if model.currentSong != nil {
                YouTubeView(bridge: model.bridge)
            } else {
                Text("Paste a YouTube link, or add local files.")
                    .font(.system(size: 12))
                    .foregroundColor(Theme.dim)
            }
        }
        .frame(maxWidth: .infinity, minHeight: 280)
        .clipShape(RoundedRectangle(cornerRadius: 6))
    }

    private var transportRow: some View {
        HStack(spacing: 8) {
            Button("PLAY / PAUSE") { model.togglePlay() }
            Button("PREV") { model.step(-1) }
            Button("NEXT") { model.step(1) }
            Spacer()
            Text(model.queueName)
                .font(.system(size: 11))
                .foregroundColor(Theme.dim)
                .lineLimit(1)
        }
    }

    // MARK: queue

    private var queue: some View {
        ScrollView {
            LazyVStack(spacing: 0) {
                ForEach(model.items.indices, id: \.self) { index in
                    let song = model.items[index]
                    HStack(spacing: 8) {
                        Text("\(index + 1)")
                            .font(.system(size: 11))
                            .foregroundColor(index == model.current ? Theme.accent : Theme.dim)
                            .frame(width: 28, alignment: .trailing)
                        Text(song.title)
                            .font(.system(size: 13))
                            .foregroundColor(index == model.current ? Theme.accent : Theme.text)
                            .lineLimit(1)
                        Spacer()
                        Text(song.duration)
                            .font(.system(size: 11))
                            .foregroundColor(Theme.dim)
                    }
                    .padding(.horizontal, 6)
                    .padding(.vertical, 5)
                    .background(index == model.current ? Theme.accent.opacity(0.13) : Color.clear)
                    .contentShape(Rectangle())
                    .onTapGesture { model.play(index) }
                }
            }
        }
        .frame(minHeight: 120)
        .background(Theme.panel)
        .clipShape(RoundedRectangle(cornerRadius: 6))
    }

    // MARK: microphone

    private var micPanel: some View {
        VStack(spacing: 6) {
            HStack(spacing: 8) {
                Text("MIC")
                    .font(.system(size: 11))
                    .foregroundColor(Theme.dim)
                    .frame(width: 60, alignment: .leading)
                ProgressView(value: Double(model.micLevel))
                    .frame(maxWidth: .infinity)
                Text(String(format: "%.0f%%", model.micLevel * 100))
                    .font(.system(size: 10))
                    .foregroundColor(Theme.dim)
                    .frame(width: 44, alignment: .trailing)
                Button("CLOSE") { model.showMic = false }
                    .controlSize(.small)
            }
            micSlider("GAIN", $model.gainDb, -12, 30, .decibels)
            micSlider("LOW", $model.lowDb, -12, 12, .decibels)
            micSlider("MID", $model.midDb, -12, 12, .decibels)
            micSlider("HIGH", $model.highDb, -12, 12, .decibels)
            micSlider("REVERB", $model.reverbMix, 0, 1, .percent)
            micSlider("VOLUME", $model.volume, 0, 1, .percent)
            if !model.micError.isEmpty {
                Text(model.micError)
                    .font(.system(size: 10))
                    .foregroundColor(.red)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .padding(8)
        .background(Theme.panel)
        .clipShape(RoundedRectangle(cornerRadius: 6))
    }

    private func micSlider(_ name: String,
                           _ value: Binding<Double>,
                           _ minimum: Double,
                           _ maximum: Double,
                           _ scale: MicScale) -> some View {
        HStack(spacing: 8) {
            Text(name)
                .font(.system(size: 11))
                .foregroundColor(Theme.dim)
                .frame(width: 60, alignment: .leading)
            Slider(value: value, in: minimum...maximum)
            Text(scale.text(value.wrappedValue))
                .font(.system(size: 11))
                .foregroundColor(Theme.text)
                .frame(width: 60, alignment: .trailing)
        }
    }
}
