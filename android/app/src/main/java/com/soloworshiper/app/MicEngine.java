package com.soloworshiper.app;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.AudioTrack;
import android.media.MediaRecorder;

/**
 * Live microphone monitor for phones and tablets.
 *
 * Real DSP, not a placeholder: high-pass, three-band EQ, compressor, reverb send
 * and a soft limiter, running on a duplex AAudio path (AudioRecord -> process ->
 * AudioTrack) at 48 kHz with the lowest buffer the device grants.
 *
 * Deliberate limit: Android offers no way to capture another application's audio,
 * so on this platform the phone's own playback (YouTube embed, local player) is
 * heard from the system mixer and is NOT mixed into this path. What this engine
 * provides is a monitored, processed vocal - the same chain the desktop console
 * applies to the microphone.
 */
public class MicEngine {

    public static final int SR = 48000;
    private static final int BLOCK = 256;

    // Controls (written from the UI thread, read once per block).
    public volatile float gainDb = 6f;      // input trim, -12 .. +30
    public volatile float lowDb = 0f;       // 150 Hz shelf,  -12 .. +12
    public volatile float midDb = 0f;       // 1.2 kHz bell, -12 .. +12
    public volatile float highDb = 0f;      // 6 kHz shelf,  -12 .. +12
    public volatile float reverbMix = 0.2f; // 0 .. 1
    public volatile float volume = 0.8f;    // output, 0 .. 1
    public volatile float peak = 0f;        // meter, 0 .. 1

    private Thread thread;
    private volatile boolean running;
    public volatile String lastError = "";

    public boolean isRunning() { return running; }

    public synchronized void start() {
        if (running) return;
        running = true;
        thread = new Thread(this::loop, "mic-engine");
        thread.start();
    }

    public synchronized void stop() {
        running = false;
        Thread t = thread;
        thread = null;
        if (t != null) {
            try { t.join(1500); } catch (InterruptedException ignored) { }
        }
        peak = 0f;
    }

    private void loop() {
        final int inMin = AudioRecord.getMinBufferSize(SR, AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_FLOAT);
        final int outMin = AudioTrack.getMinBufferSize(SR, AudioFormat.CHANNEL_OUT_STEREO,
                AudioFormat.ENCODING_PCM_FLOAT);
        if (inMin <= 0 || outMin <= 0) {
            lastError = "This device does not support 48 kHz float audio.";
            running = false;
            return;
        }

        AudioRecord record = null;
        AudioTrack track = null;
        try {
            record = new AudioRecord(MediaRecorder.AudioSource.MIC, SR,
                    AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT,
                    Math.max(inMin * 2, BLOCK * 16));
            track = new AudioTrack.Builder()
                    .setAudioAttributes(new AudioAttributes.Builder()
                            .setUsage(AudioAttributes.USAGE_MEDIA)
                            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                            .build())
                    .setAudioFormat(new AudioFormat.Builder()
                            .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                            .setSampleRate(SR)
                            .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                            .build())
                    .setBufferSizeInBytes(Math.max(outMin * 2, BLOCK * 16))
                    .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
                    .setTransferMode(AudioTrack.MODE_STREAM)
                    .build();

            if (record.getState() != AudioRecord.STATE_INITIALIZED ||
                    track.getState() != AudioTrack.STATE_INITIALIZED) {
                lastError = "The microphone is not available.";
                return;
            }

            record.startRecording();
            track.play();

            final float[] mic = new float[BLOCK];
            final float[] out = new float[BLOCK * 2];
            final Chain chain = new Chain(SR);

            while (running) {
                final int n = record.read(mic, 0, BLOCK, AudioRecord.READ_BLOCKING);
                if (n <= 0) continue;
                chain.set(gainDb, lowDb, midDb, highDb, reverbMix, volume);
                float blockPeak = 0f;
                for (int i = 0; i < n; ++i) {
                    final float v = chain.step(mic[i]);
                    out[i * 2] = v;
                    out[i * 2 + 1] = v;
                    final float a = Math.abs(v);
                    if (a > blockPeak) blockPeak = a;
                }
                peak = Math.max(blockPeak, peak * 0.86f);
                track.write(out, 0, n * 2, AudioTrack.WRITE_BLOCKING);
            }
        } catch (Throwable t) {
            lastError = t.getMessage() == null ? "audio error" : t.getMessage();
        } finally {
            running = false;
            if (record != null) {
                try { record.stop(); } catch (Throwable ignored) { }
                record.release();
            }
            if (track != null) {
                try { track.stop(); } catch (Throwable ignored) { }
                track.release();
            }
        }
    }

    // -----------------------------------------------------------------------
    // The chain: trim -> EQ -> compressor -> reverb send -> volume -> limiter.
    // -----------------------------------------------------------------------
    private static final class Chain {
        private final Biquad low = new Biquad();
        private final Biquad mid = new Biquad();
        private final Biquad high = new Biquad();
        private final Biquad hp = new Biquad();
        private final Compressor comp = new Compressor();
        private final Reverb reverb;
        private int sr;

        private float lastLow = 999f, lastMid = 999f, lastHigh = 999f;
        private float gain = 1f, wet = 0.2f, vol = 0.8f;

        Chain(int sampleRate) {
            sr = sampleRate;
            hp.highPass(sr, 80, 0.7);
            reverb = new Reverb(sr);
        }

        void set(float gainDb, float lowDb, float midDb, float highDb, float reverbMix,
                 float volume) {
            gain = (float) Math.pow(10.0, gainDb / 20.0);
            wet = reverbMix;
            vol = volume;
            if (lowDb != lastLow) { low.lowShelf(sr, 150, lowDb); lastLow = lowDb; }
            if (midDb != lastMid) { mid.peak(sr, 1200, 0.9, midDb); lastMid = midDb; }
            if (highDb != lastHigh) { high.highShelf(sr, 6000, highDb); lastHigh = highDb; }
        }

        float step(float x) {
            float s = hp.step(x) * gain;
            s = low.step(s);
            s = mid.step(s);
            s = high.step(s);
            s = comp.step(s);
            final float r = reverb.step(s);
            s = s + (r - s) * wet;
            s *= vol;
            // Soft limiter: never let a feedback howl leave at full scale.
            if (s > 0.95f) s = 0.95f + (float) Math.tanh((s - 0.95f) * 8.0) * 0.05f;
            else if (s < -0.95f) s = -0.95f + (float) Math.tanh((s + 0.95f) * 8.0) * 0.05f;
            return s;
        }
    }

    /** RBJ biquad; coefficients are recomputed only when a control changes. */
    private static final class Biquad {
        private float b0 = 1, b1, b2, a1, a2;
        private float x1, x2, y1, y2;

        float step(float x) {
            final float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }

        void highPass(double sr, double f, double q) {
            final double w = 2 * Math.PI * f / sr;
            final double alpha = Math.sin(w) / (2 * q);
            final double cos = Math.cos(w);
            final double b0n = (1 + cos) / 2, b1n = -(1 + cos), b2n = (1 + cos) / 2;
            final double a0 = 1 + alpha, a1n = -2 * cos, a2n = 1 - alpha;
            set(b0n, b1n, b2n, a0, a1n, a2n);
        }

        void lowShelf(double sr, double f, double db) {
            final double A = Math.pow(10, db / 40);
            final double w = 2 * Math.PI * f / sr;
            final double cos = Math.cos(w), sin = Math.sin(w);
            final double beta = Math.sqrt(A) / 0.707;
            final double a0 = (A + 1) + (A - 1) * cos + beta * sin;
            final double b0n = A * ((A + 1) - (A - 1) * cos + beta * sin);
            final double b1n = 2 * A * ((A - 1) - (A + 1) * cos);
            final double b2n = A * ((A + 1) - (A - 1) * cos - beta * sin);
            final double a1n = -2 * ((A - 1) + (A + 1) * cos);
            final double a2n = (A + 1) + (A - 1) * cos - beta * sin;
            set(b0n, b1n, b2n, a0, a1n, a2n);
        }

        void highShelf(double sr, double f, double db) {
            final double A = Math.pow(10, db / 40);
            final double w = 2 * Math.PI * f / sr;
            final double cos = Math.cos(w), sin = Math.sin(w);
            final double beta = Math.sqrt(A) / 0.707;
            final double a0 = (A + 1) - (A - 1) * cos + beta * sin;
            final double b0n = A * ((A + 1) + (A - 1) * cos + beta * sin);
            final double b1n = -2 * A * ((A - 1) + (A + 1) * cos);
            final double b2n = A * ((A + 1) + (A - 1) * cos - beta * sin);
            final double a1n = 2 * ((A - 1) - (A + 1) * cos);
            final double a2n = (A + 1) - (A - 1) * cos - beta * sin;
            set(b0n, b1n, b2n, a0, a1n, a2n);
        }

        void peak(double sr, double f, double q, double db) {
            final double A = Math.pow(10, db / 40);
            final double w = 2 * Math.PI * f / sr;
            final double alpha = Math.sin(w) / (2 * q);
            final double cos = Math.cos(w);
            final double a0 = 1 + alpha / A;
            final double b0n = 1 + alpha * A;
            final double b1n = -2 * cos;
            final double b2n = 1 - alpha * A;
            final double a1n = -2 * cos;
            final double a2n = 1 - alpha / A;
            set(b0n, b1n, b2n, a0, a1n, a2n);
        }

        private void set(double nb0, double nb1, double nb2, double a0, double na1, double na2) {
            b0 = (float) (nb0 / a0);
            b1 = (float) (nb1 / a0);
            b2 = (float) (nb2 / a0);
            a1 = (float) (na1 / a0);
            a2 = (float) (na2 / a0);
        }
    }

    /** Feed-forward compressor with attack/release and fixed makeup. */
    private static final class Compressor {
        private float env;
        private final float attack = 0.0055f;  // ~12 ms at 48 kHz
        private final float release = 0.00012f; // ~180 ms
        private static final float THRESH_DB = -22f;
        private static final float RATIO = 3.5f;
        private static final float MAKEUP = (float) Math.pow(10, 4.0 / 20.0);

        float step(float x) {
            final float level = Math.abs(x);
            env += (level - env) * (level > env ? attack : release);
            final float db = 20f * (float) Math.log10(Math.max(env, 1e-6f));
            final float over = db - THRESH_DB;
            final float gr = over > 0 ? over * (1f - 1f / RATIO) : 0f;
            return x * (float) Math.pow(10, -gr / 20.0) * MAKEUP;
        }
    }

    /** Compact Schroeder-style reverb: four combs, two allpasses. */
    private static final class Reverb {
        private final float[][] comb;
        private final int[] combPos;
        private final int[] combLen;
        private final float[][] ap;
        private final int[] apPos;
        private final int[] apLen;
        private float damp;

        Reverb(int sr) {
            final float[] combMs = {29.7f, 37.1f, 41.1f, 43.7f};
            final float[] apMs = {5.0f, 1.7f};
            comb = new float[4][];
            combPos = new int[4];
            combLen = new int[4];
            for (int i = 0; i < 4; ++i) {
                combLen[i] = Math.max(1, (int) (sr * combMs[i] / 1000f));
                comb[i] = new float[combLen[i]];
            }
            ap = new float[2][];
            apPos = new int[2];
            apLen = new int[2];
            for (int i = 0; i < 2; ++i) {
                apLen[i] = Math.max(1, (int) (sr * apMs[i] / 1000f));
                ap[i] = new float[apLen[i]];
            }
        }

        float step(float x) {
            float sum = 0f;
            for (int i = 0; i < 4; ++i) {
                final float y = comb[i][combPos[i]];
                sum += y;
                damp = y * 0.35f + damp * 0.65f;
                comb[i][combPos[i]] = x + damp * 0.83f;
                if (++combPos[i] >= combLen[i]) combPos[i] = 0;
            }
            float v = sum * 0.25f;
            for (int i = 0; i < 2; ++i) {
                final float buf = ap[i][apPos[i]];
                final float out = -v + buf;
                ap[i][apPos[i]] = v + buf * 0.5f;
                if (++apPos[i] >= apLen[i]) apPos[i] = 0;
                v = out;
            }
            return v;
        }
    }
}
