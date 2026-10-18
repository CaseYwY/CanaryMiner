// The minimal JSON reading the Pearl pool client needs (jsonRawValue / jsonUnquote and friends).


#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace om {

namespace json_lite_detail {

inline void skipWs(const std::string &j, size_t *p) {
    while (*p < j.size() && (j[*p] == ' ' || j[*p] == '\t' ||
                             j[*p] == '\n' || j[*p] == '\r'))
        ++*p;
}

inline int hexNibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

inline bool unicodeEscape(const std::string &j, size_t *p, uint32_t *value) {
    if (*p + 4 > j.size()) return false;
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        const int nibble = hexNibble(j[(*p)++]);
        if (nibble < 0) return false;
        v = (v << 4) | (uint32_t)nibble;
    }
    *value = v;
    return true;
}

inline void appendUtf8(uint32_t cp, std::string *out) {
    if (cp <= 0x7f) {
        *out += (char)cp;
    } else if (cp <= 0x7ff) {
        *out += (char)(0xc0 | (cp >> 6));
        *out += (char)(0x80 | (cp & 0x3f));
    } else if (cp <= 0xffff) {
        *out += (char)(0xe0 | (cp >> 12));
        *out += (char)(0x80 | ((cp >> 6) & 0x3f));
        *out += (char)(0x80 | (cp & 0x3f));
    } else {
        *out += (char)(0xf0 | (cp >> 18));
        *out += (char)(0x80 | ((cp >> 12) & 0x3f));
        *out += (char)(0x80 | ((cp >> 6) & 0x3f));
        *out += (char)(0x80 | (cp & 0x3f));
    }
}

// Decode every JSON escape, including surrogate-paired Unicode. An escaped
// key must compare identically to its literal spelling for duplicate checks.
inline bool stringToken(const std::string &j, size_t *p, std::string *decoded) {
    if (*p >= j.size() || j[*p] != '"') return false;
    ++*p;
    std::string value;
    while (*p < j.size()) {
        const unsigned char ch = (unsigned char)j[(*p)++];
        if (ch == '"') {
            if (decoded) *decoded = value;
            return true;
        }
        if (ch < 0x20) return false;
        if (ch != '\\') {
            value += (char)ch;
            continue;
        }
        if (*p >= j.size()) return false;
        switch (j[(*p)++]) {
            case '"': value += '"'; break;
            case '\\': value += '\\'; break;
            case '/': value += '/'; break;
            case 'b': value += '\b'; break;
            case 'f': value += '\f'; break;
            case 'n': value += '\n'; break;
            case 'r': value += '\r'; break;
            case 't': value += '\t'; break;
            case 'u': {
                uint32_t cp = 0;
                if (!unicodeEscape(j, p, &cp)) return false;
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (*p + 2 > j.size() || j[*p] != '\\' ||
                        j[*p + 1] != 'u')
                        return false;
                    *p += 2;
                    uint32_t low = 0;
                    if (!unicodeEscape(j, p, &low) || low < 0xdc00 ||
                        low > 0xdfff)
                        return false;
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                    return false;
                }
                appendUtf8(cp, &value);
                break;
            }
            default: return false;
        }
    }
    return false;
}

inline bool skipValue(const std::string &j, size_t *p, unsigned depth);

inline bool skipObject(const std::string &j, size_t *p, unsigned depth) {
    if (depth > 64 || *p >= j.size() || j[*p] != '{') return false;
    ++*p;
    skipWs(j, p);
    if (*p < j.size() && j[*p] == '}') { ++*p; return true; }
    for (;;) {
        if (!stringToken(j, p, nullptr)) return false;
        skipWs(j, p);
        if (*p >= j.size() || j[(*p)++] != ':') return false;
        skipWs(j, p);
        if (!skipValue(j, p, depth + 1)) return false;
        skipWs(j, p);
        if (*p >= j.size()) return false;
        const char delim = j[(*p)++];
        if (delim == '}') return true;
        if (delim != ',') return false;
        skipWs(j, p);
        if (*p >= j.size() || j[*p] == '}') return false;
    }
}

inline bool skipArray(const std::string &j, size_t *p, unsigned depth) {
    if (depth > 64 || *p >= j.size() || j[*p] != '[') return false;
    ++*p;
    skipWs(j, p);
    if (*p < j.size() && j[*p] == ']') { ++*p; return true; }
    for (;;) {
        if (!skipValue(j, p, depth + 1)) return false;
        skipWs(j, p);
        if (*p >= j.size()) return false;
        const char delim = j[(*p)++];
        if (delim == ']') return true;
        if (delim != ',') return false;
        skipWs(j, p);
        if (*p >= j.size() || j[*p] == ']') return false;
    }
}

inline bool skipNumber(const std::string &j, size_t *p) {
    const size_t start = *p;
    if (*p < j.size() && j[*p] == '-') ++*p;
    if (*p >= j.size()) return false;
    if (j[*p] == '0') {
        ++*p;
        if (*p < j.size() && isdigit((unsigned char)j[*p])) return false;
    } else {
        if (j[*p] < '1' || j[*p] > '9') return false;
        while (*p < j.size() && isdigit((unsigned char)j[*p])) ++*p;
    }
    if (*p < j.size() && j[*p] == '.') {
        ++*p;
        const size_t digits = *p;
        while (*p < j.size() && isdigit((unsigned char)j[*p])) ++*p;
        if (*p == digits) return false;
    }
    if (*p < j.size() && (j[*p] == 'e' || j[*p] == 'E')) {
        ++*p;
        if (*p < j.size() && (j[*p] == '+' || j[*p] == '-')) ++*p;
        const size_t digits = *p;
        while (*p < j.size() && isdigit((unsigned char)j[*p])) ++*p;
        if (*p == digits) return false;
    }
    return *p > start;
}

inline bool skipValue(const std::string &j, size_t *p, unsigned depth) {
    if (depth > 64 || *p >= j.size()) return false;
    if (j[*p] == '"') return stringToken(j, p, nullptr);
    if (j[*p] == '{') return skipObject(j, p, depth);
    if (j[*p] == '[') return skipArray(j, p, depth);
    static const char *literals[] = {"true", "false", "null"};
    for (const char *literal : literals) {
        const size_t n = strlen(literal);
        if (j.compare(*p, n, literal) == 0) { *p += n; return true; }
    }
    return skipNumber(j, p);
}

}  // namespace json_lite_detail

/** Extract one direct member from a complete JSON object.
 *
 * The whole object is validated even after the requested member is found.
 * Duplicate direct keys, trailing bytes, malformed nesting and malformed
 * string escapes therefore fail closed.  `out` changes only on success.
 */
inline bool jsonObjectRawValueStrict(const std::string &j, const char *key,
                                     std::string *out) {
    if (!key || !out) return false;
    size_t p = 0;
    json_lite_detail::skipWs(j, &p);
    if (p >= j.size() || j[p++] != '{') return false;
    json_lite_detail::skipWs(j, &p);
    bool found = false;
    std::string foundValue;
    std::vector<std::string> seen;
    if (p < j.size() && j[p] == '}') {
        ++p;
    } else {
        for (;;) {
            std::string member;
            if (!json_lite_detail::stringToken(j, &p, &member)) return false;
            for (const std::string &prior : seen)
                if (prior == member) return false;
            seen.push_back(member);
            json_lite_detail::skipWs(j, &p);
            if (p >= j.size() || j[p++] != ':') return false;
            json_lite_detail::skipWs(j, &p);
            const size_t start = p;
            if (!json_lite_detail::skipValue(j, &p, 1)) return false;
            const size_t end = p;
            if (member == key) {
                found = true;
                foundValue = j.substr(start, end - start);
            }
            json_lite_detail::skipWs(j, &p);
            if (p >= j.size()) return false;
            const char delim = j[p++];
            if (delim == '}') break;
            if (delim != ',') return false;
            json_lite_detail::skipWs(j, &p);
            if (p >= j.size() || j[p] == '}') return false;
        }
    }
    json_lite_detail::skipWs(j, &p);
    if (p != j.size() || !found) return false;
    *out = foundValue;
    return true;
}

/** Strict direct-member extraction requiring a JSON string value. */
inline bool jsonObjectStringStrict(const std::string &j, const char *key,
                                   std::string *out) {
    std::string raw;
    if (!jsonObjectRawValueStrict(j, key, &raw)) return false;
    size_t p = 0;
    std::string decoded;
    if (!json_lite_detail::stringToken(raw, &p, &decoded) || p != raw.size())
        return false;
    *out = decoded;
    return true;
}

/** Extract the sole element of a complete JSON array, without coercion. */
inline bool jsonArraySingleRawValueStrict(const std::string &j,
                                          std::string *out) {
    if (!out) return false;
    size_t p = 0;
    json_lite_detail::skipWs(j, &p);
    if (p >= j.size() || j[p++] != '[') return false;
    json_lite_detail::skipWs(j, &p);
    if (p >= j.size() || j[p] == ']') return false;
    const size_t start = p;
    if (!json_lite_detail::skipValue(j, &p, 1)) return false;
    const size_t end = p;
    json_lite_detail::skipWs(j, &p);
    if (p >= j.size() || j[p++] != ']') return false;
    json_lite_detail::skipWs(j, &p);
    if (p != j.size()) return false;
    *out = j.substr(start, end - start);
    return true;
}

/** Extracts the raw text of a JSON value by key, without a JSON library. */
inline bool jsonRawValue(const std::string &j, const char *key,
                         std::string *out) {
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = j.find(pat);
    if (p == std::string::npos) return false;
    p = j.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    p++;
    while (p < j.size() && isspace((unsigned char)j[p])) p++;
    if (p >= j.size()) return false;

    const size_t start = p;
    if (j[p] == '[' || j[p] == '{') {
        const char open = j[p], close = (open == '[') ? ']' : '}';
        int depth = 0;
        bool inStr = false;
        for (; p < j.size(); p++) {
            const char c = j[p];
            if (inStr) {
                if (c == '\\') p++;
                else if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') inStr = true;
            else if (c == open) depth++;
            else if (c == close) {
                depth--;
                if (depth == 0) { p++; break; }
            }
        }
    } else if (j[p] == '"') {
        p++;
        while (p < j.size() && j[p] != '"') {
            if (j[p] == '\\') p++;
            p++;
        }
        p++;
    } else {
        while (p < j.size() && j[p] != ',' && j[p] != '}' && j[p] != ']') p++;
        size_t b = start, e = p;
        auto ws = [](char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        };
        while (b < e && ws(j[b])) b++;
        while (e > b && ws(j[e - 1])) e--;
        *out = j.substr(b, e - b);
        return true;
    }
    *out = j.substr(start, p - start);
    return true;
}

/** Splits a JSON array's top-level elements into raw strings. */
inline std::vector<std::string> jsonSplitArray(const std::string &arr) {
    std::vector<std::string> out;
    if (arr.size() < 2) return out;
    size_t p = arr.find('[');
    if (p == std::string::npos) return out;
    p++;
    int depth = 0;
    bool inStr = false;
    size_t start = p;
    for (; p < arr.size(); p++) {
        const char c = arr[p];
        if (inStr) {
            if (c == '\\') p++;
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') inStr = true;
        else if (c == '[' || c == '{') depth++;
        else if (c == '}' || (c == ']' && depth > 0)) depth--;
        else if ((c == ',' && depth == 0) || (c == ']' && depth == 0)) {
            std::string e = arr.substr(start, p - start);
            while (!e.empty() && isspace((unsigned char)e.front())) e.erase(e.begin());
            while (!e.empty() && isspace((unsigned char)e.back())) e.pop_back();
            if (!e.empty()) out.push_back(e);
            start = p + 1;
            if (c == ']') break;
        }
    }
    return out;
}

inline std::string jsonUnquote(const std::string &s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

}  // namespace om
