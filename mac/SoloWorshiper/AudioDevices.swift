import CoreAudio
import Foundation

/// One installed audio device, as CoreAudio reports it. Every driver the system
/// has loaded is listed - the built-in output, USB interfaces such as a Focusrite,
/// virtual devices - whether or not anything is currently plugged into it.
struct AudioDevice: Identifiable, Hashable {
    let id: AudioDeviceID
    let name: String
    let inputChannels: Int
    let outputChannels: Int

    var isInput: Bool { inputChannels > 0 }
    var isOutput: Bool { outputChannels > 0 }
}

enum AudioDevices {

    private static func address(_ selector: AudioObjectPropertySelector,
                                _ scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal)
        -> AudioObjectPropertyAddress {
        AudioObjectPropertyAddress(mSelector: selector,
                                   mScope: scope,
                                   mElement: kAudioObjectPropertyElementMain)
    }

    /// Every device the hardware layer knows about.
    static func all() -> [AudioDevice] {
        var addr = address(kAudioHardwarePropertyDevices)
        var size: UInt32 = 0
        let system = AudioObjectID(kAudioObjectSystemObject)
        guard AudioObjectGetPropertyDataSize(system, &addr, 0, nil, &size) == noErr,
              size > 0 else { return [] }

        let count = Int(size) / MemoryLayout<AudioDeviceID>.size
        var ids = [AudioDeviceID](repeating: 0, count: count)
        guard AudioObjectGetPropertyData(system, &addr, 0, nil, &size, &ids) == noErr else { return [] }

        return ids.compactMap { id in
            let ins = channels(id, scope: kAudioObjectPropertyScopeInput)
            let outs = channels(id, scope: kAudioObjectPropertyScopeOutput)
            guard ins > 0 || outs > 0 else { return nil }
            return AudioDevice(id: id, name: name(id), inputChannels: ins, outputChannels: outs)
        }
    }

    /// The system default device for the given direction.
    static func defaultDevice(input: Bool) -> AudioDeviceID? {
        var addr = address(input ? kAudioHardwarePropertyDefaultInputDevice
                                 : kAudioHardwarePropertyDefaultOutputDevice)
        var id = AudioDeviceID(0)
        var size = UInt32(MemoryLayout<AudioDeviceID>.size)
        let status = AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject),
                                               &addr, 0, nil, &size, &id)
        return status == noErr && id != 0 ? id : nil
    }

    /// Point an already-created audio unit (the engine's input or output node) at
    /// one specific device. Must be done while the engine is stopped.
    static func setDevice(_ id: AudioDeviceID, on unit: AudioUnit?) throws {
        guard let unit else { throw MicError.deviceUnavailable }
        var device = id
        let status = AudioUnitSetProperty(unit,
                                          kAudioOutputUnitProperty_CurrentDevice,
                                          kAudioUnitScope_Global,
                                          0,
                                          &device,
                                          UInt32(MemoryLayout<AudioDeviceID>.size))
        guard status == noErr else { throw MicError.deviceUnavailable }
    }

    private static func name(_ id: AudioDeviceID) -> String {
        var addr = address(kAudioObjectPropertyName)
        var value: Unmanaged<CFString>?
        var size = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        let status = withUnsafeMutablePointer(to: &value) { pointer in
            AudioObjectGetPropertyData(id, &addr, 0, nil, &size, pointer)
        }
        if status == noErr, let value {
            return value.takeRetainedValue() as String
        }
        return "Audio device"
    }

    private static func channels(_ id: AudioDeviceID, scope: AudioObjectPropertyScope) -> Int {
        var addr = address(kAudioDevicePropertyStreamConfiguration, scope)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(id, &addr, 0, nil, &size) == noErr, size > 0 else {
            return 0
        }
        let raw = UnsafeMutableRawPointer.allocate(byteCount: Int(size),
                                                   alignment: MemoryLayout<AudioBufferList>.alignment)
        defer { raw.deallocate() }
        guard AudioObjectGetPropertyData(id, &addr, 0, nil, &size, raw) == noErr else { return 0 }
        let list = UnsafeMutableAudioBufferListPointer(raw.assumingMemoryBound(to: AudioBufferList.self))
        return list.reduce(0) { $0 + Int($1.mNumberChannels) }
    }
}
