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

    static bool isValueEnd(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '\'' || c == ',' || c == ';'; }

    static std::string hideOptionValues(const std::string &text) {
        static const char *const opts[] = {"--pass", "--wallet", "--user", "--api-key", "--password"};
        std::string out = text;
        for (const char *opt : opts) {
            const std::string key = opt;
            size_t pos = 0;
            while ((pos = out.find(key, pos)) != std::string::npos) {
                size_t v = pos + key.size();
                if (v < out.size() && out[v] != ' ' && out[v] != '=') { pos = v; continue; }
                if (v < out.size()) ++v;  // the space or '='
                size_t e = v;
                while (e < out.size() && !isValueEnd(out[e])) ++e;
                if (e > v && out.compare(v, e - v, "<redacted>") != 0) {
                    out.replace(v, e - v, "<redacted>");
                    e = v + 10;
                }
                pos = e;
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
                while (v < out.size() && (out[v] == ' ' || out[v] == '\t')) ++v;
                if (v >= out.size() || out[v] != ':') { pos = pos + key.size(); continue; }
                ++v;
                while (v < out.size() && (out[v] == ' ' || out[v] == '\t')) ++v;
                if (v >= out.size() || out[v] != '"') { pos = v; continue; }
                size_t e = v + 1;
                while (e < out.size() && out[e] != '"') {
                    if (out[e] == '\\' && e + 1 < out.size()) ++e;
                    ++e;
                }
                if (e >= out.size()) { pos = v; continue; }
                const std::string body = out.substr(v + 1, e - v - 1);
                if (body != "<redacted>") {
                    out.replace(v + 1, e - v - 1, "<redacted>");
                    e = v + 1 + 10;
                }
                pos = e + 1;
            }
        }
        return out;
    }
};

}  // namespace om
