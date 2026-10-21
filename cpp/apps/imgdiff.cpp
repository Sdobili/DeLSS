// delss-imgdiff A.png B.png [--different]
// Compare two images with the golden-test tolerance. Exit 0 when they match
// (or, with --different, when they clearly do not). Used by the overlay e2e
// test to check screen captures and overlay output against the goldens.
#include <cstdio>
#include <exception>
#include <string>

#include "headless.hpp"

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fputs("usage: delss-imgdiff A.png B.png [--different]\n", stderr);
    return 2;
  }
  try {
    const bool wantDifferent = argc > 3 && std::string(argv[3]) == "--different";
    const auto a = delss::headless::loadPng(argv[1]);
    const auto b = delss::headless::loadPng(argv[2]);
    const auto d = delss::headless::compare(a, b);
    const bool same = delss::headless::passes(d);
    std::printf("%s vs %s: mean %.3f  >16 %.3f%%  max %d -> %s\n", argv[1], argv[2], d.mean,
                d.badRatio * 100.0, d.max, same ? "match" : "differ");
    return same != wantDifferent ? 0 : 1;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "delss-imgdiff: %s\n", e.what());
    return 2;
  }
}
