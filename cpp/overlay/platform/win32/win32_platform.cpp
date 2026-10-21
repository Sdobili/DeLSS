// Windows backend.
//
// Capture: DXGI Desktop Duplication per output, copied through a staging
// texture to the CPU (simple and works with any GL driver; a zero-copy
// WGL_NV_DX_interop path is a later optimisation).
// Overlay: WS_EX_LAYERED | WS_EX_TRANSPARENT (click-through) | WS_EX_TOPMOST |
// WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, and SetWindowDisplayAffinity(
// WDA_EXCLUDEFROMCAPTURE) so Desktop Duplication never sees our own output
// (Windows 10 2004+). Needs no permissions.
// Hotkeys: GetAsyncKeyState polling through the shared ChordEdgeDetector, so
// nothing is registered system-wide and nothing can be left stuck.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <chrono>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "platform/platform.hpp"

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace delss::overlay {
namespace {

template <typename T>
void release(T*& p) {
  if (p != nullptr) {
    p->Release();
    p = nullptr;
  }
}

std::string narrow(const wchar_t* w) {
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}

class DxgiCapture final : public ScreenCapture {
 public:
  explicit DxgiCapture(const MonitorInfo& m) {
    const char* adapterName = glfwGetWin32Monitor(m.monitor);  // e.g. "\\.\DISPLAY1"
    deviceName_ = adapterName != nullptr ? adapterName : "";
    if (!open()) throw std::runtime_error("DXGI: no output named " + deviceName_);
  }
  ~DxgiCapture() override { close(); }
  DxgiCapture(const DxgiCapture&) = delete;
  DxgiCapture& operator=(const DxgiCapture&) = delete;
  DxgiCapture(DxgiCapture&&) = delete;
  DxgiCapture& operator=(DxgiCapture&&) = delete;

  bool grab(CapturedFrame& f) override {
    if (duplication_ == nullptr && !open()) return false;
    DXGI_OUTDUPL_FRAME_INFO info{};
    IDXGIResource* resource = nullptr;
    const HRESULT hr = duplication_->AcquireNextFrame(0, &info, &resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return !f.pixels.empty();  // screen unchanged
    if (hr == DXGI_ERROR_ACCESS_LOST) {  // mode change, UAC desktop, fullscreen switch
      close();
      return false;
    }
    if (FAILED(hr)) return false;
    ID3D11Texture2D* texture = nullptr;
    resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
    release(resource);
    bool ok = false;
    if (texture != nullptr) {
      D3D11_TEXTURE2D_DESC desc{};
      texture->GetDesc(&desc);
      if (ensureStaging(desc)) {
        context_->CopyResource(staging_, texture);
        D3D11_MAPPED_SUBRESOURCE map{};
        if (SUCCEEDED(context_->Map(staging_, 0, D3D11_MAP_READ, 0, &map))) {
          f.width = static_cast<int>(desc.Width);
          f.height = static_cast<int>(desc.Height);
          f.stride = f.width * 4;
          f.format = PixelFormat::BGRA8;
          f.topDown = true;
          f.pixels.resize(static_cast<size_t>(f.stride) * desc.Height);
          for (UINT y = 0; y < desc.Height; ++y) {
            std::memcpy(f.pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(f.stride),
                        static_cast<const std::uint8_t*>(map.pData) + static_cast<size_t>(y) * map.RowPitch,
                        static_cast<size_t>(f.stride));
          }
          ++f.serial;
          context_->Unmap(staging_, 0);
          ok = true;
        }
      }
      release(texture);
    }
    duplication_->ReleaseFrame();
    return ok;
  }

  [[nodiscard]] std::string describe() const override {
    return "DXGI Desktop Duplication " + deviceName_ + " (overlay excluded via WDA_EXCLUDEFROMCAPTURE)";
  }

 private:
  bool open() {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return false;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT a = 0; duplication_ == nullptr && factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND;
         ++a) {
      IDXGIOutput* output = nullptr;
      for (UINT o = 0; duplication_ == nullptr && adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND;
           ++o) {
        DXGI_OUTPUT_DESC desc{};
        output->GetDesc(&desc);
        if (narrow(desc.DeviceName) == deviceName_) {
          IDXGIOutput1* output1 = nullptr;
          output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&output1));
          if (output1 != nullptr &&
              SUCCEEDED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                          D3D11_SDK_VERSION, &device_, nullptr, &context_))) {
            if (FAILED(output1->DuplicateOutput(device_, &duplication_))) {
              release(context_);
              release(device_);
            }
          }
          release(output1);
        }
        release(output);
      }
      release(adapter);
    }
    release(factory);
    return duplication_ != nullptr;
  }

  bool ensureStaging(const D3D11_TEXTURE2D_DESC& src) {
    if (staging_ != nullptr && stagingWidth_ == src.Width && stagingHeight_ == src.Height) return true;
    release(staging_);
    D3D11_TEXTURE2D_DESC d = src;
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.SampleDesc = {1, 0};
    if (FAILED(device_->CreateTexture2D(&d, nullptr, &staging_))) return false;
    stagingWidth_ = src.Width;
    stagingHeight_ = src.Height;
    return true;
  }

  void close() {
    release(staging_);
    release(duplication_);
    release(context_);
    release(device_);
    stagingWidth_ = stagingHeight_ = 0;
  }

  std::string deviceName_;
  ID3D11Device* device_ = nullptr;
  ID3D11DeviceContext* context_ = nullptr;
  IDXGIOutputDuplication* duplication_ = nullptr;
  ID3D11Texture2D* staging_ = nullptr;
  UINT stagingWidth_ = 0;
  UINT stagingHeight_ = 0;
};

int virtualKey(Key k) {
  switch (k) {
    case Key::D:
      return 'D';
    case Key::P:
      return 'P';
    case Key::O:
      return 'O';
    case Key::H:
      return 'H';
    case Key::Q:
      return 'Q';
    case Key::Up:
      return VK_UP;
    case Key::Down:
      return VK_DOWN;
    case Key::Escape:
      return VK_ESCAPE;
  }
  return 0;
}

bool down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

class Win32Platform final : public Platform {
 public:
  void windowHints() override {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  }

  void prepareWindow(GLFWwindow* window, const MonitorInfo& m) override {
    const HWND hwnd = glfwGetWin32Window(window);
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    ex &= ~static_cast<LONG_PTR>(WS_EX_APPWINDOW);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    SetWindowPos(hwnd, HWND_TOPMOST, m.x, m.y, m.width, m.height, SWP_NOACTIVATE | SWP_FRAMECHANGED);
    if (SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE) == 0) {
      // Without exclusion Desktop Duplication would read back our own output.
      throw std::runtime_error(
          "SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed: delss-overlay needs Windows 10 2004 or "
          "newer");
    }
  }

  void keepOnTop(GLFWwindow* window) override {
    const auto now = std::chrono::steady_clock::now();
    if (now - lastRaise_ < std::chrono::milliseconds(250)) return;
    lastRaise_ = now;
    SetWindowPos(glfwGetWin32Window(window), HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }

  std::unique_ptr<ScreenCapture> createCapture(const MonitorInfo& m, ExcludedWindows /*excluded*/) override {
    // Exclusion is a property of the windows themselves (display affinity).
    return std::make_unique<DxgiCapture>(m);
  }

  void pollHotkeys(std::vector<Command>& out) override {
    std::uint8_t mods = 0;
    if (down(VK_CONTROL)) mods |= kCtrl;
    if (down(VK_MENU)) mods |= kAlt;
    if (down(VK_SHIFT)) mods |= kShift;
    if (down(VK_LWIN) || down(VK_RWIN)) mods |= kSuper;
    std::set<Key> keys;
    for (const Key k : {Key::D, Key::P, Key::O, Key::H, Key::Q, Key::Up, Key::Down, Key::Escape}) {
      if (down(virtualKey(k))) keys.insert(k);
    }
    for (const Action a : edges_.update(mods, keys)) out.push_back({a});
  }

  [[nodiscard]] std::string name() const override { return "Windows"; }

 private:
  ChordEdgeDetector edges_;
  std::chrono::steady_clock::time_point lastRaise_{};
};

}  // namespace

std::unique_ptr<Platform> createPlatform() { return std::make_unique<Win32Platform>(); }

}  // namespace delss::overlay
