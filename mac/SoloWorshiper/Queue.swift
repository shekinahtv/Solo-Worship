import Foundation

/// One entry in the shared, ordered queue. YouTube entries are played by the
/// official embed; file entries are played by the app's own AVPlayer. Same model
/// as the Windows console and the Android build, so previous / next / jump behave
/// identically on every platform.
struct Song: Identifiable {
    let isYouTube: Bool
    /// YouTube video id, or the file URL string.
    let sourceId: String
    var title: String
    var duration: String

    var id: String { (isYouTube ? "yt:" : "file:") + sourceId }

    var fileURL: URL? { isYouTube ? nil : URL(string: sourceId) }

    static func youtube(_ videoId: String, title: String, duration: String = "") -> Song {
        Song(isYouTube: true, sourceId: videoId, title: title, duration: duration)
    }

    static func file(_ url: URL, title: String) -> Song {
        Song(isYouTube: false, sourceId: url.absoluteString, title: title, duration: "")
    }
}
