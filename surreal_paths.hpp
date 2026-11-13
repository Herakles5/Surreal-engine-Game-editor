#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifdef __linux__
#include <limits.h>
#include <unistd.h>
#endif

namespace surreal::paths {

inline std::filesystem::path GetExecutableDir() {
#ifdef __linux__
    char buf[PATH_MAX] = {};
    ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        std::error_code ec;
        std::filesystem::path p = std::filesystem::path(buf).parent_path();
        if (!p.empty() && std::filesystem::exists(p, ec)) return p;
    }
#endif
    std::error_code ec;
    return std::filesystem::current_path(ec);
}

inline void AppendUniqueRoot(std::vector<std::filesystem::path>& roots, const std::filesystem::path& p) {
    if (p.empty()) return;
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || !std::filesystem::is_directory(p, ec)) return;
    std::filesystem::path norm = p.lexically_normal();
    for (const auto& r : roots) {
        if (r == norm) return;
    }
    roots.push_back(norm);
}

inline std::vector<std::filesystem::path> GetSearchRoots() {
    std::vector<std::filesystem::path> roots;
    if (const char* envHome = std::getenv("SURREAL_HOME")) {
        if (envHome[0] != '\0') AppendUniqueRoot(roots, std::filesystem::path(envHome));
    }
    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (!cwd.empty()) {
        AppendUniqueRoot(roots, cwd);
        AppendUniqueRoot(roots, cwd.parent_path());
    }
    std::filesystem::path exeDir = GetExecutableDir();
    if (!exeDir.empty()) {
        AppendUniqueRoot(roots, exeDir);
        AppendUniqueRoot(roots, exeDir.parent_path());
    }
    AppendUniqueRoot(roots, "/usr/local/share/surreal");
    AppendUniqueRoot(roots, "/usr/share/surreal");
    AppendUniqueRoot(roots, "/root/coding/MeinOS/surreal_engine");
    AppendUniqueRoot(roots, "/root/coding/Surreal-engine-Game-editor");
    AppendUniqueRoot(roots, "/root/coding/MeinOS");
    return roots;
}

inline std::string StripLegacyPrefix(const std::string& p) {
    static const char* const kPrefixes[] = {
        "/root/coding/MeinOS/surreal_engine/",
        "/root/coding/Surreal-engine-Game-editor/",
        "/root/coding/MeinOS/",
    };
    for (const char* pre : kPrefixes) {
        std::string sp(pre);
        if (p.rfind(sp, 0) == 0) {
            return p.substr(sp.size());
        }
    }
    return p;
}

inline std::string ResolvePath(const std::string& candidate) {
    if (candidate.empty()) return "";
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec)) {
        return std::filesystem::path(candidate).lexically_normal().string();
    }

    std::string stripped = StripLegacyPrefix(candidate);
    std::filesystem::path rel(stripped);
    auto roots = GetSearchRoots();
    for (const auto& root : roots) {
        if (!rel.is_absolute()) {
            std::filesystem::path joined = (root / rel).lexically_normal();
            if (std::filesystem::exists(joined, ec)) return joined.string();
        }
    }

    // Fallback by filename inside common asset folders
    std::string base = std::filesystem::path(candidate).filename().string();
    if (!base.empty() && base != candidate) {
        for (const auto& root : roots) {
            const std::filesystem::path subCandidates[] = {
                root / base,
                root / "assets" / "models" / base,
                root / "assets" / base,
                root / "avatar" / base,
            };
            for (const auto& c : subCandidates) {
                if (std::filesystem::exists(c, ec)) return c.lexically_normal().string();
            }
        }
    }
    return candidate;
}

inline std::string ResolveAsset(const std::string& relOrAbs) {
    if (relOrAbs.empty()) return "";
    std::string direct = ResolvePath(relOrAbs);
    std::error_code ec;
    if (std::filesystem::exists(direct, ec)) return direct;

    std::string stripped = StripLegacyPrefix(relOrAbs);
    auto roots = GetSearchRoots();
    for (const auto& root : roots) {
        const std::filesystem::path cands[] = {
            root / "assets" / stripped,
            root / "assets" / "models" / stripped,
            root / "assets" / "custom_models" / stripped,
        };
        for (const auto& c : cands) {
            if (std::filesystem::exists(c, ec)) return c.lexically_normal().string();
        }
    }
    return direct;
}

inline std::string ResolveScript(const std::string& scriptName) {
    if (scriptName.empty()) return "";
    auto roots = GetSearchRoots();
    std::error_code ec;
    for (const auto& root : roots) {
        std::filesystem::path p1 = root / "scripts" / scriptName;
        if (std::filesystem::exists(p1, ec)) return p1.lexically_normal().string();
        std::filesystem::path p2 = root / scriptName;
        if (std::filesystem::exists(p2, ec)) return p2.lexically_normal().string();
    }
    return "scripts/" + scriptName;
}

inline std::string ResolveMemoryFile() {
    if (const char* envMem = std::getenv("SURREAL_AION_MEMORY")) {
        if (envMem[0] != '\0') return std::string(envMem);
    }
    std::error_code ec;
    auto roots = GetSearchRoots();
    for (const auto& root : roots) {
        const std::filesystem::path cands[] = {
            root / "aion_minime_memory.json",
            root / "AI_Desktop" / "aion_minime_memory.json",
            root.parent_path() / "AI_Desktop" / "aion_minime_memory.json",
        };
        for (const auto& c : cands) {
            if (std::filesystem::exists(c, ec)) return c.lexically_normal().string();
        }
    }
    return "/root/coding/AI_Desktop/aion_minime_memory.json";
}

inline std::string DefaultRigGroupImage(const std::string& overridePath = "") {
    if (!overridePath.empty()) {
        std::string r = ResolvePath(overridePath);
        std::error_code ec;
        if (std::filesystem::exists(r, ec)) return r;
    }
    if (const char* envImg = std::getenv("SURREAL_RIG_GROUP_IMAGE")) {
        if (envImg[0] != '\0') {
            std::error_code ec;
            if (std::filesystem::exists(envImg, ec)) return std::string(envImg);
        }
    }
    std::vector<std::string> candidates = {
        "meovv_nobg.png",
        "assets/meovv_nobg.png",
        "meovv_1_gawon.png",
        "assets/models/meovv.jpg",
    };
    if (const char* home = std::getenv("HOME")) {
        candidates.push_back(std::string(home) + "/Downloads/meovv_nobg.png");
        candidates.push_back(std::string(home) + "/Downloads/meovv.jpg");
    }
    std::error_code ec;
    for (const auto& c : candidates) {
        std::string r = ResolvePath(c);
        if (std::filesystem::exists(r, ec)) return r;
    }
    return "meovv_1_gawon.png";
}

inline std::vector<std::string> DefaultAssetSearchDirs() {
    std::vector<std::string> dirs;
    auto addIfExists = [&](const std::filesystem::path& p) {
        if (p.empty()) return;
        std::error_code ec;
        if (!std::filesystem::exists(p, ec) || !std::filesystem::is_directory(p, ec)) return;
        std::string s = p.lexically_normal().string();
        for (const auto& existing : dirs) {
            if (existing == s) return;
        }
        dirs.push_back(s);
    };

    for (const auto& root : GetSearchRoots()) {
        addIfExists(root / "assets" / "models");
        addIfExists(root / "assets" / "custom_models");
        addIfExists(root / "assets" / "models" / "photo3d");
    }
    if (const char* home = std::getenv("HOME")) {
        addIfExists(std::filesystem::path(home) / "Downloads");
    }
    return dirs;
}

inline std::string GetDefaultAvatarPath() {
    const char* cands[] = {
        "assets/models/Aion_Avatar_Mia.gltf",
        "avatar/scene.gltf",
        "assets/models/miku_-_dance_animation.glb",
    };
    std::error_code ec;
    for (const char* c : cands) {
        std::string r = ResolveAsset(c);
        if (std::filesystem::exists(r, ec)) return r;
    }
    return "/root/coding/MeinOS/avatar/scene.gltf";
}

inline std::string ShellEscapeSingle(const std::string& input) {
    std::string out = "'";
    for (char c : input) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

inline bool HasDisplayEnv() {
    const char* d = std::getenv("DISPLAY");
    const char* w = std::getenv("WAYLAND_DISPLAY");
    return (d && d[0] != '\0') || (w && w[0] != '\0');
}

} // namespace surreal::paths

