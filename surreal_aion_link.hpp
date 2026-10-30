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
#include <errno.h>
#endif

namespace Surreal {

class AionLink {
public:
    bool enabled = true;
    bool connected = false;
    int generation = 0;
    int rulesCount = 0;
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
            retryTimer -= dt;
            if (retryTimer <= 0.0f) {
                retryTimer = 5.0f;
                if (Connect()) {
                    Push("AION NEXUS LINK ONLINE", 0xFF00FF99, 6.0f);
                    SendRaw("{\"cmd\":\"SURREAL_HELLO\",\"app\":\"surreal_studio\"}");
                }
            }
            return;
        }

        Poll();

        telemetryTimer -= dt;
        if (telemetryTimer <= 0.0f && connected) {
            telemetryTimer = 1.0f;
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
        int panelW = 440;
        int lines = static_cast<int>(feed.size());
        int panelH = 24 + std::max(1, lines) * lineH;
        int px = 10;                    // bottom-left, above the toast / placement banner
        int py = sh - panelH - 70;
        if (py < 60) py = 60;

        SurrealHUD::DrawPanel(fb, sw, sh, px, py, panelW, panelH, 0x050A18, 0.80f);
        char head[96];
        if (connected) std::snprintf(head, sizeof(head), "AION NEXUS // ONLINE | GEN %d | RULES %d", generation, rulesCount);
        else           std::snprintf(head, sizeof(head), "AION NEXUS // OFFLINE (retrying)");
        SurrealHUD::DrawString(fb, sw, sh, px + 8, py + 6, head, connected ? 0xFF00FF99 : 0xFF886666, 1);

        int y = py + 20;
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
        rx.clear();
    }

private:
    int fd = -1;
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
        int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (s < 0) return false;
        sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        const char* envPath = std::getenv("AION_NEXUS_SOCK"); // override for tests / custom setups
        std::strncpy(addr.sun_path, envPath ? envPath : "/tmp/aion_nexus.sock", sizeof(addr.sun_path) - 1);
        if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(s);
            // TCP fallback
            s = ::socket(AF_INET, SOCK_STREAM, 0);
            if (s < 0) return false;
            sockaddr_in in;
            std::memset(&in, 0, sizeof(in));
            in.sin_family = AF_INET;
            in.sin_port = htons(9099);
            inet_pton(AF_INET, "127.0.0.1", &in.sin_addr);
            if (::connect(s, reinterpret_cast<sockaddr*>(&in), sizeof(in)) != 0) { ::close(s); return false; }
        }
        fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
        fd = s;
        connected = true;
        return true;
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
        char buf[1024];
        for (;;) {
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n > 0) { rx.append(buf, static_cast<size_t>(n)); }
            else if (n == 0) { Disconnect(); Push("AION NEXUS LINK CLOSED", 0xFFFF6666, 6.0f); return; }
            else { if (errno != EAGAIN && errno != EWOULDBLOCK) Disconnect(); break; }
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
        }
    }
};

} // namespace Surreal
