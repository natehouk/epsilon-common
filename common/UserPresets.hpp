// epsilon-common — user preset files: one plain-text file per preset ("symbol=value" lines), the same format
// and folders on every OS. Missing keys keep their defaults, so presets from older versions still load.
// License: MIT.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

namespace epsilon {

// what the preset store needs to know about each parameter
struct PresetParam { const char* sym; float min, max; bool stored; };   // stored = false: outputs and view settings

class PresetStore {
public:
    // product: "Slate"; ext: ".slate"
    PresetStore(const char* product, const char* ext) : product_(product), ext_(ext) {}

    // macOS:   ~/Library/Audio/Presets/Epsilon Audio/<Product>
    // Windows: %APPDATA%\Epsilon Audio\<Product>\Presets
    // Linux:   $XDG_CONFIG_HOME (or ~/.config)/epsilon-audio/<product>/presets
    std::string dir() const {
#if defined(_WIN32)
        const char* a = std::getenv("APPDATA");
        return std::string(a ? a : ".") + "\\Epsilon Audio\\" + product_ + "\\Presets";
#elif defined(__APPLE__)
        const char* h = std::getenv("HOME");
        return std::string(h ? h : ".") + "/Library/Audio/Presets/Epsilon Audio/" + product_;
#else
        std::string lower = product_; for (char& c : lower) c = (char)std::tolower((unsigned char)c);
        const char* x = std::getenv("XDG_CONFIG_HOME"); const char* h = std::getenv("HOME");
        return (x && *x ? std::string(x) : std::string(h ? h : ".") + "/.config") + "/epsilon-audio/" + lower + "/presets";
#endif
    }

    // names (without the extension), sorted case-insensitively
    std::vector<std::string> list() const {
        std::vector<std::string> out;
        DIR* d = opendir(dir().c_str());
        if (!d) return out;
        while (dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            if (n.size() > ext_.size() && n.compare(n.size() - ext_.size(), ext_.size(), ext_) == 0) out.push_back(n.substr(0, n.size() - ext_.size()));
        }
        closedir(d);
        std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
        return out;
    }

    bool save(const std::string& rawName, const PresetParam* params, uint32_t count, const float* values) const {
        const std::string name = sanitize(rawName);
        if (name.empty()) return false;
        makeDirs(dir());
        FILE* f = std::fopen(path(name).c_str(), "wb");
        if (!f) return false;
        std::fprintf(f, "# %s preset\nformat=1\n", product_.c_str());
        for (uint32_t i = 0; i < count; ++i) if (params[i].stored) std::fprintf(f, "%s=%.9g\n", params[i].sym, values[i]);
        const bool ok = std::fclose(f) == 0;
        return ok;
    }

    // values starts as the defaults; keys in the file overwrite them (clamped to each parameter's range)
    bool load(const std::string& name, const PresetParam* params, uint32_t count, float* values) const {
        FILE* f = std::fopen(path(name).c_str(), "rb");
        if (!f) return false;
        char line[512];
        while (std::fgets(line, sizeof line, f)) {
            if (line[0] == '#') continue;
            char* eq = std::strchr(line, '=');
            if (!eq) continue;
            *eq = 0;
            const float v = (float)std::atof(eq + 1);
            for (uint32_t i = 0; i < count; ++i)
                if (params[i].stored && std::strcmp(params[i].sym, line) == 0) { values[i] = std::min(params[i].max, std::max(params[i].min, v)); break; }
        }
        std::fclose(f);
        return true;
    }

    bool remove(const std::string& name) const { return std::remove(path(name).c_str()) == 0; }

    // file-system-safe name: no path separators or reserved characters, trimmed, at most 48 characters
    static std::string sanitize(const std::string& in) {
        std::string s;
        for (char c : in) if (!std::strchr("/\\:*?\"<>|", c) && (unsigned char)c >= 32) s += c;
        while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
        while (!s.empty() && s.front() == ' ') s.erase(s.begin());
        return s.substr(0, 48);
    }

private:
    std::string path(const std::string& name) const { return dir() + "/" + name + ext_; }
    static std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
    static void makeDirs(const std::string& p) {
        std::string cur;
        for (size_t i = 0; i < p.size(); ++i) {
            cur += p[i];
            if ((p[i] == '/' || p[i] == '\\' || i + 1 == p.size()) && cur.size() > 1) {
#ifdef _WIN32
                _mkdir(cur.c_str());
#else
                mkdir(cur.c_str(), 0755);
#endif
            }
        }
    }
    std::string product_, ext_;
};

} // namespace epsilon
