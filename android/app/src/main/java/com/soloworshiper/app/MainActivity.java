package com.soloworshiper.app;

import android.Manifest;
import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.media3.common.MediaItem;
import androidx.media3.common.Player;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.ui.PlayerView;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * SOLO WORSHIPER for phones and tablets.
 *
 * Local media files play through the app's own player with a shared queue;
 * YouTube links (video or playlist) play through the official embed in a WebView,
 * and the playlist's items are read from its public listing so the same queue can
 * drive previous / next / jump. The microphone is monitored through a real DSP
 * chain (see MicEngine).
 *
 * Honest platform limit: Android does not allow an application to capture another
 * app's or the system mixer's audio, so the phone's playback is heard from the
 * system and is not blended into the microphone path.
 */
public class MainActivity extends Activity {

    private static final int REQ_FILES = 41;
    private static final int REQ_MIC = 42;

    private static final int BG = 0xFF0B0D10;
    private static final int PANEL = 0xFF14181D;
    private static final int ACCENT = 0xFFE5B96F;
    private static final int TEXT = 0xFFE8ECF1;
    private static final int DIM = 0xFF8A94A2;

    private final QueueModel queue = new QueueModel();
    private final List<String> youtubeQueueIds = new ArrayList<>();

    private ExoPlayer player;
    private PlayerView playerView;
    private WebView webView;
    private EditText urlField;
    private TextView statusView;
    private TextView upNextView;
    private TextView micMeterLabel;
    private ProgressBar micMeter;
    private ListView queueList;
    private QueueAdapter adapter;
    private LinearLayout micPanel;

    private MicEngine mic;
    private final Handler handler = new Handler(Looper.getMainLooper());

    private String pendingVideoId = "";
    private String lastEndedVideoId = "";
    private boolean webReady = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Android 15+ draws every app edge to edge; keep the console off the bars.
        if (Build.VERSION.SDK_INT >= 21) {
            getWindow().setStatusBarColor(Color.TRANSPARENT);
            getWindow().setNavigationBarColor(BG);
        }
        mic = new MicEngine();
        buildUi();
        createPlayer();
        createWebView();
        handler.postDelayed(tick, 400);
    }

    @Override
    protected void onDestroy() {
        handler.removeCallbacksAndMessages(null);
        if (mic != null) mic.stop();
        if (player != null) player.release();
        if (webView != null) {
            webView.loadUrl("about:blank");
            webView.destroy();
        }
        super.onDestroy();
    }

    // -----------------------------------------------------------------------
    // Layout
    // -----------------------------------------------------------------------
    private void buildUi() {
        final int padH = dp(10), padV = dp(8);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(BG);
        root.setPadding(padH, padV, padH, padV);
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            v.setPadding(padH + insets.getSystemWindowInsetLeft(),
                    padV + insets.getSystemWindowInsetTop(),
                    padH + insets.getSystemWindowInsetRight(),
                    padV + insets.getSystemWindowInsetBottom());
            return insets;
        });

        TextView title = new TextView(this);
        title.setText("SOLO WORSHIPER");
        title.setTextColor(TEXT);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setTextSize(18);
        root.addView(title);

        LinearLayout urlRow = new LinearLayout(this);
        urlRow.setOrientation(LinearLayout.HORIZONTAL);
        urlRow.setGravity(Gravity.CENTER_VERTICAL);

        urlField = new EditText(this);
        urlField.setHint("YouTube video or playlist link");
        urlField.setHintTextColor(DIM);
        urlField.setTextColor(TEXT);
        urlField.setTextSize(13);
        urlField.setSingleLine(true);
        urlField.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        LinearLayout.LayoutParams fieldLp =
                new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        urlRow.addView(urlField, fieldLp);

        Button load = button("LOAD", ACCENT, Color.BLACK);
        load.setOnClickListener(v -> loadFromField());
        urlRow.addView(load);
        root.addView(urlRow);

        LinearLayout tools = new LinearLayout(this);
        tools.setOrientation(LinearLayout.HORIZONTAL);
        Button add = button("ADD FILES", PANEL, TEXT);
        add.setOnClickListener(v -> pickFiles());
        tools.addView(add, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        Button micBtn = button("MIC", PANEL, TEXT);
        micBtn.setOnClickListener(v -> toggleMic());
        tools.addView(micBtn, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        root.addView(tools);

        statusView = new TextView(this);
        statusView.setTextColor(DIM);
        statusView.setTextSize(11);
        statusView.setText("Paste a YouTube link or add local files.");
        root.addView(statusView);

        FrameLayout media = new FrameLayout(this);
        LinearLayout.LayoutParams mediaLp =
                new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 3f);
        root.addView(media, mediaLp);

        playerView = new PlayerView(this);
        playerView.setBackgroundColor(Color.BLACK);
        playerView.setUseController(false);
        media.addView(playerView, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        webView = new WebView(this);
        webView.setBackgroundColor(Color.BLACK);
        webView.setVisibility(View.GONE);
        media.addView(webView, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout transport = new LinearLayout(this);
        transport.setOrientation(LinearLayout.HORIZONTAL);
        transport.setGravity(Gravity.CENTER_VERTICAL);
        Button play = button("PLAY", PANEL, TEXT);
        play.setOnClickListener(v -> togglePlay());
        Button prev = button("PREV", PANEL, TEXT);
        prev.setOnClickListener(v -> step(-1));
        Button next = button("NEXT", PANEL, TEXT);
        next.setOnClickListener(v -> step(1));
        final LinearLayout.LayoutParams trLp = new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        transport.addView(play, trLp);
        transport.addView(prev, new LinearLayout.LayoutParams(trLp));
        transport.addView(next, new LinearLayout.LayoutParams(trLp));
        root.addView(transport);

        upNextView = new TextView(this);
        upNextView.setTextColor(DIM);
        upNextView.setTextSize(11);
        upNextView.setSingleLine(true);
        upNextView.setText("NO PLAYLIST");
        root.addView(upNextView);

        queueList = new ListView(this);
        queueList.setBackgroundColor(BG);
        adapter = new QueueAdapter();
        queueList.setAdapter(adapter);
        queueList.setOnItemClickListener((parent, view, position, id) -> playIndex(position));
        LinearLayout.LayoutParams listLp =
                new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 2f);
        root.addView(queueList, listLp);

        micPanel = buildMicPanel();
        micPanel.setVisibility(View.GONE);
        root.addView(micPanel, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        setContentView(root);
    }

    private LinearLayout buildMicPanel() {
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setBackgroundColor(PANEL);

        LinearLayout meterRow = new LinearLayout(this);
        meterRow.setOrientation(LinearLayout.HORIZONTAL);
        meterRow.setGravity(Gravity.CENTER_VERTICAL);
        TextView label = new TextView(this);
        label.setText("MIC");
        label.setTextColor(DIM);
        label.setTextSize(11);
        meterRow.addView(label);
        micMeter = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        micMeter.setMax(100);
        LinearLayout.LayoutParams meterLp =
                new LinearLayout.LayoutParams(0, dp(10), 1f);
        meterRow.addView(micMeter, meterLp);
        micMeterLabel = new TextView(this);
        micMeterLabel.setTextColor(DIM);
        micMeterLabel.setTextSize(10);
        micMeterLabel.setText("  --");
        meterRow.addView(micMeterLabel);
        panel.addView(meterRow);

        ScrollView scroll = new ScrollView(this);
        LinearLayout rows = new LinearLayout(this);
        rows.setOrientation(LinearLayout.VERTICAL);
        rows.addView(micRow("GAIN", -12, 30, (int) mic.gainDb, v -> mic.gainDb = v));
        rows.addView(micRow("LOW", -12, 12, 12, v -> mic.lowDb = v));
        rows.addView(micRow("MID", -12, 12, 12, v -> mic.midDb = v));
        rows.addView(micRow("HIGH", -12, 12, 12, v -> mic.highDb = v));
        rows.addView(micRow("REVERB", 0, 100, 20, v -> mic.reverbMix = v / 100f));
        rows.addView(micRow("VOLUME", 0, 100, 80, v -> mic.volume = v / 100f));
        scroll.addView(rows);
        panel.addView(scroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(150)));
        return panel;
    }

    private interface FloatSetter { void set(float value); }

    private View micRow(String name, final float min, final float max, int initial,
                        final FloatSetter setter) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);

        TextView label = new TextView(this);
        label.setText(name);
        label.setTextColor(DIM);
        label.setTextSize(11);
        label.setWidth(dp(64));
        row.addView(label);

        final TextView value = new TextView(this);
        value.setTextColor(TEXT);
        value.setTextSize(11);
        value.setWidth(dp(46));
        value.setText(format(name, initial, min, max));

        SeekBar bar = new SeekBar(this);
        bar.setMax((int) (max - min));
        bar.setProgress((int) (initial - min));
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                final float v = min + progress;
                setter.set(v);
                value.setText(format(name, v, min, max));
            }
            @Override public void onStartTrackingTouch(SeekBar sb) { }
            @Override public void onStopTrackingTouch(SeekBar sb) { }
        });
        LinearLayout.LayoutParams barLp = new LinearLayout.LayoutParams(0, dp(34), 1f);
        row.addView(bar, barLp);
        row.addView(value);
        return row;
    }

    private static String format(String name, float v, float min, float max) {
        if (name.equals("REVERB") || name.equals("VOLUME")) {
            return String.format(Locale.US, "%.0f%%", v);
        }
        return String.format(Locale.US, "%+.1f dB", v);
    }

    private Button button(String text, int bg, int fg) {
        Button b = new Button(this);
        b.setText(text);
        b.setTextSize(12);
        b.setTextColor(fg);
        b.setBackgroundColor(bg);
        b.setAllCaps(false);
        b.setMinWidth(dp(64));
        return b;
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density);
    }

    // -----------------------------------------------------------------------
    // Media
    // -----------------------------------------------------------------------
    private void createPlayer() {
        player = new ExoPlayer.Builder(this).build();
        playerView.setPlayer(player);
        player.addListener(new Player.Listener() {
            @Override
            public void onPlaybackStateChanged(int state) {
                if (state == Player.STATE_ENDED) advance(true);
            }
        });
        playerView.setVisibility(View.GONE);
    }

    private void createWebView() {
        WebSettings s = webView.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setAllowFileAccess(false);
        s.setMixedContentMode(WebSettings.MIXED_CONTENT_COMPATIBILITY_MODE);
        webView.setWebViewClient(new WebViewClient() {
            @Override public void onPageFinished(WebView view, String url) {
                webReady = true;
                if (!pendingVideoId.isEmpty()) {
                    final String id = pendingVideoId;
                    pendingVideoId = "";
                    view.loadUrl("javascript:window.loadVideo('" + id + "')");
                }
            }
        });
    }

    private void loadWebPlayer() {
        try {
            java.io.InputStream in = getAssets().open("player.html");
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            in.close();
            webReady = false;
            webView.loadDataWithBaseURL("https://solo.local/", out.toString("UTF-8"),
                    "text/html", "UTF-8", null);
        } catch (Exception e) {
            status("Player page missing: " + e.getMessage());
        }
    }

    // -----------------------------------------------------------------------
    // Queue actions
    // -----------------------------------------------------------------------
    private void playIndex(int index) {
        if (!queue.jump(index)) return;
        playCurrent();
    }

    private void playCurrent() {
        QueueModel.Item item = queue.currentItem();
        if (item == null) return;
        if (item.youtube) {
            player.pause();
            playerView.setVisibility(View.GONE);
            webView.setVisibility(View.VISIBLE);
            if (!webReady) {
                pendingVideoId = item.id;
                loadWebPlayer();
            } else {
                webView.loadUrl("javascript:window.loadVideo('" + item.id + "')");
            }
            status("YouTube: " + item.title);
        } else {
            webView.loadUrl("javascript:window.pause && window.pause()");
            webView.setVisibility(View.GONE);
            playerView.setVisibility(View.VISIBLE);
            player.setMediaItem(MediaItem.fromUri(Uri.parse(item.id)));
            player.prepare();
            player.play();
            status("Playing: " + item.title);
        }
        refreshQueueUi();
    }

    private void step(int delta) {
        if (queue.isEmpty()) {
            status("No playlist loaded.");
            return;
        }
        final int target = queue.current() + delta;
        if (target < 0) { status("Start of the playlist."); return; }
        if (target >= queue.size()) { status("End of the playlist."); return; }
        playIndex(target);
    }

    private void advance(boolean auto) {
        if (queue.nextIndex() >= 0) {
            queue.advance();
            playCurrent();
        } else {
            status(auto ? "End of the playlist." : "End of the playlist.");
        }
    }

    private void togglePlay() {
        QueueModel.Item item = queue.currentItem();
        if (item == null) {
            if (player.getMediaItemCount() > 0) {
                player.play();
            } else {
                status("Nothing loaded yet.");
            }
            return;
        }
        if (item.youtube) {
            webView.loadUrl("javascript:(function(){if(player){if(player.getPlayerState()===1)" +
                    "player.pauseVideo();else player.playVideo();}})()");
        } else if (player.isPlaying()) {
            player.pause();
        } else {
            player.play();
        }
    }

    private void toggleMic() {
        if (mic.isRunning()) {
            mic.stop();
            status("Microphone monitoring stopped.");
            return;
        }
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, REQ_MIC);
            return;
        }
        mic.start();
        micPanel.setVisibility(View.VISIBLE);
        status("Microphone monitoring on - use headphones.");
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode == REQ_MIC) {
            if (results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) {
                mic.start();
                micPanel.setVisibility(View.VISIBLE);
                status("Microphone monitoring on - use headphones.");
            } else {
                status("Microphone permission denied.");
            }
        }
    }

    // -----------------------------------------------------------------------
    // Sources
    // -----------------------------------------------------------------------
    private void loadFromField() {
        final String text = urlField.getText().toString().trim();
        if (text.isEmpty()) {
            status("Paste a YouTube link first.");
            return;
        }
        final String listId = extractPlaylistId(text);
        final String videoId = extractVideoId(text);
        if (!listId.isEmpty()) {
            status("Reading the playlist from YouTube...");
            new Thread(() -> {
                final PlaylistParser.Result r = PlaylistParser.fetch(listId);
                runOnUiThread(() -> applyPlaylist(r));
            }, "playlist-fetch").start();
        } else if (!videoId.isEmpty()) {
            List<QueueModel.Item> items = new ArrayList<>();
            items.add(QueueModel.Item.youtube(videoId, videoId, ""));
            queue.setItems(items, 0);
            queue.setName("YouTube");
            lastEndedVideoId = "";
            playCurrent();
        } else {
            status("That link has no video or playlist id.");
        }
    }

    private void applyPlaylist(PlaylistParser.Result r) {
        if (!r.ok) {
            status(r.error);
            Toast.makeText(this, r.error, Toast.LENGTH_LONG).show();
            return;
        }
        List<QueueModel.Item> items = new ArrayList<>();
        youtubeQueueIds.clear();
        for (PlaylistParser.Entry e : r.entries) {
            items.add(QueueModel.Item.youtube(e.videoId, e.title, e.duration));
            youtubeQueueIds.add(e.videoId);
        }
        queue.setItems(items, 0);
        queue.setName(r.name);
        lastEndedVideoId = "";
        status("Playlist: " + items.size() + " songs");
        playCurrent();
    }

    private void pickFiles() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[]{"audio/*", "video/*"});
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        startActivityForResult(intent, REQ_FILES);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_FILES || resultCode != RESULT_OK || data == null) return;
        List<Uri> uris = new ArrayList<>();
        ClipData clip = data.getClipData();
        if (clip != null) {
            for (int i = 0; i < clip.getItemCount(); ++i) uris.add(clip.getItemAt(i).getUri());
        } else if (data.getData() != null) {
            uris.add(data.getData());
        }
        if (uris.isEmpty()) return;

        final boolean wasEmpty = queue.isEmpty();
        for (Uri uri : uris) {
            try {
                getContentResolver().takePersistableUriPermission(uri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (Exception ignored) { }
            final String title = displayName(uri);
            queue.append(QueueModel.Item.file(uri.toString(), title));
        }
        if (queue.getName().isEmpty()) queue.setName("Local files");
        status("Added " + uris.size() + " file(s) to the queue.");
        if (wasEmpty) {
            queue.jump(0);
            playCurrent();
        } else {
            refreshQueueUi();
        }
    }

    private String displayName(Uri uri) {
        try (Cursor c = getContentResolver().query(uri, null, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                int idx = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (idx >= 0) return c.getString(idx);
            }
        } catch (Exception ignored) { }
        return uri.getLastPathSegment() == null ? "file" : uri.getLastPathSegment();
    }

    // -----------------------------------------------------------------------
    // Polling and list refresh
    // -----------------------------------------------------------------------
    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            pollYoutube();
            if (mic.isRunning()) {
                int level = (int) Math.min(100, mic.peak * 140);
                micMeter.setProgress(level);
                micMeterLabel.setText(String.format(Locale.US, " %d%%", level));
            }
            handler.postDelayed(this, 500);
        }
    };

    private void pollYoutube() {
        QueueModel.Item item = queue.currentItem();
        if (item == null || !item.youtube || webView.getVisibility() != View.VISIBLE) return;
        webView.evaluateJavascript("(function(){return (window.state&&window.state())||'';})()",
                value -> {
                    if (value == null) return;
                    String s = value;
                    if (s.startsWith("\"") && s.endsWith("\"") && s.length() >= 2) {
                        s = s.substring(1, s.length() - 1);
                    }
                    if (s.isEmpty()) return;
                    String[] parts = s.split("\\|", 5);
                    if (parts.length < 4) return;
                    final int state;
                    try {
                        state = Integer.parseInt(parts[3].trim());
                    } catch (NumberFormatException e) {
                        return;
                    }
                    if (parts.length == 5 && !parts[4].isEmpty()) {
                        QueueModel.Item cur = queue.currentItem();
                        if (cur != null && cur.title.equals(cur.id)) {
                            // Title was a placeholder; adopt the real one.
                            List<QueueModel.Item> items = queue.items();
                            items.set(queue.current(), QueueModel.Item.youtube(cur.id, parts[4],
                                    cur.duration));
                            refreshQueueUi();
                        }
                    }
                    if (state == 0) {
                        QueueModel.Item cur = queue.currentItem();
                        if (cur != null && !cur.id.equals(lastEndedVideoId)) {
                            lastEndedVideoId = cur.id;
                            if (queue.nextIndex() >= 0) {
                                queue.advance();
                                playCurrent();
                            } else {
                                status("End of the playlist.");
                            }
                        }
                    }
                });
    }

    private void refreshQueueUi() {
        adapter.notifyDataSetChanged();
        QueueModel.Item next = queue.at(queue.current() + 1);
        if (next == null) {
            upNextView.setText(queue.isEmpty() ? "NO PLAYLIST" : "END OF PLAYLIST");
        } else {
            upNextView.setText("UP NEXT  " + next.title);
        }
    }

    private void status(String text) {
        statusView.setText(text);
    }

    // -----------------------------------------------------------------------
    // Queue list
    // -----------------------------------------------------------------------
    private class QueueAdapter extends BaseAdapter {
        @Override public int getCount() { return queue.size(); }
        @Override public Object getItem(int position) { return queue.at(position); }
        @Override public long getItemId(int position) { return position; }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            LinearLayout row;
            if (convertView instanceof LinearLayout) {
                row = (LinearLayout) convertView;
            } else {
                row = new LinearLayout(MainActivity.this);
                row.setOrientation(LinearLayout.HORIZONTAL);
                row.setGravity(Gravity.CENTER_VERTICAL);
                row.setPadding(dp(6), dp(6), dp(6), dp(6));

                TextView index = new TextView(MainActivity.this);
                index.setTextColor(DIM);
                index.setTextSize(11);
                index.setWidth(dp(34));
                index.setTag("index");
                row.addView(index);

                TextView title = new TextView(MainActivity.this);
                title.setTextColor(TEXT);
                title.setTextSize(13);
                title.setSingleLine(true);
                title.setEllipsize(android.text.TextUtils.TruncateAt.END);
                title.setTag("title");
                row.addView(title, new LinearLayout.LayoutParams(0,
                        ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

                TextView duration = new TextView(MainActivity.this);
                duration.setTextColor(DIM);
                duration.setTextSize(11);
                duration.setTag("duration");
                row.addView(duration);
            }

            QueueModel.Item item = queue.at(position);
            if (item == null) return row;
            TextView index = row.findViewWithTag("index");
            TextView title = row.findViewWithTag("title");
            TextView duration = row.findViewWithTag("duration");
            index.setText(String.valueOf(position + 1));
            title.setText(item.title);
            duration.setText(item.duration);
            final boolean current = position == queue.current();
            index.setTextColor(current ? ACCENT : DIM);
            title.setTextColor(current ? ACCENT : TEXT);
            row.setBackgroundColor(current ? 0x22E5B96F : Color.TRANSPARENT);
            return row;
        }
    }

    // -----------------------------------------------------------------------
    // URL parsing (same rules as the console)
    // -----------------------------------------------------------------------
    static String extractPlaylistId(String url) {
        int p = url.indexOf("list=");
        if (p < 0) return "";
        int at = p + 5;
        int end = at;
        while (end < url.length()) {
            char c = url.charAt(end);
            if (!(Character.isLetterOrDigit(c) || c == '-' || c == '_')) break;
            ++end;
        }
        int len = end - at;
        return (len >= 12 && len <= 64) ? url.substring(at, end) : "";
    }

    static String extractVideoId(String url) {
        int p = url.indexOf("v=");
        if (p >= 0 && isId(url, p + 2)) return url.substring(p + 2, p + 13);
        for (String marker : new String[]{"youtu.be/", "/embed/", "/shorts/", "/live/"}) {
            int m = url.indexOf(marker);
            if (m >= 0) {
                int at = m + marker.length();
                if (isId(url, at)) return url.substring(at, at + 11);
            }
        }
        if (url.length() == 11 && isId(url, 0)) return url;
        return "";
    }

    private static boolean isId(String s, int at) {
        if (at < 0 || s.length() - at < 11) return false;
        for (int i = 0; i < 11; ++i) {
            char c = s.charAt(at + i);
            if (!(Character.isLetterOrDigit(c) || c == '-' || c == '_')) return false;
        }
        return true;
    }
}
