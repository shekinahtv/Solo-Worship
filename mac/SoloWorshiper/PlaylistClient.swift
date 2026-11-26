import Foundation

struct PlaylistEntry {
    let videoId: String
    let title: String
    let duration: String
}

struct PlaylistResult {
    var ok = false
    var listId = ""
    var name = ""
    var error = ""
    var entries: [PlaylistEntry] = []
}

/// Reads a public YouTube playlist's item list (ids, titles, lengths) from the
/// playlist page's own metadata. No media is fetched or decoded; the official
/// embed still plays the videos.
enum PlaylistClient {

    static func fetch(listId: String) async -> PlaylistResult {
        var result = PlaylistResult()
        result.listId = listId
        guard let url = URL(string: "https://www.youtube.com/playlist?list=\(listId)&hl=en&gl=US") else {
            result.error = "That playlist id is not valid."
            return result
        }
        var request = URLRequest(url: url)
        request.timeoutInterval = 8
        // The mobile listing page is a stripped shell with no item data; the
        // desktop page carries the full item list, so ask for that one.
        request.setValue("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 " +
                         "(KHTML, like Gecko) Version/17.0 Safari/605.1.15", forHTTPHeaderField: "User-Agent")
        request.setValue("en-US,en;q=0.9", forHTTPHeaderField: "Accept-Language")
        request.setValue("SOCS=CAI; CONSENT=YES+cb", forHTTPHeaderField: "Cookie")

        do {
            let (data, response) = try await URLSession.shared.data(for: request)
            let status = (response as? HTTPURLResponse)?.statusCode ?? 0
            guard status == 200 else {
                result.error = "Playlist listing unavailable (HTTP \(status))."
                return result
            }
            let html = String(decoding: data.prefix(8 * 1024 * 1024), as: UTF8.self)
            return await Task.detached(priority: .userInitiated) {
                parse(html: html, listId: listId)
            }.value
        } catch {
            result.error = "Playlist listing unavailable (\(error.localizedDescription))."
            return result
        }
    }

    static func parse(html: String, listId: String) -> PlaylistResult {
        var result = PlaylistResult()
        result.listId = listId
        let page = html as NSString

        let meta = "property=\"og:title\" content=\""
        let metaAt = page.range(of: meta)
        if metaAt.location != NSNotFound {
            result.name = readJSONString(page, metaAt.location + meta.utf16.count)
        }
        if result.name.isEmpty { result.name = "YouTube playlist" }

        let lockup = "\"lockupViewModel\":{"
        var position = 0
        while position < page.length {
            let found = range(page, lockup, position)
            if found.location == NSNotFound { break }
            position = found.location

            let next = range(page, lockup, position + lockup.utf16.count)
            let limit = next.location == NSNotFound ? min(page.length, position + 12_000) : next.location

            var id: String?
            var title: String?
            var duration = ""

            let badge = range(page, "\"thumbnailBadgeViewModel\":{", position)
            if badge.location != NSNotFound && badge.location < limit {
                let text = range(page, "\"text\":\"", badge.location)
                if text.location != NSNotFound && text.location < badge.location + 400 {
                    duration = readJSONString(page, text.location + 8)
                }
                let activation = "\"animationActivationTargetId\":\""
                let mark = range(page, activation, badge.location)
                if mark.location != NSNotFound && mark.location < badge.location + 900 {
                    id = slice(page, mark.location + activation.utf16.count, 11)
                }
            }
            if id == nil {
                let thumb = "i.ytimg.com/vi/"
                let mark = range(page, thumb, position)
                if mark.location != NSNotFound && mark.location < limit {
                    id = slice(page, mark.location + thumb.utf16.count, 11)
                }
            }

            let titleKey = "\"lockupMetadataViewModel\":{\"title\":{\"content\":\""
            let titleAt = range(page, titleKey, position)
            if titleAt.location != NSNotFound && titleAt.location < limit {
                title = readJSONString(page, titleAt.location + titleKey.utf16.count)
            }

            if let id, id.count == 11 {
                let name = (title?.isEmpty == false) ? title! : id
                if result.entries.last?.videoId != id {
                    result.entries.append(PlaylistEntry(videoId: id, title: name, duration: duration))
                }
            }

            if next.location == NSNotFound { break }
            position = next.location
            if result.entries.count >= 500 { break }
        }

        if result.entries.isEmpty {
            result.error = "Playlist listing unavailable (no items found on the page)."
            return result
        }
        result.ok = true
        return result
    }

    private static func range(_ page: NSString, _ needle: String, _ from: Int) -> NSRange {
        guard from >= 0, from < page.length else { return NSRange(location: NSNotFound, length: 0) }
        return page.range(of: needle, options: [], range: NSRange(location: from, length: page.length - from))
    }

    private static func slice(_ page: NSString, _ from: Int, _ length: Int) -> String? {
        guard from >= 0, from + length <= page.length else { return nil }
        return page.substring(with: NSRange(location: from, length: length))
    }

    private static func readJSONString(_ page: NSString, _ from: Int) -> String {
        var out = ""
        var index = from
        while index < page.length {
            let code = page.character(at: index)
            if code == 0x22 { break }                       // closing quote
            if code != 0x5C {                               // not a backslash
                if let scalar = UnicodeScalar(UInt32(code)) { out.unicodeScalars.append(scalar) }
                index += 1
                continue
            }
            index += 1
            if index >= page.length { break }
            let escaped = page.character(at: index)
            switch escaped {
            case 0x6E, 0x74:                                // \n \t
                out.append(" ")
                index += 1
            case 0x72:                                      // \r
                index += 1
            case 0x75:                                      // \uXXXX
                if index + 4 < page.length,
                   let value = UInt32(page.substring(with: NSRange(location: index + 1, length: 4)), radix: 16),
                   let scalar = UnicodeScalar(value) {
                    out.unicodeScalars.append(scalar)
                }
                index += 5
            default:
                if let scalar = UnicodeScalar(UInt32(escaped)) { out.unicodeScalars.append(scalar) }
                index += 1
            }
        }
        return out.trimmingCharacters(in: .whitespacesAndNewlines)
    }
}

/// Same link rules as the console and the Android build.
enum LinkParser {

    static func playlistId(_ url: String) -> String? {
        guard let mark = url.range(of: "list=") else { return nil }
        let id = url[mark.upperBound...].prefix { $0.isLetter || $0.isNumber || $0 == "-" || $0 == "_" }
        return (12...64).contains(id.count) ? String(id) : nil
    }

    static func videoId(_ url: String) -> String? {
        if let mark = url.range(of: "v="), let id = eleven(url, mark.upperBound) { return id }
        for marker in ["youtu.be/", "/embed/", "/shorts/", "/live/"] {
            if let mark = url.range(of: marker), let id = eleven(url, mark.upperBound) { return id }
        }
        if url.count == 11, eleven(url, url.startIndex) != nil { return url }
        return nil
    }

    private static func eleven(_ text: String, _ start: String.Index) -> String? {
        let candidate = text[start...].prefix(11)
        guard candidate.count == 11,
              candidate.allSatisfy({ $0.isLetter || $0.isNumber || $0 == "-" || $0 == "_" }) else {
            return nil
        }
        return String(candidate)
    }
}
