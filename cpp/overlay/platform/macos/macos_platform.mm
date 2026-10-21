// macOS backend.
//
// Capture: ScreenCaptureKit (macOS 12.3+). One SCStream per display whose
// SCContentFilter excludes every overlay window, so the stream never sees
// our own output. Frames arrive as 32BGRA CVPixelBuffers on a private queue;
// grab() copies the newest one. Needs the Screen Recording permission.
// Overlay: NSWindow at NSScreenSaverWindowLevel, ignoresMouseEvents (click
// through), on all Spaces and over fullscreen apps, sharingType = None.
// Hotkeys: Carbon RegisterEventHotKey, which needs no Accessibility or Input
// Monitoring permission.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "platform/platform.hpp"

namespace delss::overlay {
struct SharedFrame {
  std::mutex mutex;
  CapturedFrame frame;
  bool fresh = false;
};
}  // namespace delss::overlay

API_AVAILABLE(macos(12.3))
@interface DelssStreamOutput : NSObject <SCStreamOutput, SCStreamDelegate>
@property(nonatomic) delss::overlay::SharedFrame* shared;
@end

@implementation DelssStreamOutput
- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   ofType:(SCStreamOutputType)type {
  if (type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sampleBuffer)) return;
  // Skip status-only buffers (idle/blank frames carry no new pixels).
  CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
  if (attachments != nullptr && CFArrayGetCount(attachments) > 0) {
    auto* dict = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
    auto* status =
        static_cast<CFNumberRef>(CFDictionaryGetValue(dict, (__bridge CFStringRef)SCStreamFrameInfoStatus));
    int value = 0;
    if (status != nullptr && CFNumberGetValue(status, kCFNumberIntType, &value) &&
        value != SCFrameStatusComplete) {
      return;
    }
  }
  CVPixelBufferRef pixels = CMSampleBufferGetImageBuffer(sampleBuffer);
  if (pixels == nullptr) return;
  CVPixelBufferLockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
  const auto width = static_cast<int>(CVPixelBufferGetWidth(pixels));
  const auto height = static_cast<int>(CVPixelBufferGetHeight(pixels));
  const auto stride = static_cast<int>(CVPixelBufferGetBytesPerRow(pixels));
  const auto* base = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddress(pixels));
  {
    std::lock_guard<std::mutex> lock(self.shared->mutex);
    auto& f = self.shared->frame;
    f.width = width;
    f.height = height;
    f.stride = stride;
    f.format = delss::overlay::PixelFormat::BGRA8;
    f.topDown = true;
    f.pixels.assign(base, base + static_cast<size_t>(stride) * static_cast<size_t>(height));
    ++f.serial;
    self.shared->fresh = true;
  }
  CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
}

- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
  NSLog(@"delss-overlay: capture stopped: %@", error);
}
@end

namespace delss::overlay {
namespace {

class API_AVAILABLE(macos(12.3)) SckCapture final : public ScreenCapture {
 public:
  SckCapture(const MonitorInfo& m, const ExcludedWindows& excluded) {
    const CGDirectDisplayID displayId = glfwGetCocoaMonitor(m.monitor);
    std::vector<CGWindowID> ours;
    for (GLFWwindow* w : excluded())
      ours.push_back(static_cast<CGWindowID>([glfwGetCocoaWindow(w) windowNumber]));

    __block SCShareableContent* content = nil;
    __block NSError* error = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                               onScreenWindowsOnly:NO
                                                 completionHandler:^(SCShareableContent* c, NSError* e) {
                                                   content = c;
                                                   error = e;
                                                   dispatch_semaphore_signal(done);
                                                 }];
    dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
    if (content == nil) {
      throw std::runtime_error(
          "ScreenCaptureKit refused (grant Screen Recording to delss-overlay in System Settings > Privacy & "
          "Security, then restart it): " +
          std::string(error != nil ? error.localizedDescription.UTF8String : "timeout"));
    }
    SCDisplay* display = nil;
    for (SCDisplay* d in content.displays) {
      if (d.displayID == displayId) display = d;
    }
    if (display == nil) throw std::runtime_error("ScreenCaptureKit: display not found");
    NSMutableArray<SCWindow*>* exclude = [NSMutableArray array];
    for (SCWindow* w in content.windows) {
      for (const CGWindowID id : ours) {
        if (w.windowID == id) [exclude addObject:w];
      }
    }
    excludedCount_ = static_cast<int>(exclude.count);
    SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:exclude];

    SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
    const CGFloat scale = [NSScreen mainScreen].backingScaleFactor;
    for (NSScreen* s in NSScreen.screens) {
      NSNumber* n = s.deviceDescription[@"NSScreenNumber"];
      if (n.unsignedIntValue == displayId)
        config.width = static_cast<size_t>(s.frame.size.width * s.backingScaleFactor);
      if (n.unsignedIntValue == displayId)
        config.height = static_cast<size_t>(s.frame.size.height * s.backingScaleFactor);
    }
    if (config.width == 0) {
      config.width = static_cast<size_t>(static_cast<CGFloat>(display.width) * scale);
      config.height = static_cast<size_t>(static_cast<CGFloat>(display.height) * scale);
    }
    config.pixelFormat = kCVPixelFormatType_32BGRA;
    config.minimumFrameInterval = CMTimeMake(1, 60);
    config.queueDepth = 3;
    config.showsCursor = YES;

    output_ = [[DelssStreamOutput alloc] init];
    output_.shared = &shared_;
    stream_ = [[SCStream alloc] initWithFilter:filter configuration:config delegate:output_];
    queue_ = dispatch_queue_create("delss.capture", DISPATCH_QUEUE_SERIAL);
    NSError* addError = nil;
    if (![stream_ addStreamOutput:output_
                             type:SCStreamOutputTypeScreen
               sampleHandlerQueue:queue_
                            error:&addError]) {
      throw std::runtime_error("ScreenCaptureKit: addStreamOutput failed");
    }
    dispatch_semaphore_t started = dispatch_semaphore_create(0);
    __block NSError* startError = nil;
    [stream_ startCaptureWithCompletionHandler:^(NSError* e) {
      startError = e;
      dispatch_semaphore_signal(started);
    }];
    dispatch_semaphore_wait(started, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
    if (startError != nil) {
      throw std::runtime_error(std::string("ScreenCaptureKit: start failed: ") +
                               startError.localizedDescription.UTF8String);
    }
  }

  ~SckCapture() override {
    dispatch_semaphore_t stopped = dispatch_semaphore_create(0);
    [stream_ stopCaptureWithCompletionHandler:^(NSError*) {
      dispatch_semaphore_signal(stopped);
    }];
    dispatch_semaphore_wait(stopped, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));
  }
  SckCapture(const SckCapture&) = delete;
  SckCapture& operator=(const SckCapture&) = delete;
  SckCapture(SckCapture&&) = delete;
  SckCapture& operator=(SckCapture&&) = delete;

  bool grab(CapturedFrame& f) override {
    std::lock_guard<std::mutex> lock(shared_.mutex);
    if (shared_.fresh) {
      f = shared_.frame;  // copy out; the capture queue keeps writing its own buffer
      shared_.fresh = false;
    }
    // ScreenCaptureKit only delivers frames when the screen changes, so "no new
    // frame" is success as long as one ever arrived.
    return !f.pixels.empty();
  }

  [[nodiscard]] std::string describe() const override {
    return "ScreenCaptureKit (" + std::to_string(excludedCount_) + " overlay windows excluded)";
  }

 private:
  SharedFrame shared_;
  DelssStreamOutput* output_ = nil;
  SCStream* stream_ = nil;
  dispatch_queue_t queue_ = nil;
  int excludedCount_ = 0;
};

std::vector<Command>* gHotkeyQueue = nullptr;

OSStatus onHotKey(EventHandlerCallRef /*next*/, EventRef event, void* /*user*/) {
  EventHotKeyID id{};
  GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof id, nullptr, &id);
  if (gHotkeyQueue != nullptr) gHotkeyQueue->push_back({static_cast<Action>(id.id)});
  return noErr;
}

UInt32 keyCode(Key k) {
  switch (k) {
    case Key::D:
      return kVK_ANSI_D;
    case Key::P:
      return kVK_ANSI_P;
    case Key::O:
      return kVK_ANSI_O;
    case Key::H:
      return kVK_ANSI_H;
    case Key::Q:
      return kVK_ANSI_Q;
    case Key::Up:
      return kVK_UpArrow;
    case Key::Down:
      return kVK_DownArrow;
    case Key::Escape:
      return kVK_Escape;
  }
  return 0;
}

class MacPlatform final : public Platform {
 public:
  MacPlatform() = default;
  ~MacPlatform() override {
    for (EventHotKeyRef ref : hotkeys_) UnregisterEventHotKey(ref);
    gHotkeyQueue = nullptr;
  }
  MacPlatform(const MacPlatform&) = delete;
  MacPlatform& operator=(const MacPlatform&) = delete;
  MacPlatform(MacPlatform&&) = delete;
  MacPlatform& operator=(MacPlatform&&) = delete;

  void initHints() override {
    glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);
    // Register with TCC on first launch so the app shows up in the Screen
    // Recording list (and the system prompt appears) before the user enables it.
    if (!CGPreflightScreenCaptureAccess()) CGRequestScreenCaptureAccess();
  }

  void windowHints() override {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE);
    ensureHotkeys();
  }

  void prepareWindow(GLFWwindow* window, const MonitorInfo& m) override {
    NSWindow* w = glfwGetCocoaWindow(window);
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];  // no Dock icon
    w.level = NSScreenSaverWindowLevel;
    w.ignoresMouseEvents = YES;
    w.hasShadow = NO;
    w.sharingType = NSWindowSharingNone;
    w.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary |
                           NSWindowCollectionBehaviorFullScreenAuxiliary |
                           NSWindowCollectionBehaviorIgnoresCycle;
    const CGDirectDisplayID id = glfwGetCocoaMonitor(m.monitor);
    for (NSScreen* s in NSScreen.screens) {
      NSNumber* n = s.deviceDescription[@"NSScreenNumber"];
      if (n.unsignedIntValue == id) [w setFrame:s.frame display:NO];  // full screen incl. menu bar area
    }
  }

  std::unique_ptr<ScreenCapture> createCapture(const MonitorInfo& m, ExcludedWindows excluded) override {
    // Ask up front: triggers the system prompt once, and gives a clear error
    // instead of an opaque ScreenCaptureKit failure.
    if (!CGPreflightScreenCaptureAccess()) {
      CGRequestScreenCaptureAccess();
      throw std::runtime_error(
          "DeLSS Overlay needs Screen Recording permission. Enable it in System Settings > Privacy & "
          "Security > Screen Recording, then press Ctrl+Option+D (or relaunch).");
    }
    if (@available(macOS 12.3, *)) return std::make_unique<SckCapture>(m, excluded);
    throw std::runtime_error("delss-overlay needs macOS 12.3 or newer (ScreenCaptureKit)");
  }

  void pollHotkeys(std::vector<Command>& out) override {
    ensureHotkeys();
    out.insert(out.end(), queue_.begin(), queue_.end());
    queue_.clear();
  }

  [[nodiscard]] std::vector<std::string> warnings() const override { return warnings_; }
  [[nodiscard]] std::string name() const override { return "macOS"; }

  void reportError(const std::string& message) override {
    Platform::reportError(message);
    // Launched from Finder there is no terminal: show the reason.
    NSAlert* alert = [[NSAlert alloc] init];
    const bool permission = message.find("Screen Recording") != std::string::npos;
    alert.messageText = permission ? @"DeLSS Overlay needs Screen Recording permission" : @"DeLSS Overlay";
    alert.informativeText = [NSString stringWithUTF8String:message.c_str()];
    if (permission) {
      [alert addButtonWithTitle:@"Open Privacy Settings"];
      [alert addButtonWithTitle:@"Later"];
    }
    [NSApp activateIgnoringOtherApps:YES];
    if ([alert runModal] == NSAlertFirstButtonReturn && permission) {
      [[NSWorkspace sharedWorkspace]
          openURL:[NSURL
                      URLWithString:
                          @"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"]];
    }
  }

 private:
  void ensureHotkeys() {
    if (registered_) return;
    registered_ = true;
    gHotkeyQueue = &queue_;
    const EventTypeSpec spec{kEventClassKeyboard, kEventHotKeyPressed};
    InstallApplicationEventHandler(&onHotKey, 1, &spec, nullptr, nullptr);
    for (const Binding& b : defaultBindings()) {
      UInt32 mods = 0;
      if ((b.chord.mods & kCtrl) != 0) mods |= controlKey;
      if ((b.chord.mods & kAlt) != 0) mods |= optionKey;
      if ((b.chord.mods & kShift) != 0) mods |= shiftKey;
      if ((b.chord.mods & kSuper) != 0) mods |= cmdKey;
      EventHotKeyRef ref = nullptr;
      const EventHotKeyID id{'DLSS', static_cast<UInt32>(b.action)};
      if (RegisterEventHotKey(keyCode(b.chord.key), mods, id, GetApplicationEventTarget(), 0, &ref) ==
          noErr) {
        hotkeys_.push_back(ref);
      } else {
        warnings_.push_back(describe(b.chord) + " (Ctrl+Option on a Mac) is taken by another app");
      }
    }
  }

  bool registered_ = false;
  std::vector<Command> queue_;
  std::vector<EventHotKeyRef> hotkeys_;
  std::vector<std::string> warnings_;
};

}  // namespace

std::unique_ptr<Platform> createPlatform() { return std::make_unique<MacPlatform>(); }

}  // namespace delss::overlay
