// Redaction filter applied to every log sink (rotating file, tee of stdout/stderr) and to the printed effective config.
//
// What it hides:
//   * a payout address anywhere in a line: "prl1" followed by 20+ lowercase alphanumerics becomes "prl1...<last 4>"
//     (the tail is kept so an operator can still tell which address a line is about);
//   * registered secrets (the configured wallet, pool password, optionally the worker name), wherever they appear;
//   * the value after --pass / --wallet / --user / --api-key style options and after JSON members named "pass",
//     "password", "wallet", "authorization".
// It is a filter over text, not a guarantee about what the miner prints: the sinks apply it as the last step so a new
// message that mentions a secret still cannot reach a file.

#pragma once

#include <string>
#include <vector>

namespace om {

class Redactor {
   public:
    /** Register an exact string to hide. Strings shorter than 4 bytes are ignored (they would mangle ordinary text). */
    void addSecret(const std::string &secret, bool keepTail = false) {
        if (secret.size() < 4) return;
        for (const auto &e : secrets_)
            if (e.value == secret) return;
        secrets_.push_back({secret, keepTail});
    }

    std::string apply(const std::string &line) const {
        std::string out = line;
        for (const auto &s : secrets_) out = replaceAll(out, s);
        out = hideAddresses(out);
        out = hideOptionValues(out);
        out = hideJsonMembers(out);
        return out;
    }

    static std::string tail4(const std::string &s) { return s.size() <= 4 ? std::string("????") : s.substr(s.size() - 4); }

   private:
    struct Secret {
        std::string value;
        bool keepTail;
    };
    std::vector<Secret> secrets_;

    static std::string replaceAll(const std::string &text, const Secret &s) {
        std::string out;
        size_t pos = 0;
        for (;;) {
            const size_t at = text.find(s.value, pos);
            if (at == std::string::npos) {
                out.append(text, pos, std::string::npos);
                return out;
            }
            out.append(text, pos, at - pos);
            out += s.keepTail ? "<redacted:..." + tail4(s.value) + ">" : std::string("<redacted>");
            pos = at + s.value.size();
        }
    }

    static bool isAddrChar(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

    static std::string hideAddresses(const std::string &text) {
        std::string out;
        size_t i = 0;
        while (i < text.size()) {
            const bool boundary = i == 0 || !(isAddrChar(text[i - 1]) || (text[i - 1] >= 'A' && text[i - 1] <= 'Z') || text[i - 1] == '_');
            if (boundary && text.compare(i, 4, "prl1") == 0) {
                size_t j = i + 4;
                while (j < text.size() && isAddrChar(text[j])) ++j;
                if (j - (i + 4) >= 20) {
                    out += "prl1..." + text.substr(j - 4, 4);
                    i = j;
                    continue;
                }
            }
            out += text[i++];
        }
        return out;
    }

    static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

    // An unfinished quoted value is private through the end of the supplied text. Escaped quotes do not end it.
    static size_t quotedEnd(const std::string &text, size_t begin, char quote) {
        size_t end = begin;
        while (end < text.size() && text[end] != quote) {
            if (text[end] == '\\' && end + 1 < text.size()) ++end;
            ++end;
        }
        return end;
    }

    static std::string hideOptionValues(const std::string &text) {
        static const char *const opts[] = {"--pass", "--wallet", "--user", "--api-key", "--password"};
        std::string out = text;
        for (const char *opt : opts) {
            const std::string key = opt;
            size_t pos = 0;
            while ((pos = out.find(key, pos)) != std::string::npos) {
                size_t v = pos + key.size();
                if (v == out.size() || (!isSpace(out[v]) && out[v] != '=')) { pos = v; continue; }
                if (out[v] == '=') ++v;
                while (v < out.size() && isSpace(out[v])) ++v;
                if (v == out.size()) break;
                const bool quoted = out[v] == '"' || out[v] == '\'';
                size_t end = v, firstClose = std::string::npos;
                // Treat a compound/escaped command-line token as one private value. This is conservative
                // text filtering, not shell execution or a claim to parse every platform's argument grammar.
                while (end < out.size() && !isSpace(out[end])) {
                    if (out[end] == '"' || out[end] == '\'') {
                        const size_t close = quotedEnd(out, end + 1, out[end]);
                        if (end == v) firstClose = close;
                        if (close == out.size()) { end = close; break; }
                        end = close + 1;
                    } else if (out[end] == '\\' && end + 1 < out.size()) {
                        end += 2;
                    } else {
                        ++end;
                    }
                }
                const bool simpleQuoted = quoted && (firstClose == out.size() || firstClose + 1 == end);
                const bool closed = simpleQuoted && firstClose < out.size();
                const size_t begin = v + (simpleQuoted ? 1 : 0);
                out.replace(begin, end - begin - (closed ? 1 : 0), "<redacted>");
                pos = begin + 10 + (closed ? 1 : 0);
            }
        }
        return out;
    }

    static std::string hideJsonMembers(const std::string &text) {
        static const char *const names[] = {"pass", "password", "wallet", "authorization"};
        std::string out = text;
        for (const char *name : names) {
            const std::string key = std::string("\"") + name + "\"";
            size_t pos = 0;
            while ((pos = out.find(key, pos)) != std::string::npos) {
                size_t v = pos + key.size();
                while (v < out.size() && isSpace(out[v])) ++v;
                if (v >= out.size() || out[v] != ':') { pos = pos + key.size(); continue; }
                ++v;
                while (v < out.size() && isSpace(out[v])) ++v;
                if (v >= out.size() || out[v] != '"') { pos = v; continue; }
                const size_t begin = v + 1, end = quotedEnd(out, begin, '"');
                const bool closed = end < out.size();
                out.replace(begin, end - begin, "<redacted>");
                pos = begin + 10 + (closed ? 1 : 0);
            }
        }
        return out;
    }
};

}  // namespace om
