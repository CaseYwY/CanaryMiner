// Small strict JSON reader for the product configuration and the local stats client.
//
// Strict means: RFC 8259 grammar only (no comments, no trailing commas, no NaN), duplicate object keys are an error,
// nesting depth is bounded, and the whole input must be consumed. Objects keep their key order. This is deliberately
// separate from json_lite.h, which only extracts fields from pool replies and never rejects unknown members.

#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace om {

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    std::string text;  // decoded string, or the exact number token
    std::vector<JsonValue> items;                           // array elements
    std::vector<std::pair<std::string, JsonValue>> members;  // object members, in input order

    bool isObject() const { return type == Type::Object; }
    bool isArray() const { return type == Type::Array; }
    bool isString() const { return type == Type::String; }
    bool isNumber() const { return type == Type::Number; }
    bool isBool() const { return type == Type::Bool; }

    const JsonValue *find(const std::string &key) const {
        for (const auto &m : members)
            if (m.first == key) return &m.second;
        return nullptr;
    }

    /** Exact non-negative integer token (digits only, no fraction/exponent/sign); false otherwise. */
    bool asUint64(uint64_t *out) const {
        if (type != Type::Number || text.empty() || text.size() > 20) return false;
        if (text.size() > 1 && text[0] == '0') return false;
        uint64_t v = 0;
        for (char ch : text) {
            if (ch < '0' || ch > '9') return false;
            const uint64_t d = static_cast<uint64_t>(ch - '0');
            if (v > (UINT64_MAX - d) / 10) return false;
            v = v * 10 + d;
        }
        *out = v;
        return true;
    }
};

namespace json_dom_detail {

constexpr unsigned kMaxDepth = 32;
constexpr size_t kMaxInput = 1u << 20;

struct Reader {
    const std::string &s;
    size_t p = 0;
    std::string error;
    explicit Reader(const std::string &text) : s(text) {}

    bool fail(const std::string &why) {
        if (error.empty()) error = why + " at byte " + std::to_string(p);
        return false;
    }
    void ws() {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) ++p;
    }
    static int hex(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
    static void utf8(uint32_t cp, std::string *o) {
        if (cp <= 0x7f) *o += static_cast<char>(cp);
        else if (cp <= 0x7ff) { *o += static_cast<char>(0xc0 | (cp >> 6)); *o += static_cast<char>(0x80 | (cp & 0x3f)); }
        else if (cp <= 0xffff) {
            *o += static_cast<char>(0xe0 | (cp >> 12)); *o += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            *o += static_cast<char>(0x80 | (cp & 0x3f));
        } else {
            *o += static_cast<char>(0xf0 | (cp >> 18)); *o += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
            *o += static_cast<char>(0x80 | ((cp >> 6) & 0x3f)); *o += static_cast<char>(0x80 | (cp & 0x3f));
        }
    }
    bool u4(uint32_t *v) {
        if (p + 4 > s.size()) return fail("truncated \\u escape");
        uint32_t r = 0;
        for (int i = 0; i < 4; ++i) {
            const int n = hex(s[p++]);
            if (n < 0) return fail("bad \\u escape");
            r = (r << 4) | static_cast<uint32_t>(n);
        }
        *v = r;
        return true;
    }
    bool str(std::string *out) {
        if (p >= s.size() || s[p] != '"') return fail("expected string");
        ++p;
        std::string v;
        while (p < s.size()) {
            const unsigned char ch = static_cast<unsigned char>(s[p++]);
            if (ch == '"') { *out = v; return true; }
            if (ch < 0x20) return fail("control character in string");
            if (ch != '\\') { v += static_cast<char>(ch); continue; }
            if (p >= s.size()) return fail("truncated escape");
            switch (s[p++]) {
                case '"': v += '"'; break;
                case '\\': v += '\\'; break;
                case '/': v += '/'; break;
                case 'b': v += '\b'; break;
                case 'f': v += '\f'; break;
                case 'n': v += '\n'; break;
                case 'r': v += '\r'; break;
                case 't': v += '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!u4(&cp)) return false;
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        if (p + 2 > s.size() || s[p] != '\\' || s[p + 1] != 'u') return fail("lone surrogate");
                        p += 2;
                        uint32_t lo = 0;
                        if (!u4(&lo)) return false;
                        if (lo < 0xdc00 || lo > 0xdfff) return fail("bad surrogate pair");
                        cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                    } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                        return fail("lone surrogate");
                    }
                    utf8(cp, &v);
                    break;
                }
                default: return fail("unknown escape");
            }
        }
        return fail("unterminated string");
    }
    bool number(JsonValue *v) {
        const size_t b = p;
        if (p < s.size() && s[p] == '-') ++p;
        if (p >= s.size()) return fail("bad number");
        if (s[p] == '0') ++p;
        else if (s[p] >= '1' && s[p] <= '9') { while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p; }
        else return fail("bad number");
        if (p < s.size() && s[p] == '.') {
            ++p;
            if (p >= s.size() || s[p] < '0' || s[p] > '9') return fail("bad fraction");
            while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p;
        }
        if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) {
            ++p;
            if (p < s.size() && (s[p] == '+' || s[p] == '-')) ++p;
            if (p >= s.size() || s[p] < '0' || s[p] > '9') return fail("bad exponent");
            while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p;
        }
        v->type = JsonValue::Type::Number;
        v->text = s.substr(b, p - b);
        return true;
    }
    bool literal(const char *word, JsonValue *v, JsonValue::Type t, bool b) {
        const size_t n = std::strlen(word);
        if (s.compare(p, n, word) != 0) return fail("bad literal");
        p += n;
        v->type = t;
        v->boolean = b;
        return true;
    }
    bool value(JsonValue *v, unsigned depth) {
        if (depth > kMaxDepth) return fail("nesting too deep");
        ws();
        if (p >= s.size()) return fail("unexpected end");
        const char c = s[p];
        if (c == '{') {
            ++p;
            v->type = JsonValue::Type::Object;
            ws();
            if (p < s.size() && s[p] == '}') { ++p; return true; }
            for (;;) {
                ws();
                std::string key;
                if (!str(&key)) return false;
                for (const auto &m : v->members)
                    if (m.first == key) return fail("duplicate key '" + key + "'");
                ws();
                if (p >= s.size() || s[p] != ':') return fail("expected ':'");
                ++p;
                JsonValue child;
                if (!value(&child, depth + 1)) return false;
                v->members.emplace_back(std::move(key), std::move(child));
                ws();
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                if (p < s.size() && s[p] == '}') { ++p; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++p;
            v->type = JsonValue::Type::Array;
            ws();
            if (p < s.size() && s[p] == ']') { ++p; return true; }
            for (;;) {
                JsonValue child;
                if (!value(&child, depth + 1)) return false;
                v->items.push_back(std::move(child));
                ws();
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                if (p < s.size() && s[p] == ']') { ++p; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { v->type = JsonValue::Type::String; return str(&v->text); }
        if (c == 't') return literal("true", v, JsonValue::Type::Bool, true);
        if (c == 'f') return literal("false", v, JsonValue::Type::Bool, false);
        if (c == 'n') return literal("null", v, JsonValue::Type::Null, false);
        if (c == '-' || (c >= '0' && c <= '9')) return number(v);
        return fail("unexpected character");
    }
};

}  // namespace json_dom_detail

/** Parse one complete JSON document. On failure returns false and a one-line reason in *error. */
inline bool parseJsonStrict(const std::string &text, JsonValue *out, std::string *error) {
    if (text.size() > json_dom_detail::kMaxInput) {
        if (error) *error = "document larger than 1 MiB";
        return false;
    }
    json_dom_detail::Reader r(text);
    JsonValue v;
    // a UTF-8 byte order mark is tolerated (editors add it); nothing else is
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        r.p = 3;
    if (!r.value(&v, 0)) {
        if (error) *error = r.error;
        return false;
    }
    r.ws();
    if (r.p != text.size()) {
        r.fail("trailing data");
        if (error) *error = r.error;
        return false;
    }
    *out = std::move(v);
    return true;
}

/** JSON string literal for `s` (quotes included), escaping control characters. */
inline std::string jsonQuote(const std::string &s) {
    std::string o = "\"";
    for (const unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char *d = "0123456789abcdef";
                    o += "\\u00";
                    o += d[c >> 4];
                    o += d[c & 15];
                } else {
                    o += static_cast<char>(c);
                }
        }
    }
    o += '"';
    return o;
}

}  // namespace om
