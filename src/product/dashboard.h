// Live dashboard for the supervisor (H-032, presentation H-033): a pure renderer (any width and height, testable without a
// terminal) plus the small terminal helpers it needs (TTY size, colour level, ANSI on Windows, a key reader with arrows).
//
// Theme "canary in a coal mine" (H-033 amendment): the structure is dark grey, one canary yellow marks what matters, and every
// state also has its text label, so colour is never the only signal. Nothing forces a background colour.
//
// Nothing here reads the wallet or the donation account: the model only carries what the supervisor aggregates from the
// workers' stats endpoints and NVML, and the worker name the user passed.

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>

#include "../core/platform.h"  // winsock2.h must come before windows.h
#include "stats.h"
#include "terminal_keys.h"
#include "ui_preferences.h"
#include "financial_view.h"
#include "market_view.h"
#include "accepted_history_view.h"

#if defined(_WIN32)
#include <conio.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace om {

enum class ColorLevel { None, Basic16, Palette256, TrueColor };
enum class DashPage { Main, Details, Log };
/** What a piece of text means; the terminal decides how it looks. */
enum class Tone { Plain, Dark, Rule, Canary, Donation, Amber, Red, Green };

struct DashGpu {
    int index = 0;
    std::string name;     // as the driver names it, e.g. "NVIDIA GeForce RTX 5080"
    std::string state = "Connecting";  // Mining | Connecting | Donation | Restarting | Stopped | Error
    std::string detail;   // the worker's last status or error (shown on the Details page)
    std::string pciBus;
    std::string poolState = "-";
    unsigned restarts = 0;
    double rate = 0;      // T MAC/s, last report interval (shown as TH/s)
    double rateAvg = 0;   // T MAC/s, since start
    double powerW = -1;   // negative = not available
    int tempC = -1;
    int fanPct = -1;
    unsigned coreMhz = 0, memMhz = 0;  // 0 = not available
    double sampleAgeS = -1;            // seconds since the last stats sample; negative = never sampled
    StatsOutcomes outcomes;
};

struct DashEvent {
    std::string utc;  // HH:MM:SS, UTC
    std::string text;
};

enum class DashLogSeverity { Unclassified, Info, Warning, Error };
struct DashLogEntry {
    std::string text, utc, isoUtc;
    DashLogSeverity severity = DashLogSeverity::Unclassified;
};

struct DashModel {
    std::string version, poolEndpoint, worker;
    std::string connection = "Connecting";  // Authorized | Connecting | Error | Stopped
    uint64_t sessionSeconds = 0;
    std::vector<DashGpu> gpus;
    StatsOutcomes donation;  // all donation shares, summed over the workers
    bool donationOn = true;
    std::vector<DashEvent> events;  // oldest first
    std::vector<std::string> log;   // redacted worker output, oldest first ([L])
    std::vector<DashLogEntry> logEntries;  // structured provenance when available; raw output is unclassified
    FinancialView finances;
    PublicMarketView publicMarket;
    AcceptedHistoryView history;
};

/** What the terminal shows: the page, the scroll position, and the terminal size and colour. */
struct DashView {
    DashPage page = DashPage::Main;
    int scroll = 0;           // rows from the top of the page; the renderer clamps it
    int columns = 80;
    int rows = 0;             // 0 = no limit
    ColorLevel color = ColorLevel::None;
    bool showEvents = true;
    bool hideWorker = false;
    UiPreferences options;
    bool logFrozen = false, logScrollHeld = false;
    std::vector<DashLogEntry> frozenLog;
};

inline std::string utcIsoClock(time_t t);

namespace dash_detail {

inline std::string clip(const std::string &s, size_t w) { return s.size() > w ? s.substr(0, w) : s; }
inline std::string padRight(const std::string &s, size_t w) { return s.size() >= w ? clip(s, w) : s + std::string(w - s.size(), ' '); }
inline std::string padLeft(const std::string &s, size_t w) { return s.size() >= w ? clip(s, w) : std::string(w - s.size(), ' ') + s; }
inline std::string fixed(double v, int decimals) {
    char b[48];
    snprintf(b, sizeof(b), "%.*f", decimals, v);
    return b;
}
inline std::string hms(uint64_t seconds) {
    char b[32];
    snprintf(b, sizeof(b), "%02llu:%02llu:%02llu", static_cast<unsigned long long>(seconds / 3600),
             static_cast<unsigned long long>((seconds / 60) % 60), static_cast<unsigned long long>(seconds % 60));
    return b;
}
/** 4122 -> "4,122". */
inline std::string commas(uint64_t v) {
    const std::string d = std::to_string(v);
    std::string out;
    const size_t n = d.size();
    for (size_t i = 0; i < n; ++i) {
        if (i && (n - i) % 3 == 0) out += ',';
        out += d[i];
    }
    return out;
}
/** "NVIDIA GeForce RTX 5080" -> "RTX 5080": the vendor words add nothing on a single-vendor rig. */
inline std::string shortName(const std::string &name) {
    static const char *kPrefixes[] = {"NVIDIA GeForce ", "NVIDIA "};
    for (const char *p : kPrefixes) {
        const std::string prefix = p;
        if (name.compare(0, prefix.size(), prefix) == 0) return name.substr(prefix.size());
    }
    return name;
}
inline bool contains(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }

/** Tone of a GPU state: Mining = the bird is singing; Error and Stopped are red; the text always says the state too. */
inline Tone stateTone(const std::string &state) {
    if (state == "Mining") return Tone::Green;  // as in the private viewer
    if (state == "Donation") return Tone::Donation;
    if (state == "Connecting" || state == "Restarting") return Tone::Amber;
    if (state == "Error" || state == "Stopped") return Tone::Red;
    return Tone::Dark;
}
/** Tone of the connection line. */
inline Tone connectionTone(const std::string &connection) {
    if (connection == "Authorized") return Tone::Green;
    if (connection == "Connecting") return Tone::Amber;
    return Tone::Red;
}
/** The bird mark: yellow while every GPU mines, red when any GPU is in Error. */
inline Tone birdTone(const DashModel &m) {
    bool anyError = false, allMining = !m.gpus.empty();
    for (const DashGpu &g : m.gpus) {
        anyError = anyError || g.state == "Error";
        allMining = allMining && g.state == "Mining";
    }
    return anyError ? Tone::Red : allMining ? Tone::Canary : Tone::Amber;
}
/** Tone of one event line: accepted shares are canary, donation windows soft yellow-white, restarts and reconnects amber. */
inline Tone eventTone(const std::string &text) {
    if (contains(text, "budget spent") || contains(text, "error") || contains(text, "Error") || contains(text, "deadline") ||
        contains(text, "by force"))
        return Tone::Red;
    if (contains(text, "accepted +")) return Tone::Green;
    if (contains(text, "donation")) return Tone::Donation;
    if (contains(text, "lost") || contains(text, "restart") || contains(text, "reconnect")) return Tone::Amber;
    return Tone::Plain;
}

/** One piece of a line, with the tone it is painted in. */
struct Seg {
    std::string text;
    Tone tone = Tone::Plain;
};
using Line = std::vector<Seg>;

inline Line one(const std::string &text, Tone tone = Tone::Plain) { return Line{Seg{text, tone}}; }

/** The first `w` characters of a line. */
inline Line fitLine(const Line &in, size_t w) {
    Line out;
    size_t used = 0;
    for (const Seg &s : in) {
        if (used >= w) break;
        const size_t take = std::min(s.text.size(), w - used);
        out.push_back(Seg{s.text.substr(0, take), s.tone});
        used += take;
    }
    return out;
}

/** Word wrap at `w` characters; a word longer than the width is split. */
inline std::vector<std::string> wrapText(const std::string &text, size_t w) {
    std::vector<std::string> out;
    std::string cur;
    size_t i = 0;
    while (i <= text.size()) {
        size_t j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        std::string word = text.substr(i, j - i);
        i = j + 1;
        while (word.size() > w) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
            out.push_back(word.substr(0, w));
            word = word.substr(w);
        }
        if (cur.empty()) {
            cur = word;
        } else if (cur.size() + 1 + word.size() <= w) {
            cur += " " + word;
        } else {
            out.push_back(cur);
            cur = word;
        }
    }
    if (!cur.empty() || out.empty()) out.push_back(cur);
    return out;
}

/** A table cell: padded to its width, right-aligned for numbers. */
inline Seg cell(const std::string &text, size_t w, bool right, Tone tone) {
    return Seg{right ? padLeft(text, w) : padRight(text, w), tone};
}
/** Zero is idle: it is drawn dark so the live numbers stand out. */
inline Tone countTone(uint64_t v, Tone live) { return v ? live : Tone::Plain; }

struct ToneCodes {
    int r, g, b, c256, c16;
};
inline ToneCodes toneCodes(Tone t) {
    switch (t) {
        case Tone::Dark: return {0x6e, 0x6e, 0x6e, 243, 90};
        case Tone::Rule: return {0x6e, 0x6e, 0x6e, 243, 90};
        case Tone::Canary: return {0xff, 0xd2, 0x1f, 220, 93};
        case Tone::Donation: return {0xff, 0xf3, 0xb0, 230, 97};
        case Tone::Amber: return {0xb8, 0x86, 0x0b, 136, 33};
        case Tone::Red: return {0xd9, 0x53, 0x4f, 167, 91};
        case Tone::Green: return {0x5f, 0xff, 0x5f, 83, 92};
        default: return {0, 0, 0, 0, 0};
    }
}
/** Box-drawing rules need a UTF-8 console; the supervisor clears this when the console cannot do UTF-8 (then rules stay '-'). */
inline bool g_dashBoxRules = true;
inline std::string paint(const std::string &text, Tone tone, ColorLevel level) {
    if (level == ColorLevel::None || tone == Tone::Plain || text.empty()) return text;
    const ToneCodes c = toneCodes(tone);
    char open[48];
    // Only the canary telemetry tones use the extended palette; status, rules and legends use the private viewer's 16 colours.
    const bool viewerPalette = tone != Tone::Canary && tone != Tone::Donation;
    switch (viewerPalette ? ColorLevel::Basic16 : level) {
        case ColorLevel::TrueColor: snprintf(open, sizeof(open), "\x1b[38;2;%d;%d;%dm", c.r, c.g, c.b); break;
        case ColorLevel::Palette256: snprintf(open, sizeof(open), "\x1b[38;5;%dm", c.c256); break;
        default: snprintf(open, sizeof(open), "\x1b[%dm", c.c16); break;
    }
    // Rules: a box-drawing line on colour terminals, swapped in after layout so widths stay byte-exact; plain output keeps '-'.
    if (tone == Tone::Rule && g_dashBoxRules) {
        std::string line;
        for (char ch : text) line += ch == '-' ? std::string("\xe2\x94\x80") : std::string(1, ch);
        return std::string(open) + line + "\x1b[0m";
    }
    return std::string(open) + text + "\x1b[0m";
}
inline std::string paintLine(const Line &line, ColorLevel level) {
    std::string out;
    for (const Seg &s : line) out += paint(s.text, s.tone, level);
    return out;
}

inline void section(std::vector<Line> &out, const std::string &title, size_t w) {
    out.push_back(one(title, Tone::Canary));
    out.push_back(one(std::string(w - 1, '-'), Tone::Rule));  // one column short: the cursor never wraps at the edge
}

/** The title line: the bird mark, then the version and the worker. */
inline Line titleLine(const DashModel &m) {
    return Line{Seg{"(o> ", birdTone(m)}, Seg{"CanaryMiner " + m.version + "  -  " + m.worker, Tone::Canary}};
}

/** Main page: header, HARDWARE, SESSION SHARES, RECENT EVENTS. Widths: 80 drops Submitted and Retired; 100 and wider is full. */
inline std::vector<Line> mainPage(const DashModel &m, size_t w, bool showEvents = true) {
    const bool wide = w >= 100, full = w >= 79;
    std::vector<Line> out;
    double rate = 0, avg = 0, power = 0;
    bool havePower = false;
    for (const DashGpu &g : m.gpus) {
        rate += g.rate;
        avg += g.rateAvg;
        if (g.powerW >= 0) {
            power += g.powerW;
            havePower = true;
        }
    }
    out.push_back(titleLine(m));
    out.push_back(one("mining | PRL | Pool " + (m.poolEndpoint.empty() ? std::string("none") : m.poolEndpoint) + " | Worker " + m.worker));
    // The whole connection line takes the connection's tone, as in the private viewer.
    out.push_back(Line{Seg{"Connection " + m.connection + " | Mining session " + hms(m.sessionSeconds), connectionTone(m.connection)}});
    out.push_back(Line{Seg{"Current ", Tone::Plain}, Seg{fixed(rate, 2) + " TH/s", Tone::Canary},
                       Seg{"    Session average ", Tone::Plain}, Seg{fixed(avg, 2) + " TH/s", Tone::Canary},
                       Seg{"    GPU power ", Tone::Plain},
                       Seg{havePower ? fixed(power, 0) + " W" : std::string("n/a"), havePower ? Tone::Plain : Tone::Dark}});
    // The first minutes include connecting and kernel warm-up: say so instead of letting a low average alarm anyone.
    out.push_back(m.sessionSeconds < 300 ? one("Allow about 5 minutes for the hashrate to stabilize.", Tone::Dark) : one(""));

    // HARDWARE: fixed-width columns; the state is last, so its colour never shifts a column.
    section(out, "HARDWARE", w);
    {
        Line head{Seg{padRight("GPU", 14), Tone::Plain}, Seg{padLeft("Current TH/s", 12), Tone::Plain},
                  Seg{padLeft("Power", 9), Tone::Plain}, Seg{padLeft("Temp", 7), Tone::Plain}, Seg{padLeft("Fan", 6), Tone::Plain}};
        if (full) {
            head.push_back(Seg{padLeft("Core MHz", 10), Tone::Plain});
            head.push_back(Seg{padLeft("Mem MHz", 10), Tone::Plain});
        }
        head.push_back(Seg{"  State", Tone::Plain});
        out.push_back(head);
    }
    for (const DashGpu &g : m.gpus) {
        const std::string label = shortName(g.name) + " #" + std::to_string(g.index);
        Line row{cell(label, 14, false, Tone::Plain),
                 cell(fixed(g.rate, 2), 12, true, g.rate > 0 ? Tone::Canary : Tone::Dark),
                 cell(g.powerW >= 0 ? fixed(g.powerW, 1) + " W" : std::string("-"), 9, true, g.powerW >= 0 ? Tone::Plain : Tone::Dark),
                 cell(g.tempC >= 0 ? std::to_string(g.tempC) + " C" : std::string("-"), 7, true, g.tempC >= 0 ? Tone::Plain : Tone::Dark),
                 cell(g.fanPct >= 0 ? std::to_string(g.fanPct) + "%" : std::string("-"), 6, true, g.fanPct >= 0 ? Tone::Plain : Tone::Dark)};
        if (full) {
            row.push_back(cell(g.coreMhz ? commas(g.coreMhz) : std::string("-"), 10, true, g.coreMhz ? Tone::Plain : Tone::Dark));
            row.push_back(cell(g.memMhz ? commas(g.memMhz) : std::string("-"), 10, true, g.memMhz ? Tone::Plain : Tone::Dark));
        }
        row.push_back(Seg{"  ", Tone::Plain});
        row.push_back(Seg{g.state, stateTone(g.state)});
        out.push_back(row);
    }
    out.push_back(one(""));

    if (m.history.enabled) {
        section(out, "ACCEPTED HISTORY (observed user shares)", w);
        const auto &h = m.history;
        if (!h.ready) {
            for (const auto &part : wrapText("N/A: " + (h.error.empty() ? std::string("loading local history") : h.error), w))
                out.push_back(one(part, Tone::Amber));
        } else {
            const std::string history = "24h " + (h.clockAdjusted ? std::string("N/A") : commas(h.day)) +
                " | 7d " + (h.clockAdjusted ? std::string("N/A") : commas(h.week)) +
                " | 30d " + (h.clockAdjusted ? std::string("N/A") : commas(h.month)) + " | all local " + commas(h.all);
            for (const auto &part : wrapText(history, w)) out.push_back(one(part, Tone::Canary));
            for (const auto &part : wrapText("Collected since " + utcIsoClock(static_cast<time_t>(h.startedUtc)) +
                    "; 1s observation resolution; donor shares excluded.", w)) out.push_back(one(part, Tone::Plain));
            out.push_back(one("As of " + utcIsoClock(static_cast<time_t>(h.asOfUtc)), Tone::Dark));
            if (h.asOfUtc < h.startedUtc || h.asOfUtc - h.startedUtc < 30 * 86400 || h.gaps || h.clockAdjusted)
                for (const auto &part : wrapText("Coverage: partial span/known observations" + std::string(h.gaps ? "; recovery/gap warning" : "") +
                        (h.clockAdjusted ? "; clock adjusted, rolling totals unavailable" : ""), w)) out.push_back(one(part, Tone::Amber));
            if (h.gapFromUtc)
                for (const auto &part : wrapText("Latest uncertain interval: " + utcIsoClock(static_cast<time_t>(h.gapFromUtc)) + " to " +
                        utcIsoClock(static_cast<time_t>(h.gapToUtc)), w)) out.push_back(one(part, Tone::Amber));
        }
        out.push_back(one(""));
    }

    if (m.publicMarket.enabled) {
        section(out, m.publicMarket.heading, w);
        for (const auto &line : m.publicMarket.lines)
            for (const auto &part : wrapText(line, w)) out.push_back(one(part));
        out.push_back(one(""));
    }

    if (m.finances.enabled) {
        section(out, "PRODUCTION AND REFERENCE VALUATION", w);
        for (const auto &line : financialViewLines(m.finances))
            for (const auto &part : wrapText(line, w)) out.push_back(one(part));
        out.push_back(one(""));
    }

    // SESSION SHARES: per GPU, a TOTAL, then the donation (kept apart from the shares).
    section(out, "SESSION SHARES", w);
    auto shareHead = [&]() {
        Line head{cell("GPU", 14, false, Tone::Plain)};
        if (wide) head.push_back(cell("Submitted", 11, true, Tone::Plain));
        head.push_back(cell("Accepted", 10, true, Tone::Plain));
        if (wide) head.push_back(cell("Retired", 9, true, Tone::Plain));
        head.push_back(cell("Stale local/pool", 17, true, Tone::Plain));
        head.push_back(cell("Rejected", 10, true, Tone::Plain));
        head.push_back(cell("Invalid", 9, true, Tone::Plain));
        head.push_back(cell("Pending", 9, true, Tone::Plain));
        return head;
    };
    auto shareRow = [&](const std::string &label, Tone labelTone, const StatsOutcomes &o) {
        Line row{cell(label, 14, false, labelTone)};
        if (wide) row.push_back(cell(commas(o.submitted), 11, true, countTone(o.submitted, Tone::Plain)));
        row.push_back(cell(commas(o.accepted), 10, true, countTone(o.accepted, Tone::Canary)));
        if (wide) row.push_back(cell(commas(o.guardRetired), 9, true, countTone(o.guardRetired, Tone::Plain)));
        row.push_back(cell(commas(o.localStale) + "/" + commas(o.stale), 17, true, countTone(o.localStale + o.stale, Tone::Plain)));
        row.push_back(cell(commas(o.rejected), 10, true, countTone(o.rejected, Tone::Plain)));
        row.push_back(cell(commas(o.hostInvalid), 9, true, countTone(o.hostInvalid, Tone::Plain)));
        row.push_back(cell(commas(o.pending), 9, true, countTone(o.pending, Tone::Plain)));
        return row;
    };
    out.push_back(shareHead());
    StatsOutcomes total;
    for (const DashGpu &g : m.gpus) {
        const StatsOutcomes &o = g.outcomes;
        total.submitted += o.submitted;
        total.accepted += o.accepted;
        total.guardRetired += o.guardRetired;
        total.localStale += o.localStale;
        total.stale += o.stale;
        total.rejected += o.rejected;
        total.hostInvalid += o.hostInvalid;
        total.pending += o.pending;
        out.push_back(shareRow(shortName(g.name) + " #" + std::to_string(g.index), Tone::Plain, o));
    }
    out.push_back(shareRow("TOTAL", Tone::Plain, total));
    out.push_back(shareRow(m.donationOn ? "Donation" : "Donation (off)", Tone::Donation, m.donation));
    for (const std::string &line : wrapText(
             "Local stale was not sent. Rejected = pool rejections only. Donation = the developer donation, separate from your shares.", w))
        out.push_back(one(line, Tone::Dark));
    out.push_back(one(""));

    if (showEvents) {
        section(out, "RECENT EVENTS - UTC", w);
        if (m.events.empty()) out.push_back(one("none yet", Tone::Dark));
        for (const DashEvent &e : m.events) out.push_back(Line{Seg{e.utc, Tone::Dark}, Seg{"  ", Tone::Plain}, Seg{e.text, eventTone(e.text)}});
    }
    return out;
}

/** Details page: per GPU, the PCI bus, pool state, restarts and last error. */
inline std::vector<Line> detailsPage(const DashModel &m, size_t w, const UiPreferences &options = UiPreferences{}) {
    std::vector<Line> out;
    section(out, "DETAILS", w);
    if (m.gpus.empty()) out.push_back(one("no GPUs", Tone::Dark));
    for (const DashGpu &g : m.gpus) {
        if (options.detailsIdentity)
            out.push_back(Line{Seg{shortName(g.name) + " #" + std::to_string(g.index), Tone::Canary},
                               Seg{"  pci " + (g.pciBus.empty() ? std::string("-") : g.pciBus), Tone::Dark}});
        else out.push_back(one("GPU " + std::to_string(g.index), Tone::Canary));
        Line state{Seg{"  State ", Tone::Plain}, Seg{g.state, stateTone(g.state)}};
        if (options.detailsPool) state.push_back({"  Pool " + g.poolState, Tone::Plain});
        if (options.detailsRestarts) state.push_back({"  Restarts " + std::to_string(g.restarts), Tone::Plain});
        out.push_back(state);
        const bool faulty = g.state == "Error" || g.state == "Restarting";
        if (options.detailsErrors)
            out.push_back(one("  Last error: " + (faulty && !g.detail.empty() ? g.detail : std::string("none")), faulty ? Tone::Amber : Tone::Dark));
        out.push_back(one(""));
    }
    return out;
}

/** Log page: the redacted worker output, newest last. */
inline std::vector<Line> logPage(const std::vector<DashLogEntry> &entries, size_t w, const UiPreferences &options) {
    std::vector<Line> out;
    section(out, "WORKER OUTPUT (redacted)", w);
    std::vector<const DashLogEntry *> selected;
    for (const auto &entry : entries) {
        const bool unknown = entry.severity == DashLogSeverity::Unclassified;
        const bool matches = options.logSeverity == 0 || (options.logSeverity == 1 && entry.severity >= DashLogSeverity::Warning) ||
                             (options.logSeverity == 2 && entry.severity == DashLogSeverity::Error);
        if (unknown ? options.logUnclassified : matches) selected.push_back(&entry);
    }
    if (selected.empty()) out.push_back(one(entries.empty() ? "none yet" : "no matching entries (raw lines are unclassified)", Tone::Dark));
    const size_t from = selected.size() > options.logRows ? selected.size() - options.logRows : 0;
    for (size_t i = from; i < selected.size(); ++i) {
        const auto &entry = *selected[i];
        const auto &time = options.logIsoTime ? entry.isoUtc : entry.utc;
        const std::string prefix = options.logTimestamps ? "[" + (time.empty() ? std::string("time unavailable") : time) + "] " : "";
        const Tone tone = entry.severity == DashLogSeverity::Error ? Tone::Red : entry.severity == DashLogSeverity::Warning ? Tone::Amber : Tone::Plain;
        for (const std::string &part : wrapText(prefix + entry.text, w)) out.push_back(one(part, tone));
    }
    return out;
}

}  // namespace dash_detail

/** One UTC wall-clock time as HH:MM:SS. */
inline std::string utcClock(time_t t) {
    struct tm g {};
#if defined(_WIN32)
    gmtime_s(&g, &t);
#else
    gmtime_r(&t, &g);
#endif
    char b[16];
    snprintf(b, sizeof(b), "%02d:%02d:%02d", g.tm_hour, g.tm_min, g.tm_sec);
    return b;
}

inline std::string utcIsoClock(time_t t) {
    struct tm g {};
#if defined(_WIN32)
    if (gmtime_s(&g, &t) != 0) return "[invalid UTC]";
#else
    if (!gmtime_r(&t, &g)) return "[invalid UTC]";
#endif
    char b[32];
    strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", &g);
    return b;
}

/**
 * Full-screen frame for `view`. The page scrolls inside the terminal rows; the row counter, the data age and the key list stay
 * at the bottom, and the title closes the frame. `view.scroll` is clamped to the page. Colour follows `view.color`.
 */
inline std::string renderDashboard(const DashModel &input, DashView &view) {
    using namespace dash_detail;
    DashModel m = input;
    if (view.hideWorker) m.worker = "[hidden]";
    if ((view.page == DashPage::Details && !view.options.detailsEnabled) || (view.page == DashPage::Log && !view.options.logEnabled)) {
        view.page = DashPage::Main;
        view.scroll = 0;
    }
    if (m.logEntries.empty()) for (const auto &text : m.log) m.logEntries.push_back({text, "", "", DashLogSeverity::Unclassified});
    if (view.page != DashPage::Log || view.options.logFollow) {
        view.logFrozen = false;
        view.frozenLog.clear();
    } else if (!view.logFrozen) {
        view.frozenLog = m.logEntries;
        view.logFrozen = true;
    }
    const size_t W = static_cast<size_t>(view.columns < 60 ? 60 : view.columns);
    const std::vector<Line> body = view.page == DashPage::Details ? detailsPage(m, W, view.options)
                                   : view.page == DashPage::Log    ? logPage(view.logFrozen ? view.frozenLog : m.logEntries, W, view.options)
                                                                    : mainPage(m, W, view.showEvents);

    double age = -1;
    for (const DashGpu &g : m.gpus) age = std::max(age, g.sampleAgeS);
    const std::string dataLine = std::string("Data age ") + (age < 0 ? std::string("N/A") : fixed(age, 1) + "s") +
        " | Current: 10s | Average: mining session |" + (view.options.detailsEnabled ? " [D] Details" : "") +
        (view.options.logEnabled ? " [L] Log" : "") + " [M] Main [Q] Quit [S] Settings" +
        (view.page == DashPage::Log ? std::string(" | ") + (view.logFrozen ? "Frozen" : view.logScrollHeld ? "Scroll held [F] Follow" : "Following") : "");
    const std::vector<std::string> dataRows = wrapText(dataLine, W);

    // Footer = row counter + data rows; the title is the last line. The page gets what is left of the rows (one spare row
    // keeps the cursor from scrolling the console).
    const size_t n = body.size();
    const long extra = static_cast<long>(1 + dataRows.size() + 1);
    size_t budget = n;
    if (view.rows > 0) budget = static_cast<size_t>(std::max<long>(1, static_cast<long>(view.rows) - 1 - extra));
    const size_t maxScroll = n > budget ? n - budget : 0;
    if (view.page == DashPage::Log && view.options.logFollow && !view.logScrollHeld) view.scroll = static_cast<int>(maxScroll);
    if (view.scroll < 0) view.scroll = 0;
    if (static_cast<size_t>(view.scroll) > maxScroll) view.scroll = static_cast<int>(maxScroll);
    const size_t from = static_cast<size_t>(view.scroll);
    const size_t to = std::min(n, from + budget);

    std::string out;
    for (size_t i = from; i < to; ++i) out += paintLine(fitLine(body[i], W), view.color) + "\n";
    const std::string rowsLine = "Rows " + std::to_string(n ? from + 1 : 0) + "-" + std::to_string(to) + "/" + std::to_string(n) +
                                 " | Up/Down or PgUp/PgDn scroll";
    out += paint(clip(rowsLine, W), Tone::Dark, view.color) + "\n";
    for (const std::string &row : dataRows) out += paint(clip(row, W), Tone::Dark, view.color) + "\n";
    out += paintLine(fitLine(titleLine(m), W), view.color);
    return out;
}

/** Applies one key to the view. False when the key means nothing here (Q is the caller's: it stops the run). */
inline bool handleDashKey(DashView &view, int key) {
    using namespace dash_detail;
    const int page = view.rows > 0 ? std::max(1, view.rows - 6) : 10;
    switch (key) {
        case 'd':
        case 'D':
            if (!view.options.detailsEnabled) return false;
            view.page = view.page == DashPage::Details ? DashPage::Main : DashPage::Details;
            view.scroll = 0;
            return true;
        case 'l':
        case 'L':
            if (!view.options.logEnabled) return false;
            view.page = view.page == DashPage::Log ? DashPage::Main : DashPage::Log;
            view.scroll = 0;
            view.logFrozen = false;
            view.logScrollHeld = false;
            return true;
        case 'm':
        case 'M':
            view.page = DashPage::Main;
            view.scroll = 0;
            return true;
        case 'f':
        case 'F':
            if (view.page != DashPage::Log || !view.options.logFollow) return false;
            view.logScrollHeld = false;
            return true;
        case kKeyUp:
            if (view.page == DashPage::Log) view.logScrollHeld = true;
            view.scroll = std::max(0, view.scroll - 1); return true;
        case kKeyDown: view.scroll += 1; return true;  // the renderer clamps it to the page
        case kKeyPageUp:
            if (view.page == DashPage::Log) view.logScrollHeld = true;
            view.scroll = std::max(0, view.scroll - page); return true;
        case kKeyPageDown: view.scroll += page; return true;
        default: return false;
    }
}

/** One plain, timestamped summary line (the non-TTY fallback). The model holds no wallet or donation account, so none can print. */
inline std::string renderPlainSummary(const DashModel &m, time_t now) {
    using namespace dash_detail;
    double rate = 0, avg = 0;
    uint64_t accepted = 0, rejected = 0;
    std::string states;
    for (const DashGpu &g : m.gpus) {
        rate += g.rate;
        avg += g.rateAvg;
        accepted += g.outcomes.accepted;
        rejected += g.outcomes.rejected;
        states += (states.empty() ? "" : ", ") + std::string("GPU") + std::to_string(g.index) + " " + g.state;
    }
    std::string history;
    if (m.history.enabled) {
        const auto &h = m.history;
        if (!h.ready) history = " | history N/A: " + (h.error.empty() ? std::string("loading") : h.error);
        else history = " | observed user history 24h " + (h.clockAdjusted ? std::string("N/A") : commas(h.day)) +
            ", 7d " + (h.clockAdjusted ? std::string("N/A") : commas(h.week)) +
            ", 30d " + (h.clockAdjusted ? std::string("N/A") : commas(h.month)) + ", all local " + commas(h.all) +
            ", since " + utcIsoClock(static_cast<time_t>(h.startedUtc)) + ", as of " + utcIsoClock(static_cast<time_t>(h.asOfUtc)) +
            ", 1s resolution, donation excluded, observed coverage only" + (h.gaps ? ", gaps" : "") +
            (h.clockAdjusted ? ", clock adjusted" : "");
    }
    return "[" + utcClock(now) + "] Current " + fixed(rate, 1) + " TH/s | average " + fixed(avg, 1) + " TH/s | accepted " +
           std::to_string(accepted) + " rejected " + std::to_string(rejected) + " | " + states + " | donation " +
           (m.donationOn ? std::string("on") : std::string("off")) + history;
}

// ---- terminal helpers

inline int terminalColumns(int fallback = 80) {
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        const int w = info.srWindow.Right - info.srWindow.Left + 1;
        if (w > 0) return w;
    }
    return fallback;
#else
    struct winsize ws {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
    return fallback;
#endif
}

inline int terminalRows(int fallback = 24) {
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        const int h = info.srWindow.Bottom - info.srWindow.Top + 1;
        if (h > 0) return h;
    }
    return fallback;
#else
    struct winsize ws {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) return ws.ws_row;
    return fallback;
#endif
}

/** An environment variable's value; empty when unset or unreadable. */
inline std::string envValue(const char *name) {
#if defined(_WIN32)
    char *buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, name) != 0 || !buf) return std::string();
    const std::string v(buf);
    free(buf);
    return v;
#else
    const char *v = getenv(name);
    return v ? std::string(v) : std::string();
#endif
}

/** Colour depth: NO_COLOR gives none; truecolor with COLORTERM=truecolor|24bit or Windows Terminal; 256 with a 256color TERM; else 16. */
inline ColorLevel detectColorLevel() {
    if (!envValue("NO_COLOR").empty()) return ColorLevel::None;
    const std::string colorterm = envValue("COLORTERM");
    if (colorterm == "truecolor" || colorterm == "24bit" || !envValue("WT_SESSION").empty()) return ColorLevel::TrueColor;
    if (envValue("TERM").find("256color") != std::string::npos) return ColorLevel::Palette256;
    return ColorLevel::Basic16;
}

/**
 * Reads keys without blocking. Unix: the terminal is put in no-echo, no-canonical mode until destruction. Arrow and paging keys
 * come back as kKeyUp / kKeyDown / kKeyPageUp / kKeyPageDown; any other key that is not one character reads as 0.
 */
class KeyReader {
   public:
    KeyReader() {
#if !defined(_WIN32)
        if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved_) == 0) {
            termios raw = saved_;
            raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 0;
            active_ = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
        }
#endif
    }
    ~KeyReader() {
#if !defined(_WIN32)
        if (active_) tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
#endif
    }
    KeyReader(const KeyReader &) = delete;
    KeyReader &operator=(const KeyReader &) = delete;

    /** The next key, or 0 when none is waiting. */
    int poll() {
#if defined(_WIN32)
        if (!_kbhit()) return 0;
        const int c = _getch();
        if (windowsPrefix_ || c == 0 || c == 0xE0) {  // preserve a prefix if the scan code arrives later
            const int scan = windowsPrefix_ ? c : (_kbhit() ? _getch() : -1);
            windowsPrefix_ = scan < 0;
            if (scan < 0) return 0;
            switch (scan) {
                case 72: return kKeyUp;
                case 80: return kKeyDown;
                case 73: return kKeyPageUp;
                case 81: return kKeyPageDown;
                case 75: return kKeyLeft;
                case 77: return kKeyRight;
                case 15: return kKeyBackTab;
                case 60: return kKeyF2;
                default: return 0;
            }
        }
        return c;
#else
        if (!active_) return 0;
        const auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        if (const int queued = decoder_.poll(now)) return queued;
        unsigned char c = 0;
        // Bound input work so paste/key-repeat cannot starve worker/safety polling.
        for (int n = 0; n < 256 && read(STDIN_FILENO, &c, 1) == 1; ++n) decoder_.feed(c, now);
        return decoder_.poll(now);
#endif
    }

   private:
#if !defined(_WIN32)
    termios saved_{};
    bool active_ = false;
    TerminalKeyDecoder decoder_;
#else
    bool windowsPrefix_ = false;
#endif
};

}  // namespace om
