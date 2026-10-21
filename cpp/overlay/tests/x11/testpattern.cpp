// Test helper for the X11 end-to-end test: shows an image in a plain X11
// window at a fixed position and logs every mouse click it receives, so the
// test can prove clicks pass through the overlay to the window underneath.
//   x11-testpattern IMAGE.png X Y
// Prints "ready <window id>" once mapped and "click <x> <y> <button>" per press.
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// NOLINTBEGIN
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"
// NOLINTEND

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fputs("usage: x11-testpattern IMAGE.png X Y\n", stderr);
    return 2;
  }
  int w = 0;
  int h = 0;
  int n = 0;
  unsigned char* rgba = stbi_load(argv[1], &w, &h, &n, 4);
  if (rgba == nullptr) return 1;
  Display* d = XOpenDisplay(nullptr);
  if (d == nullptr) return 1;
  const int screen = DefaultScreen(d);
  XSetWindowAttributes attrs{};
  attrs.override_redirect = True;  // exact placement, no WM involvement
  attrs.event_mask = ExposureMask | ButtonPressMask;
  attrs.background_pixel = BlackPixel(d, screen);
  const Window win =
      XCreateWindow(d, RootWindow(d, screen), std::atoi(argv[2]), std::atoi(argv[3]),
                    static_cast<unsigned>(w), static_cast<unsigned>(h), 0, CopyFromParent, InputOutput,
                    CopyFromParent, CWOverrideRedirect | CWEventMask | CWBackPixel, &attrs);
  // 24-bit TrueColor ZPixmap is BGRX in memory.
  auto* bgrx = static_cast<char*>(std::malloc(static_cast<size_t>(w) * static_cast<size_t>(h) * 4));
  for (int i = 0; i < w * h; ++i) {
    bgrx[i * 4] = static_cast<char>(rgba[i * 4 + 2]);
    bgrx[i * 4 + 1] = static_cast<char>(rgba[i * 4 + 1]);
    bgrx[i * 4 + 2] = static_cast<char>(rgba[i * 4]);
    bgrx[i * 4 + 3] = 0;
  }
  XImage* img = XCreateImage(d, DefaultVisual(d, screen), static_cast<unsigned>(DefaultDepth(d, screen)),
                             ZPixmap, 0, bgrx, static_cast<unsigned>(w), static_cast<unsigned>(h), 32, 0);
  const GC gc = XCreateGC(d, win, 0, nullptr);
  XMapWindow(d, win);
  bool announced = false;
  for (;;) {
    XEvent e{};
    XNextEvent(d, &e);
    if (e.type == Expose) {
      XPutImage(d, win, gc, img, 0, 0, 0, 0, static_cast<unsigned>(w), static_cast<unsigned>(h));
      XFlush(d);
      if (!announced) {
        std::printf("ready 0x%lx\n", win);
        std::fflush(stdout);
        announced = true;
      }
    } else if (e.type == ButtonPress) {
      std::printf("click %d %d %u\n", e.xbutton.x, e.xbutton.y, e.xbutton.button);
      std::fflush(stdout);
    }
  }
}
