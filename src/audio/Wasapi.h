#pragma once
// ---------------------------------------------------------------------------
// Thin, explicit WASAPI layer. Everything is raw COM so the behaviour on the
// Focusrite driver is predictable: exclusive event-driven streams for the
// microphone and main output, shared loopback for the karaoke feed.
// ---------------------------------------------------------------------------
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <string>
#include <vector>

namespace au {

struct Endpoint {
  std::wstring id;
  std::wstring name;       // friendly name, e.g. "Analogue 1 + 2 (Focusrite USB Audio)"
  std::wstring device;     // adapter name
  unsigned state = 0;       // DEVICE_STATE_* (active / disabled / unplugged)
  unsigned channels = 0;
  unsigned rate = 0;
  bool isDefault = false;
};

// COM apartment for the calling thread. Call once per thread.
struct ComScope {
  bool ok = false;
  ComScope();
  ~ComScope();
};

std::vector<Endpoint> List(EDataFlow flow);
std::wstring DefaultId(EDataFlow flow);
bool FindEndpoint(const std::wstring& id, EDataFlow flow, Endpoint& out);

// Sample rates the endpoint accepts in exclusive mode for the given channel
// count. Used to build the device capability display.
std::vector<unsigned> SupportedRates(const std::wstring& id, EDataFlow flow, unsigned channels);

// A single opened stream. The owner drives the thread and calls Read/Write.
class Client {
public:
  ~Client() { Close(); }

  HRESULT Open(const std::wstring& deviceId, EDataFlow flow, bool loopback, unsigned rate,
               unsigned channels, unsigned bufferFrames, bool exclusive, HANDLE eventHandle);
  void Close();
  HRESULT Start();
  void Stop();

  // Capture: pull the next packet. Returns 0 frames if nothing is ready.
  HRESULT ReadNext(BYTE** data, UINT32* frames);
  void ReleaseCapture(UINT32 frames);
  UINT32 PacketSize();

  // Render: get a buffer to fill (silence pre-filled) and commit it.
  HRESULT GetRenderBuffer(BYTE** data, UINT32* frames);
  HRESULT CommitRender(UINT32 frames);

  bool isOpen() const { return client_ != nullptr; }
  // Some WDM drivers (the Focusrite USB one among them) refuse an event-driven
  // exclusive stream but accept a polling exclusive stream.
  bool eventDriven() const { return eventDriven_; }
  bool isFloat() const { return float_; }
  int bytesPerSample() const { return bytesPer_; }
  unsigned channels() const { return channels_; }
  unsigned rate() const { return rate_; }
  unsigned bufferFrames() const { return bufferFrames_; }
  bool exclusive() const { return exclusive_; }
  double bufferLatencyMs() const { return 1000.0 * bufferFrames_ / (double)rate_; }
  double streamLatencyMs() const { return streamLatencyMs_; }
  const wchar_t* lastError() const { return err_.c_str(); }
  unsigned long long underruns() const { return underruns_; }
  unsigned long long dropouts() const { return dropouts_; }

  // Interleaved float output helper for capture packets; converts the native
  // sample type and downmixes/selects one channel.
  static void ToMono(const BYTE* data, UINT32 frames, unsigned channels, int bytesPer,
                     bool isFloat, int selectedChannel, float* out);
  static void ToStereo(const BYTE* data, UINT32 frames, unsigned channels, int bytesPer,
                       bool isFloat, float* outInterleaved);

private:
  IAudioClient* client_ = nullptr;
  IAudioCaptureClient* capture_ = nullptr;
  IAudioRenderClient* render_ = nullptr;
  HANDLE event_ = nullptr;
  bool float_ = true;
  int bytesPer_ = 4;
  unsigned channels_ = 2;
  unsigned rate_ = 48000;
  unsigned bufferFrames_ = 0;
  bool exclusive_ = false;
  bool eventDriven_ = true;
  UINT32 packetSize_ = 0;
  bool silent_ = false;
  double streamLatencyMs_ = 0;
  HRESULT OpenImpl(const std::wstring& deviceId, EDataFlow flow, bool loopback, unsigned rate,
                   unsigned channels, unsigned bufferFrames, bool exclusive, HANDLE eventHandle,
                   int depth);
  unsigned long long underruns_ = 0;
  unsigned long long dropouts_ = 0;
  std::wstring err_;
};

std::wstring HrToString(HRESULT hr);
std::wstring Utf8ToWide(const std::string& s);

} // namespace au
