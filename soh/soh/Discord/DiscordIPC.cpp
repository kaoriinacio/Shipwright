#include "DiscordIPC.h"

#ifdef _WIN32
#include <windows.h>
#else
#error "DiscordIPC so implementado pra Windows por enquanto"
#endif

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>

namespace DiscordIPC {

static HANDLE       g_pipe     = INVALID_HANDLE_VALUE;
static std::string  g_clientId;
static int64_t      g_startTime = 0;
static bool         g_connected = false;
static time_t       g_lastPing  = 0;
static uint32_t     g_nonceSeed = 1;

// ---------- helpers ----------

static std::string EscapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

static bool WriteAll(const void* data, size_t len) {
    if (g_pipe == INVALID_HANDLE_VALUE) return false;
    const char* p = (const char*)data;
    DWORD written = 0;
    while (len > 0) {
        DWORD chunk = (DWORD)(len > 0x7FFFFFFF ? 0x7FFFFFFF : len);
        if (!WriteFile(g_pipe, p, chunk, &written, nullptr)) return false;
        p += written;
        len -= written;
    }
    return true;
}

static bool ReadAll(void* data, size_t len) {
    if (g_pipe == INVALID_HANDLE_VALUE) return false;
    char* p = (char*)data;
    DWORD read = 0;
    while (len > 0) {
        DWORD chunk = (DWORD)(len > 0x7FFFFFFF ? 0x7FFFFFFF : len);
        if (!ReadFile(g_pipe, p, chunk, &read, nullptr) || read == 0) return false;
        p += read;
        len -= read;
    }
    return true;
}

static bool SendFrame(uint32_t opcode, const std::string& payload) {
    uint32_t header[2] = { opcode, (uint32_t)payload.size() };
    if (!WriteAll(header, sizeof(header))) return false;
    if (!payload.empty() && !WriteAll(payload.data(), payload.size())) return false;
    return true;
}

static bool RecvFrame(std::string& out) {
    uint32_t header[2];
    if (!ReadAll(header, sizeof(header))) return false;
    uint32_t len = header[1];
    out.resize(len);
    if (len && !ReadAll(out.data(), len)) return false;
    return true;
}

// ---------- conexão ----------

static bool TryConnect() {
    for (int i = 0; i < 10; ++i) {
        char name[64];
        std::snprintf(name, sizeof(name), "\\\\?\\pipe\\discord-ipc-%d", i);
        HANDLE h = CreateFileA(name,
                               GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            g_pipe = h;
            return true;
        }
    }
    return false;
}

static bool Handshake() {
    std::string payload =
        "{\"v\":1,\"client_id\":\"" + EscapeJson(g_clientId) + "\"}";
    if (!SendFrame(0, payload)) return false;
    std::string resp;
    if (!RecvFrame(resp)) return false;
    // resposta esperada: {"cmd":"DISPATCH","evt":"READY",...}
    return resp.find("\"evt\":\"READY\"") != std::string::npos;
}

// ---------- API pública ----------

void Init(const std::string& clientId) {
    g_clientId  = clientId;
    g_connected = false;

    if (!TryConnect()) return;
    if (!Handshake()) {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
        return;
    }

    g_connected = true;
    g_startTime = (int64_t)std::time(nullptr);
    g_lastPing  = std::time(nullptr);
}

void Update(const std::string& details, const std::string& state,
            const std::string& largeImage, const std::string& largeText) {

    // Se caiu ou nunca conectou, tenta de novo (Discord pode ter aberto depois)
    if (!g_connected) {
        if (!TryConnect()) return;
        if (!Handshake()) {
            CloseHandle(g_pipe);
            g_pipe = INVALID_HANDLE_VALUE;
            return;
        }
        g_connected = true;
        g_startTime = (int64_t)std::time(nullptr);
    }

    std::string activity = "{";
    activity += "\"details\":\"" + EscapeJson(details) + "\",";
    activity += "\"state\":\""   + EscapeJson(state)   + "\"";
    if (!largeImage.empty()) {
        activity += ",\"assets\":{\"large_image\":\"" + EscapeJson(largeImage) + "\"";
        if (!largeText.empty())
            activity += ",\"large_text\":\"" + EscapeJson(largeText) + "\"";
        activity += "}";
    }
    activity += ",\"timestamps\":{\"start\":" + std::to_string(g_startTime) + "}";
    activity += "}";

    char nonce[32];
    std::snprintf(nonce, sizeof(nonce), "%u", g_nonceSeed++);

    std::string payload =
        "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" +
        std::to_string((unsigned long)GetCurrentProcessId()) +
        ",\"activity\":" + activity + "},\"nonce\":\"" + nonce + "\"}";

    if (!SendFrame(1, payload)) {
        // pipe morreu — fecha e tenta de novo no próximo Update
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
        g_connected = false;
        return;
    }

    std::string resp;
    if (!RecvFrame(resp)) {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
        g_connected = false;
        return;
    }

    g_lastPing = std::time(nullptr);
}

void Tick() {
    if (!g_connected || g_pipe == INVALID_HANDLE_VALUE) return;
    if (std::time(nullptr) - g_lastPing < 15) return;

    // opcode 3 = PING; Discord responde com PONG
    if (!SendFrame(3, "{}")) {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
        g_connected = false;
        return;
    }
    std::string resp;
    RecvFrame(resp);
    g_lastPing = std::time(nullptr);
}

void Shutdown() {
    if (g_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
    g_connected = false;
}

} // namespace DiscordIPC