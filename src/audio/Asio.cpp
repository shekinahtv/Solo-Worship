#include "audio/Asio.h"

#include <objbase.h>
#include <combaseapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace asio {

// Sample type codes from the ASIO specification (little-endian group).
enum {
  ASIOSTInt16LSB = 16,
  ASIOSTInt24LSB = 17,
  ASIOSTInt32LSB = 18,
  ASIOSTFloat32LSB = 19,
  ASIOSTFloat64LSB = 20
};

static Engine* g_active = nullptr;

std::vector<Driver> EnumerateDrivers() {
  std::vector<Driver> out;
  HKEY root = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_READ | KEY_WOW64_64KEY, &root) !=
      ERROR_SUCCESS)
    return out;

  DWORD index = 0;
  wchar_t name[256];
  while (true) {
    DWORD nameLen = 256;
    FILETIME ft;
    LSTATUS st = RegEnumKeyExW(root, index++, name, &nameLen, nullptr, nullptr, nullptr, &ft);
    if (st != ERROR_SUCCESS) break;
    HKEY sub = nullptr;
    if (RegOpenKeyExW(root, name, 0, KEY_READ | KEY_WOW64_64KEY, &sub) != ERROR_SUCCESS) continue;
    wchar_t clsid[128] = {};
    DWORD size = sizeof(clsid);
    DWORD type = 0;
    if (RegQueryValueExW(sub, L"CLSID", nullptr, &type, (LPBYTE)clsid, &size) == ERROR_SUCCESS) {
      Driver d;
      d.name = name;
      d.clsid = clsid;
      // Resolve the DLL for diagnostics / manual loading fallback.
      HKEY clsidKey = nullptr;
      std::wstring path = L"CLSID\\" + d.clsid + L"\\InprocServer32";
      if (RegOpenKeyExW(HKEY_CLASSES_ROOT, path.c_str(), 0, KEY_READ, &clsidKey) ==
          ERROR_SUCCESS) {
        wchar_t dll[MAX_PATH] = {};
        DWORD dllSize = sizeof(dll);
        if (RegQueryValueExW(clsidKey, nullptr, nullptr, &type, (LPBYTE)dll, &dllSize) ==
            ERROR_SUCCESS) {
          d.dllPath = dll;
          // Strip an 8.3-style short path expansion if present.
          wchar_t expanded[MAX_PATH * 2] = {};
          DWORD n = ExpandEnvironmentStringsW(dll, expanded, MAX_PATH * 2);
          if (n > 0 && n < MAX_PATH * 2) d.dllPath = expanded;
        }
        RegCloseKey(clsidKey);
      }
      out.push_back(d);
    }
    RegCloseKey(sub);
  }
  RegCloseKey(root);
  return out;
}

const char* Engine::errorText(ASIOError e) {
  switch (e) {
  case ASE_OK: return "OK";
  case ASE_SUCCESS: return "success";
  case ASE_NotPresent: return "driver not present";
  case ASE_HWMalfunction: return "hardware malfunction";
  case ASE_InvalidParameter: return "invalid parameter";
  case ASE_InvalidMode: return "invalid mode";
  case ASE_SPNotAdvancing: return "sample position not advancing";
  case ASE_NoClock: return "no clock";
  case ASE_NoMemory: return "out of memory";
  default: return "unknown ASIO error";
  }
}

Engine::~Engine() { close(); }

// ---------------------------------------------------------------------------
// Driver instantiation.
//
// ASIO drivers are COM in-process servers, but many installs (the Focusrite
// one included) register only the ASIO key, not HKCR\CLSID. So we load the DLL
// directly and drive DllGetClassObject ourselves, falling back to a scan of the
// usual ASIO folders.
// ---------------------------------------------------------------------------
static std::wstring readDefaultString(HKEY root, const std::wstring& sub) {
  HKEY h = nullptr;
  std::wstring out;
  if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &h) == ERROR_SUCCESS) {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    if (RegQueryValueExW(h, nullptr, nullptr, &type, (LPBYTE)buf, &size) == ERROR_SUCCESS &&
        type == REG_SZ) {
      wchar_t expanded[MAX_PATH * 2] = {};
      DWORD n = ExpandEnvironmentStringsW(buf, expanded, MAX_PATH * 2);
      out = (n > 0 && n < MAX_PATH * 2) ? expanded : buf;
    }
    RegCloseKey(h);
  }
  return out;
}

static IASIO* createFromDll(const std::wstring& path, const CLSID& clsid, HMODULE* keepLoaded) {
  HMODULE h = LoadLibraryW(path.c_str());
  if (!h) return nullptr;
  using GetClassFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
  auto getClass = (GetClassFn)GetProcAddress(h, "DllGetClassObject");
  if (!getClass) {
    FreeLibrary(h);
    return nullptr;
  }
  IClassFactory* factory = nullptr;
  if (FAILED(getClass(clsid, IID_IClassFactory, (void**)&factory)) || !factory) {
    FreeLibrary(h);
    return nullptr;
  }
  void* obj = nullptr;
  HRESULT hr = factory->CreateInstance(nullptr, clsid, &obj);
  factory->Release();
  if (FAILED(hr) || !obj) {
    FreeLibrary(h);
    return nullptr;
  }
  *keepLoaded = h;
  return (IASIO*)obj;
}

static void collectDlls(const std::wstring& dir, std::vector<std::wstring>& out) {
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW((dir + L"\\*.dll").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    out.push_back(dir + L"\\" + fd.cFileName);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

static IASIO* instantiateDriver(const std::wstring& clsidText, HMODULE* keepLoaded) {
  CLSID clsid;
  if (FAILED(CLSIDFromString(clsidText.c_str(), &clsid))) return nullptr;

  IASIO* obj = nullptr;
  if (SUCCEEDED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, (void**)&obj)) && obj)
    return obj;

  std::vector<std::wstring> candidates;
  const std::wstring reg = readDefaultString(HKEY_CLASSES_ROOT, L"CLSID\\" + clsidText + L"\\InprocServer32");
  if (!reg.empty()) candidates.push_back(reg);
  const std::wstring reg32 = readDefaultString(
      HKEY_CLASSES_ROOT, L"WOW6432Node\\CLSID\\" + clsidText + L"\\InprocServer32");
  if (!reg32.empty()) candidates.push_back(reg32);

  wchar_t pf[MAX_PATH] = {};
  DWORD n = GetEnvironmentVariableW(L"ProgramW6432", pf, MAX_PATH);
  const std::wstring progFiles = n > 0 ? std::wstring(pf) : std::wstring(L"C:\\Program Files");
  collectDlls(progFiles + L"\\Focusrite\\Drivers", candidates);
  collectDlls(progFiles + L"\\Common Files\\ASIO", candidates);
  collectDlls(progFiles + L"\\ASIO", candidates);
  collectDlls(progFiles + L"\\Steinberg\\ASIO", candidates);

  for (const auto& path : candidates) {
    if (path.empty()) continue;
    obj = createFromDll(path, clsid, keepLoaded);
    if (obj) return obj;
  }
  return nullptr;
}

static float sampleToFloat(const void* base, int index, int type) {
  switch (type) {
  case ASIOSTFloat32LSB:
    return ((const float*)base)[index];
  case ASIOSTFloat64LSB:
    return (float)((const double*)base)[index];
  case ASIOSTInt32LSB:
    return (float)(((const int*)base)[index] / 2147483648.0);
  case ASIOSTInt16LSB:
    return ((const short*)base)[index] / 32768.0f;
  case ASIOSTInt24LSB: {
    const unsigned char* p = (const unsigned char*)base + (size_t)index * 3;
    int v = (int)((unsigned)p[0] << 8 | (unsigned)p[1] << 16 | (unsigned)p[2] << 24);
    return (float)(v >> 8) / 8388608.0f;
  }
  default:
    return 0.0f;
  }
}

static void floatToSample(void* base, int index, int type, float v) {
  v = std::clamp(v, -1.0f, 1.0f);
  switch (type) {
  case ASIOSTFloat32LSB:
    ((float*)base)[index] = v;
    break;
  case ASIOSTFloat64LSB:
    ((double*)base)[index] = v;
    break;
  case ASIOSTInt32LSB:
    ((int*)base)[index] = (int)std::llround((double)v * 2147483647.0);
    break;
  case ASIOSTInt16LSB:
    ((short*)base)[index] = (short)std::lround(v * 32767.0f);
    break;
  case ASIOSTInt24LSB: {
    unsigned char* p = (unsigned char*)base + (size_t)index * 3;
    int s = (int)std::lround((double)v * 8388607.0);
    p[0] = (unsigned char)(s & 0xFF);
    p[1] = (unsigned char)((s >> 8) & 0xFF);
    p[2] = (unsigned char)((s >> 16) & 0xFF);
    break;
  }
  default:
    break;
  }
}

void Engine::onBuffer(long index) {
  if (!process_ || !buffersCreated_ || index < 0 || index > 1) return;
  static thread_local bool prioritySet = false;
  if (!prioritySet) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    prioritySet = true;
  }

  for (long i = 0; i < numBufferInfos_; ++i) {
    const ASIOBufferInfo& bi = bufferInfos_[i];
    if (!bi.buffers[index]) continue;
    if (bi.isInput) {
      std::vector<float>& dst = inFloat_[(size_t)i];
      const int type = inType_[(size_t)i];
      for (int f = 0; f < bufferSize_; ++f) dst[(size_t)f] = sampleToFloat(bi.buffers[index], f, type);
    }
  }

  process_(inPtrs_.data(), outPtrs_.data(), bufferSize_);

  for (long i = 0; i < numBufferInfos_; ++i) {
    const ASIOBufferInfo& bi = bufferInfos_[i];
    if (!bi.buffers[index] || bi.isInput) continue;
    const std::vector<float>& src = outFloat_[(size_t)i];
    const int type = outType_[(size_t)i];
    for (int f = 0; f < bufferSize_; ++f) floatToSample(bi.buffers[index], f, type, src[(size_t)f]);
  }
}

static void CB_bufferSwitch(long idx, ASIOBool) {
  if (g_active) g_active->onBufferDirect(idx);
}
static void CB_sampleRate(ASIOSampleRate) {}
static long CB_asioMessage(long selector, long value, void*, double*) {
  switch (selector) {
  case kAsioSelectorSupported:
    switch (value) {
    case kAsioEngineVersion:
    case kAsioResetRequest:
    case kAsioBufferSizeChange:
    case kAsioResyncRequest:
    case kAsioLatenciesChanged:
    case kAsioSupportsTimeInfo:
    case kAsioOverload:
      return 1;
    default:
      return 0;
    }
  case kAsioEngineVersion:
    return 2;
  case kAsioResetRequest:
  case kAsioBufferSizeChange:
    if (g_active) g_active->requestReset();
    return 1;
  case kAsioResyncRequest:
    return 1;
  case kAsioLatenciesChanged:
    if (g_active) g_active->refreshLatencies();
    return 1;
  case kAsioSupportsTimeInfo:
    return 0;
  default:
    return 0;
  }
}
static ASIOTime* CB_bufferSwitchTimeInfo(ASIOTime*, long idx, ASIOBool) {
  if (g_active) g_active->onBufferDirect(idx);
  return nullptr;
}

bool Engine::open(const std::wstring& clsidText, HWND messageWindow, int preferredRate,
                  int preferredBuffer, std::wstring* error) {
  close();
  CLSID clsid;
  if (FAILED(CLSIDFromString(clsidText.c_str(), &clsid))) {
    if (error) *error = L"Invalid ASIO CLSID: " + clsidText;
    return false;
  }
  auto fail = [&](const std::wstring& msg) {
    if (error) *error = msg;
    close();
    return false;
  };

  (void)clsid;
  driver_ = instantiateDriver(clsidText, &dll_);
  if (!driver_) return fail(L"Cannot load the ASIO driver (CLSID " + clsidText + L")");

  window_ = messageWindow;
  if (!driver_->init(messageWindow)) return fail(L"ASIO init() refused");

  char nameBuf[64] = {};
  driver_->getDriverName(nameBuf);
  driverName_.assign(nameBuf, nameBuf + strnlen(nameBuf, 63));

  long nIn = 0, nOut = 0;
  if (driver_->getChannels(&nIn, &nOut) != ASE_OK || nIn <= 0)
    return fail(L"The ASIO driver reports no usable inputs");
  inChannels_ = (int)nIn;
  outChannels_ = (int)nOut;
  if (outChannels_ <= 0) return fail(L"The ASIO driver reports no outputs");

  long minSize = 0, maxSize = 0, preferred = 0, granularity = 0;
  if (driver_->getBufferSize(&minSize, &maxSize, &preferred, &granularity) != ASE_OK)
    return fail(L"getBufferSize failed");

  long want = preferredBuffer > 0 ? preferredBuffer : preferred;
  if (granularity > 1) want = (long)std::lround((double)want / granularity) * granularity;
  want = std::clamp(want, minSize, maxSize);
  bufferSize_ = (int)want;

  ASIOSampleRate rate = (ASIOSampleRate)preferredRate;
  if (driver_->canSampleRate(rate) != ASE_OK) {
    // Try the usual suspects before giving up.
    bool found = false;
    for (ASIOSampleRate r : {48000.0, 44100.0, 96000.0, 88200.0}) {
      if (driver_->canSampleRate(r) == ASE_OK) {
        rate = r;
        found = true;
        break;
      }
    }
    if (!found) return fail(L"No supported sample rate");
  }
  if (driver_->setSampleRate(rate) != ASE_OK) return fail(L"setSampleRate failed");
  ASIOSampleRate actual = 0;
  if (driver_->getSampleRate(&actual) == ASE_OK && actual > 0) rate = actual;
  sampleRate_ = (int)std::llround(rate);

  callbacks_.bufferSwitch = &CB_bufferSwitch;
  callbacks_.sampleRateDidChange = &CB_sampleRate;
  callbacks_.asioMessage = &CB_asioMessage;
  callbacks_.bufferSwitchTimeInfo = &CB_bufferSwitchTimeInfo;

  g_active = this;
  if (createBuffersInternal() != ASE_OK) return fail(L"createBuffers failed");

  long inLat = 0, outLat = 0;
  if (driver_->getLatencies(&inLat, &outLat) == ASE_OK) {
    inputLatencySamples_ = (int)inLat;
    outputLatencySamples_ = (int)outLat;
    inputLatencyMs_ = 1000.0 * inLat / (double)sampleRate_;
    outputLatencyMs_ = 1000.0 * outLat / (double)sampleRate_;
  }
  return true;
}

ASIOError Engine::createBuffersInternal() {
  if (!driver_) return ASE_NotPresent;
  inputLatencySamples_ = 0;
  numBufferInfos_ = inChannels_ + outChannels_;
  bufferInfos_ = new ASIOBufferInfo[(size_t)numBufferInfos_];
  inFloat_.assign((size_t)numBufferInfos_, std::vector<float>());
  outFloat_.assign((size_t)numBufferInfos_, std::vector<float>());
  inType_.assign((size_t)numBufferInfos_, ASIOSTFloat32LSB);
  outType_.assign((size_t)numBufferInfos_, ASIOSTFloat32LSB);
  inPtrs_.assign((size_t)inChannels_, nullptr);
  outPtrs_.assign((size_t)outChannels_, nullptr);

  for (int i = 0; i < inChannels_; ++i) {
    ASIOBufferInfo& bi = bufferInfos_[i];
    bi.isInput = 1;
    bi.channelNum = i;
    bi.buffers[0] = bi.buffers[1] = nullptr;
  }
  for (int i = 0; i < outChannels_; ++i) {
    ASIOBufferInfo& bi = bufferInfos_[inChannels_ + i];
    bi.isInput = 0;
    bi.channelNum = i;
    bi.buffers[0] = bi.buffers[1] = nullptr;
  }

  ASIOError err =
      driver_->createBuffers(bufferInfos_, numBufferInfos_, bufferSize_, &callbacks_);
  if (err != ASE_OK) return err;

  buffersCreated_ = true;
  for (long i = 0; i < numBufferInfos_; ++i) {
    ASIOChannelInfo info = {};
    info.channel = bufferInfos_[i].channelNum;
    info.isInput = bufferInfos_[i].isInput;
    if (driver_->getChannelInfo(&info) == ASE_OK) {
      if (info.isInput)
        inType_[(size_t)i] = info.type;
      else
        outType_[(size_t)i] = info.type;
    }
    inFloat_[(size_t)i].assign((size_t)bufferSize_, 0.0f);
    outFloat_[(size_t)i].assign((size_t)bufferSize_, 0.0f);
  }
  // Point the interleaved helpers at the per-channel float scratch.
  for (int c = 0; c < inChannels_; ++c) inPtrs_[(size_t)c] = inFloat_[(size_t)c].data();
  for (int c = 0; c < outChannels_; ++c) outPtrs_[(size_t)c] = outFloat_[(size_t)inChannels_ + c].data();
  return ASE_OK;
}

bool Engine::start() {
  if (!driver_ || !buffersCreated_) return false;
  ASIOError e = driver_->start();
  started_ = (e == ASE_OK);
  return started_;
}

void Engine::stop() {
  if (driver_ && started_) {
    driver_->stop();
    started_ = false;
  }
}

void Engine::refreshLatencies() {
  if (!driver_) return;
  long inLat = 0, outLat = 0;
  if (driver_->getLatencies(&inLat, &outLat) == ASE_OK) {
    inputLatencySamples_ = (int)inLat;
    outputLatencySamples_ = (int)outLat;
    inputLatencyMs_ = 1000.0 * inLat / (double)std::max(1, sampleRate_);
    outputLatencyMs_ = 1000.0 * outLat / (double)std::max(1, sampleRate_);
  }
}

HRESULT Engine::showControlPanel() {
  if (!driver_) return E_FAIL;
  driver_->controlPanel();
  return S_OK;
}

void Engine::close() {
  if (g_active == this) g_active = nullptr;
  if (driver_) {
    if (started_) driver_->stop();
    if (buffersCreated_) driver_->disposeBuffers();
    driver_->Release();
    driver_ = nullptr;
  }
  if (dll_) {
    FreeLibrary(dll_);
    dll_ = nullptr;
  }
  buffersCreated_ = false;
  started_ = false;
  delete[] bufferInfos_;
  bufferInfos_ = nullptr;
  numBufferInfos_ = 0;
  inFloat_.clear();
  outFloat_.clear();
  inPtrs_.clear();
  outPtrs_.clear();
}

} // namespace asio
