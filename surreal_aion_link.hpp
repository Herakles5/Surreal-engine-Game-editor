#pragma once
/**
 * ============================================================================
 * AION SURREAL ENGINE <-> AION NEXUS EVOLUTION ENGINE LINK
 *
 * Self-contained, dependency-free (POSIX sockets only) client for the
 * Evolution Engine's IPC server (Unix socket /tmp/aion_nexus.sock, TCP :9099).
 *
 *   Surreal  -> Nexus : SURREAL_TELEMETRY (live fps / entities / mode, 1 Hz)
 *                       SURREAL_BENCHMARK (scorecard -> feeds the evolution loop)
 *                       SPEAK             (Aion speaks via her TTS)
 *   Nexus    -> Surreal: CONNECTED, STEP_START, NEW_AUTONOMOUS_GOAL,
 *                       REFLECTION_FINISHED, STILLSTAND, ... -> shown in HUD
 *
 * Never blocks the render loop: non-blocking sockets, retry every few seconds.
 * Works fully offline (link simply shows OFFLINE).
 * ============================================================================
 */

#include <string>
#include <deque>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include "surreal_hud.hpp"

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#endif

namespace Surreal {

class AionLink {
public:
    bool enabled = true;
    bool connected = false;
    bool IsConnecting() const { return connecting; }
    int generation = 0;
    int rulesCount = 0;
    int focalAxiomsCount = 16;
    float compressionRatioPct = 95.8f;
    float cognitiveLoadFactor = 0.71f;
    float caecEffectiveCost = 0.796f;
    float predictedEntropy = 0.704f;
    std::string oracleAuditHash = "0x916719da452204dd";
    std::string lastGoal;

    struct Msg { std::string text; uint32_t color; float ttl; };
    std::deque<Msg> feed;

    // ---------- lifecycle ----------
    ~AionLink() { Disconnect(); }

    void Update(float dt, float fps, float frameMs, size_t entityCount, bool editMode, const std::string& backend) {
        if (!enabled) return;
#ifndef _WIN32
        for (auto& m : feed) m.ttl -= dt;
        while (!feed.empty() && feed.front().ttl <= 0.0f) feed.pop_front();

        if (!connected) {
            if (connecting && fd >= 0) {
                connectTimer += dt;
                pollfd pfd = {fd, POLLOUT | POLLERR | POLLHUP, 0};
                int pr = ::poll(&pfd, 1, 0);
                if (pr > 0) {
                    int err = 0;
                    socklen_t len = sizeof(err);
                    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0 && !(pfd.revents & (POLLERR | POLLHUP))) {
                        connecting = false;
                        connected = true;
                        Push("AION NEXUS LINK ONLINE", 0xFF00FF99, 6.0f);
                        SendRaw("{\"cmd\":\"SURREAL_HELLO\",\"app\":\"surreal_studio\"}");
                    } else {
                        Disconnect();
                    }
                } else if (pr < 0 || connectTimer > 2.5f) {
                    Disconnect();
                }
                if (!connected) return;
            } else {
                retryTimer -= dt;
                if (retryTimer <= 0.0f) {
                    retryTimer = 5.0f;
                    if (Connect()) {
                        Push("AION NEXUS LINK ONLINE", 0xFF00FF99, 6.0f);
                        SendRaw("{\"cmd\":\"SURREAL_HELLO\",\"app\":\"surreal_studio\"}");
                    }
                }
                if (!connected) return;
            }
        }

        Poll();

        telemetryTimer -= dt;
        if (telemetryTimer <= 0.0f && connected) {
            telemetryTimer = 1.0f;
            // Gen 4875 Wish #3: Real-time scene-coupled Dynamic Axiom Compression
            float fpsStrain = std::clamp((60.0f - std::max(5.0f, fps)) / 50.0f, 0.0f, 1.0f);
            float entStrain = std::clamp(static_cast<float>(entityCount) / 40.0f, 0.0f, 1.0f);
            cognitiveLoadFactor = std::clamp(0.45f * fpsStrain + 0.30f * entStrain + 0.25f * predictedEntropy, 0.08f, 0.96f);
            focalAxiomsCount = std::clamp(static_cast<int>(std::lround(36.0f - cognitiveLoadFactor * 28.0f)), 8, 36);
            int totalRules = std::max(focalAxiomsCount, rulesCount > 0 ? rulesCount : 385);
            compressionRatioPct = (1.0f - static_cast<float>(focalAxiomsCount) / static_cast<float>(totalRules)) * 100.0f;

            char buf[384];
            std::snprintf(buf, sizeof(buf),
                "{\"cmd\":\"SURREAL_TELEMETRY\",\"fps\":%.1f,\"frame_ms\":%.2f,\"entities\":%zu,\"mode\":\"%s\",\"backend\":\"%s\"}",
                fps, frameMs, entityCount, editMode ? "EDIT" : "PLAY", Escape(backend).c_str());
            SendRaw(buf);
        }
#endif
    }

    // ---------- outgoing ----------
    void SendBenchmark(const BenchmarkResults& r, int w, int h, const std::string& backend) {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "{\"cmd\":\"SURREAL_BENCHMARK\",\"avg_fps\":%.1f,\"min_fps\":%.1f,\"max_fps\":%.1f,"
            "\"skin_ms\":%.2f,\"render_ms\":%.2f,\"verts\":%llu,\"tris\":%llu,\"res\":\"%dx%d\",\"backend\":\"%s\"}",
            r.avgFps, r.minFps, r.maxFps, r.avgSkinningMs, r.avgRenderMs,
            static_cast<unsigned long long>(r.totalVertices), static_cast<unsigned long long>(r.totalTriangles),
            w, h, Escape(backend).c_str());
        SendRaw(buf);
    }

    void Speak(const std::string& text) {
        SendRaw("{\"cmd\":\"SPEAK\",\"text\":\"" + Escape(text) + "\"}");
    }

    void InjectTask(const std::string& task) {
        SendRaw("{\"cmd\":\"INJECT_TASK\",\"task\":\"" + Escape(task) + "\"}");
    }

    // ---------- HUD ----------
    void RenderHUD(uint32_t* fb, int sw, int sh) const {
        if (!enabled) return;
        const int lineH = 12;
        int panelW = 468;
        int lines = static_cast<int>(feed.size());
        int panelH = 48 + std::max(1, lines) * lineH;
        int px = 10;                    // bottom-left, above the toast / placement banner
        int py = sh - panelH - 70;
        if (py < 60) py = 60;

        SurrealHUD::DrawPanel(fb, sw, sh, px, py, panelW, panelH, 0x050A18, 0.80f);
        char head[128];
        if (connected) std::snprintf(head, sizeof(head), "AION NEXUS // ONLINE | GEN %d | RULES %d", generation, rulesCount);
        else           std::snprintf(head, sizeof(head), "AION NEXUS // OFFLINE | TPK GEN 4875 STANDALONE");
        SurrealHUD::DrawString(fb, sw, sh, px + 8, py + 6, head, connected ? 0xFF00FF99 : 0xFF00E5FF, 1);

        char tpkLine[128];
        std::snprintf(tpkLine, sizeof(tpkLine), "TPK Gen 4834: EGT=0.75 H=%.3f CAEC=%.2f(-12%%) Oracle:%s",
                      predictedEntropy, caecEffectiveCost, oracleAuditHash.substr(0, 10).c_str());
        SurrealHUD::DrawString(fb, sw, sh, px + 8, py + 18, tpkLine, 0xFF88FFCC, 1);

        char dacLine[128];
        int totalR = std::max(focalAxiomsCount, rulesCount > 0 ? rulesCount : 385);
        std::snprintf(dacLine, sizeof(dacLine), "DAC Gen 4875: %d->%d Focal Axioms (%.1f%% Comp) | Skills:13",
                      totalR, focalAxiomsCount, compressionRatioPct);
        SurrealHUD::DrawString(fb, sw, sh, px + 8, py + 30, dacLine, 0xFFFF99FF, 1);

        int y = py + 44;
        for (const auto& m : feed) {
            std::string t = m.text;
            size_t maxChars = static_cast<size_t>((panelW - 16) / 8);
            if (t.size() > maxChars) t = t.substr(0, maxChars - 2) + "..";
            SurrealHUD::DrawString(fb, sw, sh, px + 8, y, t, m.color, 1);
            y += lineH;
        }
    }

    void Push(const std::string& text, uint32_t color = 0xFF00E5FF, float ttl = 12.0f) {
        feed.push_back({text, color, ttl});
        while (feed.size() > 5) feed.pop_front();
    }

    void Disconnect() {
#ifndef _WIN32
        if (fd >= 0) { ::close(fd); fd = -1; }
#endif
        connected = false;
        connecting = false;
        connectTimer = 0.0f;
        rx.clear();
    }

private:
    int fd = -1;
    bool connecting = false;
    float connectTimer = 0.0f;
    float retryTimer = 0.0f;
    float telemetryTimer = 1.0f;
    std::string rx;

    static std::string Escape(const std::string& s) {
        std::string o;
        for (char c : s) {
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if (c == '\n') o += "\\n";
            else if (static_cast<unsigned char>(c) >= 32) o += c;
        }
        return o;
    }

    // Minimal JSON field extraction (flat objects emitted by json.dumps)
    static std::string JStr(const std::string& j, const std::string& key) {
        std::string pat = "\"" + key + "\"";
        size_t p = j.find(pat);
        if (p == std::string::npos) return "";
        p = j.find(':', p + pat.size());
        if (p == std::string::npos) return "";
        ++p;
        while (p < j.size() && j[p] == ' ') ++p;
        if (p >= j.size() || j[p] != '"') return "";
        ++p;
        std::string out;
        while (p < j.size() && j[p] != '"') {
            if (j[p] == '\\' && p + 1 < j.size()) { ++p; out += (j[p] == 'n') ? ' ' : j[p]; }
            else out += j[p];
            ++p;
        }
        return out;
    }
    static int JInt(const std::string& j, const std::string& key, int def = 0) {
        std::string pat = "\"" + key + "\"";
        size_t p = j.find(pat);
        if (p == std::string::npos) return def;
        p = j.find(':', p + pat.size());
        if (p == std::string::npos) return def;
        return std::atoi(j.c_str() + p + 1);
    }

    bool Connect() {
#ifndef _WIN32
        Disconnect();
        auto tryNonBlockingConnect = [&](int domain, const sockaddr* sa, socklen_t salen) -> int {
            int s = ::socket(domain, SOCK_STREAM, 0);
            if (s < 0) return -1;
            int flags = ::fcntl(s, F_GETFL, 0);
            if (flags >= 0) ::fcntl(s, F_SETFL, flags | O_NONBLOCK);
            int rc = ::connect(s, sa, salen);
            if (rc == 0) {
                fd = s;
                connected = true;
                connecting = false;
                return 1;
            }
            if (errno == EINPROGRESS) {
                fd = s;
                connected = false;
                connecting = true;
                connectTimer = 0.0f;
                return 0; // Asynchronous connect in flight
            }
            ::close(s);
            return -1;
        };

        sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        const char* envPath = std::getenv("AION_NEXUS_SOCK"); // override for tests / custom setups
        std::strncpy(addr.sun_path, envPath ? envPath : "/tmp/aion_nexus.sock", sizeof(addr.sun_path) - 1);
        int unixRc = tryNonBlockingConnect(AF_UNIX, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (unixRc == 1) return true;
        if (unixRc == 0) return false;

        // TCP fallback (non-blocking)
        sockaddr_in in;
        std::memset(&in, 0, sizeof(in));
        in.sin_family = AF_INET;
        in.sin_port = htons(9099);
        inet_pton(AF_INET, "127.0.0.1", &in.sin_addr);
        int tcpRc = tryNonBlockingConnect(AF_INET, reinterpret_cast<sockaddr*>(&in), sizeof(in));
        return tcpRc == 1;
#else
        return false;
#endif
    }

    void SendRaw(const std::string& json) {
#ifndef _WIN32
        if (!connected || fd < 0) return;
        std::string line = json + "\n";
        ssize_t n = ::send(fd, line.c_str(), line.size(), MSG_NOSIGNAL);
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            Disconnect();
            Push("AION NEXUS LINK LOST", 0xFFFF6666, 6.0f);
        }
#endif
    }

    void Poll() {
#ifndef _WIN32
        if (!connected || fd < 0) return;
        char buf[1024];
        for (;;) {
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n > 0) {
                rx.append(buf, static_cast<size_t>(n));
                if (rx.size() > 65536) {
                    size_t lastNl = rx.rfind('\n');
                    if (lastNl != std::string::npos) rx.erase(0, lastNl + 1);
                    else rx.clear();
                }
            } else if (n == 0) {
                Disconnect();
                Push("AION NEXUS LINK CLOSED", 0xFFFF6666, 6.0f);
                return;
            } else {
                if (errno != EAGAIN && errno != EWOULDBLOCK) Disconnect();
                break;
            }
        }
        size_t pos;
        while ((pos = rx.find('\n')) != std::string::npos) {
            std::string line = rx.substr(0, pos);
            rx.erase(0, pos + 1);
            if (!line.empty() && line[0] == '{') HandleEvent(line);
        }
#endif
    }

    void HandleEvent(const std::string& j) {
        std::string ev = JStr(j, "event");
        char buf[256];
        if (ev == "CONNECTED") {
            generation = JInt(j, "generation");
            rulesCount = JInt(j, "rules_count");
            std::snprintf(buf, sizeof(buf), "Evolution Engine synced: Gen %d, %d rules", generation, rulesCount);
            Push(buf, 0xFF00FF99);
        } else if (ev == "STEP_START") {
            Push("Step " + std::to_string(JInt(j, "step")) + ": " + JStr(j, "task"), 0xFF00E5FF);
        } else if (ev == "NEW_AUTONOMOUS_GOAL") {
            lastGoal = JStr(j, "goal");
            Push("GOAL: " + (lastGoal.empty() ? JStr(j, "new_goal") : lastGoal), 0xFFFFCC00);
        } else if (ev == "REFLECTION_FINISHED") {
            generation = JInt(j, "new_generation", generation);
            Push("NEW RULE (Gen " + std::to_string(generation) + "): " + JStr(j, "rule"), 0xFFFF88FF, 20.0f);
        } else if (ev == "STILLSTAND") {
            Push("Stillstand detected - Aion is rewiring", 0xFFFF8800);
        } else if (ev == "MEMORY_SAVED") {
            generation = JInt(j, "generation", generation);
        } else if (ev == "TASK_INJECTED") {
            Push("Task injected: " + JStr(j, "task"), 0xFF88AAFF);
        } else if (ev == "SURREAL_ACK") {
            Push(JStr(j, "message"), 0xFF00FF99);
        } else if (ev == "RULES_CONSOLIDATED") {
            rulesCount = JInt(j, "active_rules_count", rulesCount);
            std::string oHash = JStr(j, "oracle_audit_hash");
            if (!oHash.empty()) oracleAuditHash = oHash;
            std::snprintf(buf, sizeof(buf), "TPK Pruned %d rules -> %d active (Oracle %s)",
                          JInt(j, "pruned_count"), rulesCount, oracleAuditHash.substr(0, 10).c_str());
            Push(buf, 0xFF88FFCC, 16.0f);
        }
    }
};

} // namespace Surreal
