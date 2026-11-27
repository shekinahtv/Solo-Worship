import AppKit
import AVFoundation
import QuartzCore
import SwiftUI
import WebKit

/// What the embed page reports about the current video.
struct WebState {
    let position: Double
    let duration: Double
    let playing: Bool
    /// YT.PlayerState: -1 unstarted, 0 ended, 1 playing, 2 paused, 3 buffering, 5 cued
    let state: Int
    let title: String

    init?(raw: String) {
        let parts = raw.components(separatedBy: "|")
        guard parts.count >= 4,
              let position = Double(parts[0]),
              let duration = Double(parts[1]),
              let state = Int(parts[3].trimmingCharacters(in: .whitespaces)) else { return nil }
        self.position = position
        self.duration = duration
        self.playing = parts[2] == "1"
        self.state = state
        self.title = parts.count >= 5 ? parts[4] : ""
    }
}

/// Owns the WKWebView that hosts the official YouTube embed and calls into it.
/// The page is loaded from a bundled file with an https base URL so the embed
/// sees a normal origin; nothing is downloaded, ripped or extracted.
final class PlayerBridge: NSObject, WKNavigationDelegate {

    private weak var webView: WKWebView?
    private var pendingVideoId: String?
    private var ready = false

    func makeWebView() -> WKWebView {
        if let webView { return webView }
        let configuration = WKWebViewConfiguration()
        configuration.mediaTypesRequiringUserActionForPlayback = []
        configuration.allowsAirPlayForMediaPlayback = true
        let view = WKWebView(frame: .zero, configuration: configuration)
        view.navigationDelegate = self
        view.underPageBackgroundColor = .black
        webView = view
        loadPlayerPage()
        return view
    }

    private func loadPlayerPage() {
        guard let url = Bundle.main.url(forResource: "player", withExtension: "html"),
              let html = try? String(contentsOf: url, encoding: .utf8) else { return }
        ready = false
        webView?.loadHTMLString(html, baseURL: URL(string: "https://solo.local/"))
    }

    func load(videoId: String) {
        if webView == nil { _ = makeWebView() }
        if ready {
            call("window.loadVideo('\(videoId)')")
        } else {
            pendingVideoId = videoId
        }
    }

    func play() { call("window.play && window.play()") }
    func pause() { call("window.pause && window.pause()") }
    func toggle() { call("window.toggle && window.toggle()") }

    func fetchState(_ completion: @escaping (WebState?) -> Void) {
        guard ready, let webView else {
            completion(nil)
            return
        }
        webView.evaluateJavaScript("(window.state && window.state()) || ''") { value, _ in
            guard let raw = value as? String, !raw.isEmpty else {
                completion(nil)
                return
            }
            completion(WebState(raw: raw))
        }
    }

    private func call(_ script: String) {
        DispatchQueue.main.async { [weak self] in
            self?.webView?.evaluateJavaScript(script, completionHandler: nil)
        }
    }

    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        ready = true
        if let id = pendingVideoId {
            pendingVideoId = nil
            call("window.loadVideo('\(id)')")
        }
    }
}

struct YouTubeView: NSViewRepresentable {
    let bridge: PlayerBridge

    func makeNSView(context: Context) -> WKWebView {
        bridge.makeWebView()
    }

    func updateNSView(_ nsView: WKWebView, context: Context) {}
}

/// AVPlayerLayer host for local audio and video files.
final class PlayerNSView: NSView {
    private let playerLayer = AVPlayerLayer()

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
        let host = CALayer()
        host.backgroundColor = NSColor.black.cgColor
        layer = host
        playerLayer.videoGravity = .resizeAspect
        host.addSublayer(playerLayer)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not used")
    }

    var player: AVPlayer? {
        get { playerLayer.player }
        set { playerLayer.player = newValue }
    }

    override func layout() {
        super.layout()
        playerLayer.frame = bounds
    }
}

struct LocalPlayerView: NSViewRepresentable {
    let player: AVPlayer

    func makeNSView(context: Context) -> PlayerNSView {
        let view = PlayerNSView()
        view.player = player
        return view
    }

    func updateNSView(_ nsView: PlayerNSView, context: Context) {
        if nsView.player !== player { nsView.player = player }
    }
}
