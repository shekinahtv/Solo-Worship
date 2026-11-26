import AVFoundation
import Foundation
import os

/// The controls the UI writes. Read once per audio block, never per sample.
struct MicParams {
    var gainDb: Float = 6
    var lowDb: Float = 0
    var midDb: Float = 0
    var highDb: Float = 0
    var reverbMix: Float = 0.2
    var volume: Float = 0.8
}

enum MicError: LocalizedError {
    case noInput
    case noOutput
    case deviceUnavailable

    var errorDescription: String? {
        switch self {
        case .noInput: return "No microphone input is available."
        case .noOutput: return "No audio output is available."
        case .deviceUnavailable: return "The selected audio device could not be opened."
        }
    }
}

/// Live microphone monitor.
///
/// Real DSP, not a placeholder: 80 Hz high pass, three-band EQ, compressor with
/// makeup, reverb send, output volume and a soft limiter - the same chain the
/// desktop console applies to the microphone, on the lowest buffer the device
/// grants, with a live input meter.
///
/// Deliberate limit: macOS gives no application access to another application's
/// audio, so the karaoke track (YouTube embed or local file) is heard from the
/// system output and is NOT mixed into this path. What this engine does is the
/// monitored, processed vocal.
final class MicEngine {

    private let engine = AVAudioEngine()
    private var sourceNode: AVAudioSourceNode?
    private var chain: Chain?
    private var converter: AVAudioConverter?
    private var monoFormat: AVAudioFormat?
    private var tapInstalled = false

    private let ring = Ring(capacity: 48_000 * 2)
    private let params = OSAllocatedUnfairLock(initialState: MicParams())
    private let peakLock = OSAllocatedUnfairLock(initialState: Float(0))

    private(set) var running = false
    private(set) var lastError = ""
    private(set) var sampleRate: Double = 48_000

    var peak: Float { peakLock.withLock { $0 } }

    func set(_ value: MicParams) {
        params.withLock { $0 = value }
    }

    /// Starts the monitor. Returns nil on success, or a message to show the user.
    func start(input: AudioDeviceID, output: AudioDeviceID) -> String? {
        guard !running else { return nil }
        do {
            try build(input: input, output: output)
            engine.prepare()
            try engine.start()
            running = true
            lastError = ""
            return nil
        } catch {
            if let mic = error as? MicError, let text = mic.errorDescription {
                lastError = text
            } else {
                lastError = error.localizedDescription
            }
            teardown()
            return lastError
        }
    }

    func stop() {
        guard running || sourceNode != nil || tapInstalled else { return }
        teardown()
        running = false
        peakLock.withLock { $0 = 0 }
    }

    private func build(input: AudioDeviceID, output: AudioDeviceID) throws {
        if input != 0 {
            try AudioDevices.setDevice(input, on: engine.inputNode.audioUnit)
        }
        if output != 0 {
            try AudioDevices.setDevice(output, on: engine.outputNode.audioUnit)
        }

        let inFormat = engine.inputNode.outputFormat(forBus: 0)
        guard inFormat.sampleRate > 0, inFormat.channelCount > 0 else { throw MicError.noInput }
        let outFormat = engine.outputNode.outputFormat(forBus: 0)
        let rate = outFormat.sampleRate > 0 ? outFormat.sampleRate : inFormat.sampleRate
        sampleRate = rate

        guard let playFormat = AVAudioFormat(standardFormatWithSampleRate: rate, channels: 2),
              let mono = AVAudioFormat(standardFormatWithSampleRate: rate, channels: 1) else {
            throw MicError.noOutput
        }
        monoFormat = mono
        chain = Chain(sampleRate: rate)
        converter = (abs(inFormat.sampleRate - rate) > 0.5 || inFormat.channelCount != 1)
            ? AVAudioConverter(from: inFormat, to: mono)
            : nil

        engine.inputNode.installTap(onBus: 0, bufferSize: 256, format: inFormat) { [weak self] buffer, _ in
            self?.capture(buffer)
        }
        tapInstalled = true

        let node = AVAudioSourceNode(format: playFormat) { [weak self] _, _, frameCount, audioBufferList -> OSStatus in
            guard let self else { return noErr }
            let list = UnsafeMutableAudioBufferListPointer(audioBufferList)
            let frames = Int(frameCount)
            guard let first = list.first,
                  let destination = first.mData?.assumingMemoryBound(to: Float.self) else { return noErr }
            self.ring.read(destination, frames)
            if list.count > 1 {
                for channel in 1..<list.count {
                    if let copy = list[channel].mData?.assumingMemoryBound(to: Float.self) {
                        memcpy(copy, destination, frames * MemoryLayout<Float>.size)
                    }
                }
            }
            return noErr
        }
        engine.attach(node)
        engine.connect(node, to: engine.mainMixerNode, format: playFormat)
        sourceNode = node
    }

    private func teardown() {
        engine.stop()
        if tapInstalled {
            engine.inputNode.removeTap(onBus: 0)
            tapInstalled = false
        }
        if let node = sourceNode {
            engine.detach(node)
            sourceNode = nil
        }
        converter = nil
        monoFormat = nil
        chain = nil
    }

    // -----------------------------------------------------------------------
    // Capture: convert to the render rate, run the chain, push to the ring.
    // -----------------------------------------------------------------------
    private func capture(_ buffer: AVAudioPCMBuffer) {
        guard let chain else { return }
        chain.set(params.withLock { $0 })

        if let converter, let mono = monoFormat {
            let ratio = mono.sampleRate / max(1, buffer.format.sampleRate)
            let capacity = AVAudioFrameCount(Double(buffer.frameLength) * ratio) + 64
            guard let converted = AVAudioPCMBuffer(pcmFormat: mono, frameCapacity: capacity) else { return }
            var supplied = false
            var conversionError: NSError?
            converter.convert(to: converted, error: &conversionError) { _, status in
                if supplied {
                    status.pointee = .noDataNow
                    return nil
                }
                supplied = true
                status.pointee = .haveData
                return buffer
            }
            if conversionError != nil || converted.frameLength == 0 { return }
            if let data = converted.floatChannelData?[0] {
                process(data, Int(converted.frameLength), chain)
            }
        } else if let data = buffer.floatChannelData?[0] {
            process(data, Int(buffer.frameLength), chain)
        }
    }

    private func process(_ source: UnsafeMutablePointer<Float>, _ count: Int, _ chain: Chain) {
        guard count > 0 else { return }
        var scratch = [Float](repeating: 0, count: count)
        var blockPeak: Float = 0
        scratch.withUnsafeMutableBufferPointer { destination in
            guard let base = destination.baseAddress else { return }
            for index in 0..<count {
                let value = chain.step(source[index])
                base[index] = value
                let magnitude = abs(value)
                if magnitude > blockPeak { blockPeak = magnitude }
            }
            ring.write(base, count)
        }
        peakLock.withLock { state in
            state = max(blockPeak, state * 0.86)
        }
    }
}

// ---------------------------------------------------------------------------
// A single-producer / single-consumer float ring with a short critical section.
// ---------------------------------------------------------------------------
private final class Ring {
    private struct State {
        var buffer: [Float]
        var read = 0
        var count = 0
    }

    private let state: OSAllocatedUnfairLock<State>

    init(capacity: Int) {
        state = OSAllocatedUnfairLock(initialState: State(buffer: [Float](repeating: 0, count: capacity)))
    }

    func write(_ source: UnsafePointer<Float>, _ count: Int) {
        state.withLock { ring in
            let capacity = ring.buffer.count
            guard capacity > 0 else { return }
            for index in 0..<count {
                if ring.count == capacity {
                    ring.read = (ring.read + 1) % capacity
                    ring.count -= 1
                }
                ring.buffer[(ring.read + ring.count) % capacity] = source[index]
                ring.count += 1
            }
        }
    }

    func read(_ destination: UnsafeMutablePointer<Float>, _ count: Int) {
        state.withLock { ring in
            let capacity = ring.buffer.count
            for index in 0..<count {
                if ring.count > 0 {
                    destination[index] = ring.buffer[ring.read]
                    ring.read = (ring.read + 1) % capacity
                    ring.count -= 1
                } else {
                    destination[index] = 0
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// The chain: trim -> high pass -> EQ -> compressor -> reverb send -> volume
// -> soft limiter.
// ---------------------------------------------------------------------------
private final class Chain {
    private let hp = Biquad()
    private let low = Biquad()
    private let mid = Biquad()
    private let high = Biquad()
    private let compressor = Compressor()
    private let reverb: Reverb
    private let sampleRate: Float

    private var lastLow: Float = 999
    private var lastMid: Float = 999
    private var lastHigh: Float = 999
    private var gain: Float = 1
    private var wet: Float = 0.2
    private var volume: Float = 0.8

    init(sampleRate: Double) {
        self.sampleRate = Float(sampleRate)
        reverb = Reverb(sampleRate: sampleRate)
        hp.highPass(Float(sampleRate), 80, 0.7)
    }

    func set(_ params: MicParams) {
        gain = powf(10, params.gainDb / 20)
        wet = params.reverbMix
        volume = params.volume
        if params.lowDb != lastLow {
            low.lowShelf(sampleRate, 150, params.lowDb)
            lastLow = params.lowDb
        }
        if params.midDb != lastMid {
            mid.peak(sampleRate, 1200, 0.9, params.midDb)
            lastMid = params.midDb
        }
        if params.highDb != lastHigh {
            high.highShelf(sampleRate, 6000, params.highDb)
            lastHigh = params.highDb
        }
    }

    func step(_ input: Float) -> Float {
        var value = hp.step(input) * gain
        value = low.step(value)
        value = mid.step(value)
        value = high.step(value)
        value = compressor.step(value)
        let tail = reverb.step(value)
        value += (tail - value) * wet
        value *= volume
        // Soft limiter: never let a feedback howl leave at full scale.
        if value > 0.95 {
            value = 0.95 + tanhf((value - 0.95) * 8) * 0.05
        } else if value < -0.95 {
            value = -0.95 + tanhf((value + 0.95) * 8) * 0.05
        }
        return value
    }
}

/// RBJ biquad; coefficients are recomputed only when a control changes.
private final class Biquad {
    private var b0: Float = 1, b1: Float = 0, b2: Float = 0, a1: Float = 0, a2: Float = 0
    private var x1: Float = 0, x2: Float = 0, y1: Float = 0, y2: Float = 0

    func step(_ x: Float) -> Float {
        let y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2 = x1
        x1 = x
        y2 = y1
        y1 = y
        return y
    }

    func highPass(_ sr: Float, _ freq: Float, _ q: Float) {
        let w = 2 * Double.pi * Double(freq) / Double(sr)
        let alpha = sin(w) / (2 * Double(q))
        let cosw = cos(w)
        apply((1 + cosw) / 2, -(1 + cosw), (1 + cosw) / 2, 1 + alpha, -2 * cosw, 1 - alpha)
    }

    func lowShelf(_ sr: Float, _ freq: Float, _ db: Float) {
        let a = pow(10, Double(db) / 40)
        let w = 2 * Double.pi * Double(freq) / Double(sr)
        let cosw = cos(w), sinw = sin(w)
        let beta = sqrt(a) / 0.707
        let a0 = (a + 1) + (a - 1) * cosw + beta * sinw
        let b0n = a * ((a + 1) - (a - 1) * cosw + beta * sinw)
        let b1n = 2 * a * ((a - 1) - (a + 1) * cosw)
        let b2n = a * ((a + 1) - (a - 1) * cosw - beta * sinw)
        let a1n = -2 * ((a - 1) + (a + 1) * cosw)
        let a2n = (a + 1) + (a - 1) * cosw - beta * sinw
        apply(b0n, b1n, b2n, a0, a1n, a2n)
    }

    func highShelf(_ sr: Float, _ freq: Float, _ db: Float) {
        let a = pow(10, Double(db) / 40)
        let w = 2 * Double.pi * Double(freq) / Double(sr)
        let cosw = cos(w), sinw = sin(w)
        let beta = sqrt(a) / 0.707
        let a0 = (a + 1) - (a - 1) * cosw + beta * sinw
        let b0n = a * ((a + 1) + (a - 1) * cosw + beta * sinw)
        let b1n = -2 * a * ((a - 1) + (a + 1) * cosw)
        let b2n = a * ((a + 1) + (a - 1) * cosw - beta * sinw)
        let a1n = 2 * ((a - 1) - (a + 1) * cosw)
        let a2n = (a + 1) - (a - 1) * cosw - beta * sinw
        apply(b0n, b1n, b2n, a0, a1n, a2n)
    }

    func peak(_ sr: Float, _ freq: Float, _ q: Float, _ db: Float) {
        let a = pow(10, Double(db) / 40)
        let w = 2 * Double.pi * Double(freq) / Double(sr)
        let alpha = sin(w) / (2 * Double(q))
        let cosw = cos(w)
        apply(1 + alpha * a, -2 * cosw, 1 - alpha * a, 1 + alpha / a, -2 * cosw, 1 - alpha / a)
    }

    private func apply(_ nb0: Double, _ nb1: Double, _ nb2: Double,
                       _ a0: Double, _ na1: Double, _ na2: Double) {
        b0 = Float(nb0 / a0)
        b1 = Float(nb1 / a0)
        b2 = Float(nb2 / a0)
        a1 = Float(na1 / a0)
        a2 = Float(na2 / a0)
    }
}

/// Feed-forward compressor with attack/release and fixed makeup.
private final class Compressor {
    private var envelope: Float = 0
    private let attack: Float = 0.0055     // ~12 ms at 48 kHz
    private let release: Float = 0.00012   // ~180 ms
    private let thresholdDb: Float = -22
    private let ratio: Float = 3.5
    private let makeup: Float = 1.585      // +4 dB

    func step(_ x: Float) -> Float {
        let level = abs(x)
        envelope += (level - envelope) * (level > envelope ? attack : release)
        let db = 20 * log10f(max(envelope, 1e-6))
        let over = db - thresholdDb
        let reduction = over > 0 ? over * (1 - 1 / ratio) : 0
        return x * powf(10, -reduction / 20) * makeup
    }
}

/// Compact Schroeder-style reverb: four combs, two allpasses.
private final class Reverb {
    private var comb: [[Float]] = []
    private var combPos: [Int]
    private var allpass: [[Float]] = []
    private var allpassPos: [Int]
    private var damp: Float = 0

    init(sampleRate: Double) {
        let combMs: [Double] = [29.7, 37.1, 41.1, 43.7]
        let allpassMs: [Double] = [5.0, 1.7]
        combPos = Array(repeating: 0, count: combMs.count)
        allpassPos = Array(repeating: 0, count: allpassMs.count)
        for ms in combMs {
            comb.append([Float](repeating: 0, count: max(1, Int(sampleRate * ms / 1000))))
        }
        for ms in allpassMs {
            allpass.append([Float](repeating: 0, count: max(1, Int(sampleRate * ms / 1000))))
        }
    }

    func step(_ x: Float) -> Float {
        var sum: Float = 0
        for index in 0..<comb.count {
            let y = comb[index][combPos[index]]
            sum += y
            damp = y * 0.35 + damp * 0.65
            comb[index][combPos[index]] = x + damp * 0.83
            combPos[index] = (combPos[index] + 1) % comb[index].count
        }
        var value = sum * 0.25
        for index in 0..<allpass.count {
            let buffer = allpass[index][allpassPos[index]]
            let out = -value + buffer
            allpass[index][allpassPos[index]] = value + buffer * 0.5
            allpassPos[index] = (allpassPos[index] + 1) % allpass[index].count
            value = out
        }
        return value
    }
}
