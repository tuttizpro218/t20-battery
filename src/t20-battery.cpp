// t20-battery — battery level of the HXSJ T20 (and any mouse whose dongle reuses the
// Sharkoon "Mediatrack Edge" ids 1EA7:0066 "2.4G Mouse"), read straight from hidraw.
//
// Protocol (reverse-engineered 2026-11, see docs/PROTOCOL.md):
//   * the receiver pushes [B5 67 id[4] state level] on mouse wake / re-link and on a
//     ~10 s light-sleep heartbeat;
//   * the read-only query [B5 05 00 00 00 00 00 00] makes the receiver stream its
//     256-byte status page as 64 frames [B5 05 addr 04 d0 d1 d2 d3]; the mouse's last
//     reported status sits at 0x91..0x96, so chunk 0x94 = [id3, state, level, x];
//   * the page answers even while the mouse is deep asleep, but holds the last value —
//     and charging is not reported by this firmware at all.
//
// Output (for polybar's custom/script):
//   fresh:   "\uF245 96%"   red below 15%
//   stale:   dimmed         (receiver answers but we could not refresh)
//   missing: last cached value, dimmed, for up to 30 minutes, then nothing
//
// Build:  make
// Usage:  t20-battery [--plain] [--raw]
//   --plain   no polybar formatting ("T20 96%")
//   --raw     "state level" hex pair, for debugging

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

constexpr unsigned kVid = 0x1EA7, kPid = 0x0066;
constexpr long kStaleSeconds = 1800;

fs::path CachePath() {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    fs::path base = xdg && *xdg ? fs::path(xdg)
                                : fs::path(std::getenv("HOME") ? std::getenv("HOME") : ".") / ".cache";
    return base / "t20-battery";
}

struct Chunk { unsigned char b[4]; };

// Sends the page query on one hidraw node and collects the 0x90 and 0x94 chunks.
// Returns false when the node does not answer in the expected shape.
bool QueryChunks(int fd, Chunk& c90, Chunk& c94) {
    static const unsigned char query[8] = {0xB5, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    if (::write(fd, query, sizeof query) != (ssize_t)sizeof query) return false;
    bool have90 = false, have94 = false;
    long deadline = 1500;
    while (deadline > 0) {
        struct pollfd pfd{fd, POLLIN, 0};
        int pr = ::poll(&pfd, 1, 200);
        if (pr <= 0) { deadline -= 200; continue; }
        unsigned char buf[16];
        ssize_t got = ::read(fd, buf, sizeof buf);
        if (got != 8) { deadline -= 200; continue; }
        if (buf[0] != 0xB5 || buf[1] != 0x05 || buf[3] != 0x04) continue;
        if (buf[2] == 0x90) { std::memcpy(c90.b, buf + 4, 4); have90 = true; }
        if (buf[2] == 0x94) { std::memcpy(c94.b, buf + 4, 4); have94 = true; }
        if (have90 && have94) return true;
    }
    return have90 && have94;
}

// Finds the dongle's mouse interface among the hidraw nodes and queries it.
// Nodes of the same receiver that do not answer (the keyboard interface) are skipped,
// so no report descriptor parsing is needed.
bool PollReceiver(int& percent, int& state) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/sys/class/hidraw", ec)) {
        std::ifstream uevent(entry.path() / "device" / "uevent");
        if (!uevent) continue;
        std::string line;
        bool match = false;
        while (std::getline(uevent, line)) {
            if (line.rfind("HID_ID=", 0) != 0) continue;
            unsigned bus = 0, vid = 0, pid = 0;
            if (sscanf(line.c_str() + 7, "%x:%x:%x", &bus, &vid, &pid) == 3)
                match = bus == 3 && vid == kVid && pid == kPid;
            break;
        }
        if (!match) continue;

        std::string node = "/dev/" + entry.path().filename().string();
        int fd = ::open(node.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        Chunk c90{}, c94{};
        bool ok = QueryChunks(fd, c90, c94);
        ::close(fd);
        if (!ok) continue;
        percent = c94.b[2];
        state = c94.b[1];
        if (percent >= 0 && percent <= 100) return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    bool plain = false, raw = false;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--plain")) plain = true;
        else if (!std::strcmp(argv[i], "--raw")) raw = true;
        else { std::fprintf(stderr, "usage: t20-battery [--plain] [--raw]\n"); return 2; }
    }
    const char* icon = plain ? "T20 " : "\xEF\x89\x85 ";  // nf-fa-mouse_pointer

    int percent = -1, state = -1;
    bool fresh = PollReceiver(percent, state);

    fs::path cache = CachePath();
    auto emit = [&](int p, const char* color, const char* reset) {
        if (raw) { std::printf("%02x %02x\n", (unsigned)(state < 0 ? 0 : state), (unsigned)p); return 0; }
        std::string out;
        if (!plain) out += color;
        out += icon;
        out += std::to_string(p) + "%";
        if (!plain) out += reset;
        std::printf("%s\n", out.c_str());
        std::ofstream f(cache);
        if (f) f << p << " " << std::time(nullptr) << "\n";
        return 0;
    };

    if (fresh && percent >= 0) {
        if (state >= 0x80) return emit(percent, "\xEF\x83\xA7 %{F#7aa2f7}", "%{F-}");  // bolt (reserved)
        if (percent < 15) return emit(percent, "%{F#f7768e}", "%{F-}");               // low: red
        return emit(percent, "", "");
    }

    std::ifstream f(cache);
    long cached = 0;
    long long ts = 0;
    if (f && (f >> cached >> ts) && cached > 0 && cached <= 100) {
        long long age = (long long)std::time(nullptr) - ts;
        if (age < kStaleSeconds) return emit((int)cached, "%{F#6c6c6c}", "%{F-}");
    }
    return 1;  // nothing to show
}
