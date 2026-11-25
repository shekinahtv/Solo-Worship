#pragma once
// ---------------------------------------------------------------------------
// Minimal ASIO host.
//
// The Steinberg ASIO interfaces are re-declared here (same layout as the 2.3
// SDK) so the application links against any installed ASIO driver without
// shipping the SDK. Drivers are plain COM in-process servers registered under
// HKLM\SOFTWARE\ASIO, so no extra runtime is required.
// ---------------------------------------------------------------------------
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace asio {

typedef long ASIOBool;
typedef long ASIOError;
typedef double ASIOSampleRate;
typedef long ASIOSamples[2];
typedef unsigned long ASIOTimeStamp[2];

enum {
  ASE_OK = 0,
  ASE_SUCCESS = 0x3f4847a0,
  ASE_NotPresent = -1000,
  ASE_HWMalfunction = -999,
  ASE_InvalidParameter = -998,
  ASE_InvalidMode = -997,
  ASE_SPNotAdvancing = -996,
  ASE_NoClock = -995,
  ASE_NoMemory = -994
};

enum AsioMessageSelector {
  kAsioSelectorSupported = 1,
  kAsioEngineVersion = 2,
  kAsioResetRequest = 3,
  kAsioBufferSizeChange = 4,
  kAsioResyncRequest = 5,
  kAsioLatenciesChanged = 6,
  kAsioSupportsTimeInfo = 7,
  kAsioSupportsTimeCode = 8,
  kAsioMMCCommand = 9,
  kAsioSupportsInputMonitor = 10,
  kAsioSupportsInputGain = 11,
  kAsioSupportsInputMeter = 12,
  kAsioSupportsOutputGain = 13,
  kAsioSupportsOutputMeter = 14,
  kAsioOverload = 15
};

struct ASIOChannelInfo {
  long channel;
  ASIOBool isInput;
  ASIOBool isActive;
  long channelGroup;
  long type;
  char name[32];
};

struct ASIOBufferInfo {
  ASIOBool isInput;
  long channelNum;
  void* buffers[2];
};

struct ASIOClockSource {
  long index;
  long associatedChannel;
  long associatedGroup;
  ASIOBool isCurrentSource;
  char name[32];
};

struct ASIOTimeInfo {
  long speed;
  ASIOTimeStamp systemTime;
  ASIOSamples samplePosition;
  ASIOSampleRate sampleRate;
  unsigned long flags;
  char reserved[12];
};

struct ASIOTime {
  long reserved[4];
  ASIOTimeInfo timeInfo;
  long reserved2[2];
};

struct ASIOCallbacks {
  void (*bufferSwitch)(long doubleBufferIndex, ASIOBool directProcess);
  void (*sampleRateDidChange)(ASIOSampleRate sRate);
  long (*asioMessage)(long selector, long value, void* message, double* opt);
  ASIOTime* (*bufferSwitchTimeInfo)(ASIOTime* params, long doubleBufferIndex,
                                    ASIOBool directProcess);
};

struct IASIO {
  virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) = 0;
  virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
  virtual ULONG STDMETHODCALLTYPE Release() = 0;
  virtual ASIOBool init(void* sysHandle) = 0;
  virtual void getDriverName(char* name) = 0;
  virtual long getDriverVersion() = 0;
  virtual void getErrorMessage(char* string) = 0;
  virtual ASIOError start() = 0;
  virtual ASIOError stop() = 0;
  virtual ASIOError getChannels(long* numInputChannels, long* numOutputChannels) = 0;
  virtual ASIOError getLatencies(long* inputLatency, long* outputLatency) = 0;
  virtual ASIOError getBufferSize(long* minSize, long* maxSize, long* preferredSize,
                                  long* granularity) = 0;
  virtual ASIOError canSampleRate(ASIOSampleRate sampleRate) = 0;
  virtual ASIOError getSampleRate(ASIOSampleRate* sampleRate) = 0;
  virtual ASIOError setSampleRate(ASIOSampleRate sampleRate) = 0;
  virtual ASIOError getClockSources(ASIOClockSource* clocks, long* numSources) = 0;
  virtual ASIOError setClockSource(long reference) = 0;
  virtual ASIOError getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) = 0;
  virtual ASIOError getChannelInfo(ASIOChannelInfo* info) = 0;
  virtual ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize,
                                  ASIOCallbacks* callbacks) = 0;
  virtual ASIOError disposeBuffers() = 0;
  virtual ASIOError controlPanel() = 0;
  virtual ASIOError future(long selector, void* opt) = 0;
  virtual ASIOError outputReady() = 0;
};

struct Driver {
  std::wstring name;
  std::wstring clsid;
  std::wstring dllPath;
};

std::vector<Driver> EnumerateDrivers();

// Real-time ASIO stream. The process callback receives de-interleaved float
// buffers already converted from whatever sample type the driver uses.
class Engine {
public:
  using ProcessFn = std::function<void(const float* const* ins, float* const* outs, int frames)>;

  ~Engine();

  bool open(const std::wstring& clsid, HWND messageWindow, int preferredRate,
            int preferredBuffer, std::wstring* error);
  void close();
  bool start();
  void stop();

  void setProcess(ProcessFn fn) { process_ = std::move(fn); }

  bool opened() const { return driver_ != nullptr && buffersCreated_; }
  int inputChannels() const { return inChannels_; }
  int outputChannels() const { return outChannels_; }
  int sampleRate() const { return sampleRate_; }
  int bufferSize() const { return bufferSize_; }
  double inputLatencyMs() const { return inputLatencyMs_; }
  double outputLatencyMs() const { return outputLatencyMs_; }
  const std::wstring& driverName() const { return driverName_; }
  bool resetRequested() const { return resetRequested_; }
  void clearResetRequest() { resetRequested_ = false; }
  HRESULT showControlPanel();

  // Called from the driver's real-time thread.
  void onBufferDirect(long index) { onBuffer(index); }
  void requestReset() { resetRequested_ = true; }
  void refreshLatencies();

  static const char* errorText(ASIOError e);

private:
  void onBuffer(long index);
  ASIOError createBuffersInternal();

  IASIO* driver_ = nullptr;
  HMODULE dll_ = nullptr;
  ASIOCallbacks callbacks_{};
  ASIOBufferInfo* bufferInfos_ = nullptr;
  long numBufferInfos_ = 0;
  bool buffersCreated_ = false;
  bool started_ = false;

  int inChannels_ = 0, outChannels_ = 0;
  int sampleRate_ = 48000;
  int bufferSize_ = 128;
  double inputLatencyMs_ = 0, outputLatencyMs_ = 0;
  int inputLatencySamples_ = 0, outputLatencySamples_ = 0;

  // Conversion scratch, allocated at open time.
  std::vector<std::vector<float>> inFloat_;
  std::vector<std::vector<float>> outFloat_;
  std::vector<int> inType_, outType_;
  std::vector<const float*> inPtrs_;
  std::vector<float*> outPtrs_;

  std::wstring driverName_;
  ProcessFn process_;
  volatile bool resetRequested_ = false;
  volatile long pendingReset_ = 0;
  HWND window_ = nullptr;
};

} // namespace asio
