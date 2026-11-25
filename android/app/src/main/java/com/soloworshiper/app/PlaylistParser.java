package com.soloworshiper.app;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;

/**
 * Reads a public YouTube playlist's item list (ids, titles, lengths) from the
 * playlist page's own metadata. No media is fetched or decoded; the official
 * embed still plays the videos. Blocking: call from a worker thread.
 */
public class PlaylistParser {

    public static class Entry {
        public final String videoId;
        public final String title;
        public final String duration;

        Entry(String videoId, String title, String duration) {
            this.videoId = videoId;
            this.title = title;
            this.duration = duration;
        }
    }

    public static class Result {
        public boolean ok;
        public String listId = "";
        public String name = "";
        public String error = "";
        public final List<Entry> entries = new ArrayList<>();
    }

    public static Result fetch(String listId) {
        Result r = new Result();
        r.listId = listId;
        String html;
        try {
            html = httpGet("https://www.youtube.com/playlist?list=" + listId + "&hl=en&gl=US");
        } catch (Exception e) {
            r.error = "Playlist listing unavailable (" + e.getMessage() + ").";
            return r;
        }
        if (html == null || html.isEmpty()) {
            r.error = "Playlist listing unavailable (empty response).";
            return r;
        }

        final String meta = "property=\"og:title\" content=\"";
        int at = html.indexOf(meta);
        if (at >= 0) r.name = readJsonString(html, at + meta.length());
        if (r.name.isEmpty()) r.name = "YouTube playlist";

        final String lockup = "\"lockupViewModel\":{";
        int pos = 0;
        while ((pos = html.indexOf(lockup, pos)) >= 0) {
            int next = html.indexOf(lockup, pos + lockup.length());
            int limit = next < 0 ? Math.min(html.length(), pos + 12000) : next;
            String id = null, title = null, duration = "";

            int badge = html.indexOf("\"thumbnailBadgeViewModel\":{", pos);
            if (badge >= 0 && badge < limit) {
                int text = html.indexOf("\"text\":\"", badge);
                if (text >= 0 && text < badge + 400) duration = readJsonString(html, text + 8);
                String act = "\"animationActivationTargetId\":\"";
                int a = html.indexOf(act, badge);
                if (a >= 0 && a < badge + 900) id = html.substring(a + act.length(), a + act.length() + 11);
            }
            if (id == null) {
                String thumb = "i.ytimg.com/vi/";
                int t = html.indexOf(thumb, pos);
                if (t >= 0 && t < limit) id = html.substring(t + thumb.length(), t + thumb.length() + 11);
            }
            String titleKey = "\"lockupMetadataViewModel\":{\"title\":{\"content\":\"";
            int ti = html.indexOf(titleKey, pos);
            if (ti >= 0 && ti < limit) title = readJsonString(html, ti + titleKey.length());

            if (id != null && id.length() == 11) {
                if (title == null || title.isEmpty()) title = id;
                if (r.entries.isEmpty() ||
                        !r.entries.get(r.entries.size() - 1).videoId.equals(id)) {
                    r.entries.add(new Entry(id, title, duration));
                }
            }
            pos = next < 0 ? html.length() : next;
            if (r.entries.size() >= 500) break;
        }

        if (r.entries.isEmpty()) {
            r.error = "Playlist listing unavailable (no items found on the page).";
            return r;
        }
        r.ok = true;
        return r;
    }

    private static String httpGet(String url) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(8000);
        c.setReadTimeout(8000);
        // The mobile listing page is a stripped shell with no item data; the
        // desktop page carries the full item list, so ask for that one.
        c.setRequestProperty("User-Agent",
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " +
                        "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36");
        c.setRequestProperty("Accept-Language", "en-US,en;q=0.9");
        c.setRequestProperty("Cookie", "SOCS=CAI; CONSENT=YES+cb");
        c.connect();
        if (c.getResponseCode() != 200) {
            throw new Exception("HTTP " + c.getResponseCode());
        }
        try (InputStream in = c.getInputStream()) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[16384];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
                if (out.size() > 8 * 1024 * 1024) break;
            }
            return out.toString("UTF-8");
        } finally {
            c.disconnect();
        }
    }

    private static String readJsonString(String s, int from) {
        StringBuilder out = new StringBuilder();
        for (int i = from; i < s.length(); ++i) {
            char c = s.charAt(i);
            if (c == '"') break;
            if (c != '\\') { out.append(c); continue; }
            if (++i >= s.length()) break;
            char e = s.charAt(i);
            switch (e) {
                case 'n': case 't': out.append(' '); break;
                case 'r': break;
                case 'u':
                    if (i + 4 < s.length()) {
                        try {
                            out.append((char) Integer.parseInt(s.substring(i + 1, i + 5), 16));
                        } catch (NumberFormatException ignored) { }
                        i += 4;
                    }
                    break;
                default: out.append(e); break;
            }
        }
        return out.toString().trim();
    }
}
