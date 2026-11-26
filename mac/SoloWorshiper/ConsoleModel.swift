import AppKit
import AVFoundation
import CoreAudio
import Foundation
import SwiftUI
import UniformTypeIdentifiers

/// The whole console state: the shared queue, the two players, the microphone
/// monitor and the device selection. One object, so every control stays in step.
@MainActor
final class ConsoleModel: ObservableObject {

    @Published private(set) var items: [Song] = []
    @Published private(set) var current = -1
    @Published private(set) var queueName = ""
    @Published private(set) var status = "Paste a YouTube link or add local files."
    @Published private(set) var upNext = "NO PLAYLIST"
    @Published var urlText = ""
    @Published private(set) var micRunning = false
    @Published private(set) var micLevel: Double = 0
    @Published private(set) var micError = ""
    @Published var showMic = false

    @Published var gainDb: Double = 6 { didSet { pushMic() } }
    @Published var lowDb: Double = 0 { didSet { pushMic() } }
    @Published var midDb: Double = 0 { didSet { pushMic() } }
    @Published var highDb: Double = 0 { didSet { pushMic() } }
    @Published var reverbMix: Double = 0.2 { didSet { pushMic() } }
    @Published var volume: Double = 0.8 { didSet { pushMic() } }

    @Published private(set) var inputDevices: [AudioDevice] = []
    @Published private(set) var outputDevices: [AudioDevice] = []
    @Published private(set) var inputDeviceId: AudioDeviceID = 0
    @Published private(set) var outputDeviceId: AudioDeviceID = 0

    let bridge = PlayerBridge()
    let mic = MicEngine()
    let localPlayer = AVPlayer()

    private var timer: Timer?
    private var endObserver: NSObjectProtocol?
    private var lastEndedVideoId = ""

    var currentSong: Song? {
        (current >= 0 && current < items.count) ? items[current] : nil
    }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------
    func start() {
        refreshDevices()
        localPlayer.actionAtItemEnd = .pause

        endObserver = NotificationCenter.default.addObserver(
            forName: .AVPlayerItemDidPlayToEndTime, object: nil, queue: .main
        ) { [weak self] note in
            Task { @MainActor in
                guard let self, let item = note.object as? AVPlayerItem,
                      item === self.localPlayer.currentItem else { return }
                self.autoAdvance()
            }
        }

        timer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.tick() }
        }
        pushMic()
    }

    func shutdown() {
        timer?.invalidate()
        timer = nil
        if let endObserver {
            NotificationCenter.default.removeObserver(endObserver)
            self.endObserver = nil
        }
        mic.stop()
        micRunning = false
        localPlayer.pause()
    }

    // -----------------------------------------------------------------------
    // Devices
    // -----------------------------------------------------------------------
    func refreshDevices() {
        let all = AudioDevices.all()
        inputDevices = all.filter { $0.isInput }
        outputDevices = all.filter { $0.isOutput }
        if inputDeviceId == 0 {
            inputDeviceId = AudioDevices.defaultDevice(input: true) ?? inputDevices.first?.id ?? 0
        }
        if outputDeviceId == 0 {
            outputDeviceId = AudioDevices.defaultDevice(input: false) ?? outputDevices.first?.id ?? 0
        }
    }

    func selectInput(_ id: AudioDeviceID) {
        inputDeviceId = id
        restartMicIfNeeded()
    }

    func selectOutput(_ id: AudioDeviceID) {
        outputDeviceId = id
        restartMicIfNeeded()
    }

    private func restartMicIfNeeded() {
        guard micRunning else { return }
        mic.stop()
        micRunning = false
        toggleMic()
    }

    // -----------------------------------------------------------------------
    // Sources
    // -----------------------------------------------------------------------
    func loadFromField() {
        let text = urlText.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else {
            status = "Paste a YouTube link first."
            return
        }
        if let listId = LinkParser.playlistId(text) {
            status = "Reading the playlist from YouTube..."
            Task {
                let result = await PlaylistClient.fetch(listId: listId)
                apply(result)
            }
        } else if let videoId = LinkParser.videoId(text) {
            items = [.youtube(videoId, title: videoId)]
            current = 0
            queueName = "YouTube"
            lastEndedVideoId = ""
            playCurrent()
        } else {
            status = "That link has no video or playlist id."
        }
    }

    private func apply(_ result: PlaylistResult) {
        guard result.ok else {
            status = result.error
            return
        }
        items = result.entries.map { Song.youtube($0.videoId, title: $0.title, duration: $0.duration) }
        current = 0
        queueName = result.name
        lastEndedVideoId = ""
        status = "Playlist: \(items.count) songs"
        playCurrent()
    }

    func addFiles() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = true
        panel.canChooseDirectories = false
        panel.canChooseFiles = true
        panel.allowedContentTypes = [.audio, .movie]
        panel.prompt = "Add"
        panel.message = "Add audio or video files to the queue"
        guard panel.runModal() == .OK, !panel.urls.isEmpty else { return }

        let wasEmpty = items.isEmpty
        for url in panel.urls {
            items.append(.file(url, title: url.lastPathComponent))
        }
        if queueName.isEmpty { queueName = "Local files" }
        status = "Added \(panel.urls.count) file(s) to the queue."
        if wasEmpty {
            current = 0
            playCurrent()
        } else {
            refreshUpNext()
        }
    }

    // -----------------------------------------------------------------------
    // Transport
    // -----------------------------------------------------------------------
    func play(_ index: Int) {
        guard items.indices.contains(index) else { return }
        current = index
        playCurrent()
    }

    func playCurrent() {
        guard let song = currentSong else { return }
        if song.isYouTube {
            localPlayer.pause()
            bridge.load(videoId: song.sourceId)
            status = "YouTube: \(song.title)"
        } else if let url = song.fileURL {
            bridge.pause()
            localPlayer.replaceCurrentItem(with: AVPlayerItem(url: url))
            localPlayer.play()
            status = "Playing: \(song.title)"
        }
        refreshUpNext()
    }

    func togglePlay() {
        guard let song = currentSong else {
            status = "Nothing loaded yet."
            return
        }
        if song.isYouTube {
            bridge.toggle()
        } else if localPlayer.timeControlStatus == .playing {
            localPlayer.pause()
        } else {
            localPlayer.play()
        }
    }

    func step(_ delta: Int) {
        guard !items.isEmpty else {
            status = "No playlist loaded."
            return
        }
        let target = current + delta
        if target < 0 {
            status = "Start of the playlist."
            return
        }
        if target >= items.count {
            status = "End of the playlist."
            return
        }
        play(target)
    }

    private func autoAdvance() {
        if current + 1 < items.count {
            play(current + 1)
        } else {
            status = "End of the playlist."
        }
    }

    // -----------------------------------------------------------------------
    // Microphone
    // -----------------------------------------------------------------------
    func toggleMic() {
        if micRunning {
            mic.stop()
            micRunning = false
            status = "Microphone monitoring stopped."
            return
        }
        pushMic()
        if let failure = mic.start(input: inputDeviceId, output: outputDeviceId) {
            micError = failure
            micRunning = false
            showMic = true
            status = "Microphone could not start: \(failure)"
            return
        }
        micError = ""
        micRunning = true
        showMic = true
        status = "Microphone monitoring on - use headphones."
    }

    private func pushMic() {
        mic.set(MicParams(gainDb: Float(gainDb),
                          lowDb: Float(lowDb),
                          midDb: Float(midDb),
                          highDb: Float(highDb),
                          reverbMix: Float(reverbMix),
                          volume: Float(volume)))
    }

    // -----------------------------------------------------------------------
    // Polling
    // -----------------------------------------------------------------------
    private func tick() {
        if micRunning {
            micLevel = Double(min(1, mic.peak * 1.4))
        } else if micLevel != 0 {
            micLevel = 0
        }
        guard let song = currentSong, song.isYouTube else { return }
        bridge.fetchState { [weak self] state in
            guard let self, let state else { return }
            Task { @MainActor in self.handle(state) }
        }
    }

    private func handle(_ state: WebState) {
        guard let song = currentSong else { return }
        if !state.title.isEmpty, song.title == song.sourceId,
           let index = items.firstIndex(where: { $0.id == song.id }) {
            // The title was a placeholder: adopt the real one.
            items[index].title = state.title
            refreshUpNext()
        }
        if state.state == 0, lastEndedVideoId != song.sourceId {
            lastEndedVideoId = song.sourceId
            autoAdvance()
        }
    }

    private func refreshUpNext() {
        let next = (current + 1 < items.count) ? items[current + 1] : nil
        if let next {
            upNext = "UP NEXT  \(next.title)"
        } else {
            upNext = items.isEmpty ? "NO PLAYLIST" : "END OF PLAYLIST"
        }
    }
}
