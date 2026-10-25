// epsilon-common — small OS helpers for plugin UIs: open a web page, a monotonic clock, and typed characters.
// On Windows, link with -lshell32 (for ShellExecuteA). License: MIT.
#pragma once
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
extern "C" __declspec(dllimport) void* __stdcall ShellExecuteA(void*, const char*, const char*, const char*, const char*, int);
#endif

namespace epsilon {

// opens url in the default browser without blocking the UI
inline void openURL(const char* url) {
    if (!url || !*url) return;
    for (const char* p = url; *p; ++p) if (*p == '\'' || *p == '"') return;   // never pass quotes to a shell
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, 1);
#elif defined(__APPLE__)
    const std::string c = std::string("open '") + url + "' &"; if (std::system(c.c_str())) {}
#else
    const std::string c = std::string("xdg-open '") + url + "' >/dev/null 2>&1 &"; if (std::system(c.c_str())) {}
#endif
}

// seconds on a steady clock (for hover-tip delays and status flashes)
inline double nowSec() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// DPF key events report the unshifted key: apply Shift to letters and the US-layout symbols.
// Returns 0 for keys that aren't printable.
inline char typedChar(unsigned key, bool shift) {
    if (key < 32 || key > 126) return 0;
    char c = (char)key;
    if (!shift) return c;
    if (c >= 'a' && c <= 'z') return (char)std::toupper((unsigned char)c);
    static const char from[] = "1234567890-=[]\\;',./`", to[] = "!@#$%^&*()_+{}|:\"<>?~";
    for (int i = 0; from[i]; ++i) if (from[i] == c) return to[i];
    return c;
}

} // namespace epsilon
