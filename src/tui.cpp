#include "fluxgate/tui.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#endif

namespace fluxgate {
namespace {

// ANSI codes (std::string so concatenation with literals works freely)
const std::string RESET    = "\033[0m";
const std::string BOLD     = "\033[1m";
const std::string DIM      = "\033[2m";
const std::string B_PURPLE = "\033[1;35m";
const std::string CYAN     = "\033[36m";
const std::string GREEN    = "\033[32m";
const std::string B_GREEN  = "\033[1;32m";
const std::string YELLOW   = "\033[33m";
const std::string B_YELLOW = "\033[1;33m";
const std::string CLEAR    = "\033[2J\033[H";

// Repeat a (possibly multibyte) UTF-8 glyph n times.
std::string repeat(const std::string& glyph, std::size_t n) {
    std::string s;
    s.reserve(glyph.size() * n);
    for (std::size_t i = 0; i < n; ++i) s += glyph;
    return s;
}

std::string pad_left(std::string s, std::size_t width) {
    if (s.size() < width) s.insert(0, width - s.size(), ' ');
    return s;
}

// Number of visible columns: skip ANSI escape sequences, count UTF-8 codepoints.
// (All glyphs we use are width-1; emoji may be off by one in some terminals.)
std::size_t visible_width(const std::string& s) {
    std::size_t cols = 0;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '\033') {                       // ESC — skip until final letter
            ++i;
            if (i < s.size() && s[i] == '[') ++i;
            while (i < s.size() && !std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
            if (i < s.size()) ++i;                  // consume the final letter
            continue;
        }
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0xC0) != 0x80) ++cols;             // count UTF-8 lead bytes only
        ++i;
    }
    return cols;
}

// Box geometry — W = inner content width in visual columns.
constexpr std::size_t W = 60;

std::string top_border() { return "╔" + repeat("═", W) + "╗"; }
std::string mid_border() { return "╠" + repeat("═", W) + "╣"; }
std::string bot_border() { return "╚" + repeat("═", W) + "╝"; }

std::string row(const std::string& content) {
    const std::size_t vis = visible_width(content);
    const std::string pad = vis < W ? std::string(W - vis, ' ') : std::string();
    return "║" + content + pad + "║";
}

std::string empty_row() { return "║" + std::string(W, ' ') + "║"; }

} // namespace

// ── Public API ────────────────────────────────────────────────────────────────

FluxGateTUI::FluxGateTUI(std::shared_ptr<Metrics> metrics, const AppConfig& config)
    : metrics_(std::move(metrics)), config_(config) {}

FluxGateTUI::~FluxGateTUI() { stop(); }

void FluxGateTUI::start() {
    running_ = true;
    thread_ = std::thread([this] { run(); });
}

void FluxGateTUI::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

bool FluxGateTUI::quit_requested() const { return quit_.load(); }

// ── Formatting ──────────────────────────────────────────────────────────────

std::string FluxGateTUI::bar(double ratio, int width) {
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    const int filled = static_cast<int>(ratio * width);
    return GREEN + repeat("█", filled) + DIM + repeat("░", width - filled) + RESET;
}

std::string FluxGateTUI::fmt_bytes(std::uint64_t b) {
    std::ostringstream o;
    if (b >= 1073741824) o << std::fixed << std::setprecision(1) << b / 1073741824.0 << " GB";
    else if (b >= 1048576) o << std::fixed << std::setprecision(1) << b / 1048576.0 << " MB";
    else if (b >= 1024)    o << std::fixed << std::setprecision(1) << b / 1024.0 << " KB";
    else                   o << b << " B";
    return o.str();
}

std::string FluxGateTUI::fmt_num(std::uint64_t n) {
    std::ostringstream o;
    if (n >= 1000000) { o << std::fixed << std::setprecision(1) << n / 1000000.0 << "M"; return o.str(); }
    if (n >= 1000)    { o << std::fixed << std::setprecision(1) << n / 1000.0 << "K"; return o.str(); }
    return std::to_string(n);
}

// ── Drawing ─────────────────────────────────────────────────────────────────

void FluxGateTUI::draw(const MetricsSnapshot& s) const {
    const double hit_rate = (s.cache_hits + s.cache_misses > 0)
        ? static_cast<double>(s.cache_hits) / (s.cache_hits + s.cache_misses) : 0.0;
    const double cost_saved = s.estimated_tokens_saved * 5.0 / 1'000'000.0;

    std::ostringstream o;
    o << CLEAR;

    // Header
    {
        const std::string mitm = config_.enable_mitm ? GREEN + "ON" + RESET : DIM + "OFF" + RESET;
        const std::string addr = config_.listen_host + ":" + std::to_string(config_.listen_port);
        o << BOLD << top_border() << RESET << "\n";
        o << row(" " + B_PURPLE + "FluxGate" + RESET + "  ·  " + CYAN + addr + RESET
                 + "  ·  MITM " + mitm) << "\n";
        o << BOLD << mid_border() << RESET << "\n";
    }

    o << empty_row() << "\n";

    // Sessions
    o << row(" " + DIM + "SESSIONS" + RESET) << "\n";
    o << row("  Active  " + BOLD + pad_left(std::to_string(s.active_sessions), 4) + RESET
             + "    Accepted  " + BOLD + pad_left(fmt_num(s.accepted_sessions), 6) + RESET
             + "    Failed  " + BOLD + pad_left(fmt_num(s.upstream_connect_failures), 4) + RESET) << "\n";

    o << empty_row() << "\n";

    // Cache
    {
        o << row(" " + DIM + "CACHE" + RESET) << "\n";
        std::ostringstream hr; hr << std::fixed << std::setprecision(1) << hit_rate * 100.0 << "%";
        const std::string hr_col = (hit_rate > 0.7 ? B_GREEN : YELLOW) + hr.str() + RESET;
        o << row("  Hit rate  " + bar(hit_rate, 22) + "  " + BOLD + hr_col) << "\n";
        o << row("  Hits  " + B_GREEN + pad_left(fmt_num(s.cache_hits), 8) + RESET
                 + "      Misses  " + BOLD + pad_left(fmt_num(s.cache_misses), 8) + RESET) << "\n";
    }

    o << empty_row() << "\n";

    // Savings + Traffic
    {
        o << row(" " + DIM + "SAVINGS / TRAFFIC" + RESET) << "\n";
        std::ostringstream cost_s; cost_s << "$" << std::fixed << std::setprecision(4) << cost_saved;
        const std::string cost_col = (cost_saved > 0.01 ? B_YELLOW : DIM) + cost_s.str() + RESET;
        o << row("  Tokens  " + B_PURPLE + pad_left(fmt_num(s.estimated_tokens_saved), 8) + RESET
                 + "      In   " + BOLD + pad_left(fmt_bytes(s.client_to_upstream_bytes), 9) + RESET) << "\n";
        o << row("  Cost  " + cost_col
                 + "      Out  " + BOLD + pad_left(fmt_bytes(s.upstream_to_client_bytes), 9) + RESET) << "\n";
    }

    o << empty_row() << "\n";

    // Footer
    {
        o << BOLD << mid_border() << RESET << "\n";
        const std::string url = "http://" + config_.admin_host + ":"
            + std::to_string(config_.admin_port) + "/";
        o << row("  " + DIM + "Dashboard: " + RESET + CYAN + url + RESET
                 + DIM + "   [q] quit" + RESET) << "\n";
        o << BOLD << bot_border() << RESET << "\n";
    }

    std::cout << o.str() << std::flush;
}

void FluxGateTUI::run() {
#ifndef _WIN32
    if (!isatty(STDOUT_FILENO)) {
        while (running_) std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return;
    }

    std::cout << "\033[?25l" << std::flush; // hide cursor

    struct termios orig{};
    bool raw_ok = false;
    if (isatty(STDIN_FILENO)) {
        tcgetattr(STDIN_FILENO, &orig);
        struct termios raw = orig;
        raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        raw_ok = true;
    }

    while (running_) {
        draw(metrics_->snapshot());
        if (raw_ok) {
            char c = 0;
            if (read(STDIN_FILENO, &c, 1) == 1 && (c == 'q' || c == 'Q')) {
                quit_ = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }

    if (raw_ok) tcsetattr(STDIN_FILENO, TCSANOW, &orig);
    std::cout << "\033[?25h" << RESET << "\n" << GREEN << "  FluxGate stopped.\n" << RESET << std::flush;
#else
    while (running_) std::this_thread::sleep_for(std::chrono::milliseconds(200));
#endif
}

} // namespace fluxgate
