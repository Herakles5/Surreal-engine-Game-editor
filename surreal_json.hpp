#pragma once

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace surreal::json {

class JsonValue {
public:
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    using ArrayType = std::vector<JsonValue>;
    using ObjectEntry = std::pair<std::string, JsonValue>;
    using ObjectType = std::vector<ObjectEntry>;

    Type type = Type::Null;
    bool boolVal = false;
    double numVal = 0.0;
    std::string strVal;
    ArrayType arrVal;
    ObjectType objVal;

    JsonValue() = default;
    static JsonValue MakeNull() { return JsonValue(); }
    static JsonValue MakeBool(bool b) {
        JsonValue v;
        v.type = Type::Bool;
        v.boolVal = b;
        return v;
    }
    static JsonValue MakeNumber(double d) {
        JsonValue v;
        v.type = Type::Number;
        v.numVal = std::isfinite(d) ? d : 0.0;
        return v;
    }
    static JsonValue MakeString(std::string s) {
        JsonValue v;
        v.type = Type::String;
        v.strVal = std::move(s);
        return v;
    }
    static JsonValue MakeArray() {
        JsonValue v;
        v.type = Type::Array;
        return v;
    }
    static JsonValue MakeObject() {
        JsonValue v;
        v.type = Type::Object;
        return v;
    }

    bool IsNull() const { return type == Type::Null; }
    bool IsBool() const { return type == Type::Bool; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    bool AsBool(bool def = false) const {
        if (type == Type::Bool) return boolVal;
        if (type == Type::Number) return numVal != 0.0;
        return def;
    }
    double AsDouble(double def = 0.0) const {
        if (type == Type::Number) return numVal;
        if (type == Type::Bool) return boolVal ? 1.0 : 0.0;
        return def;
    }
    float AsFloat(float def = 0.0f) const { return static_cast<float>(AsDouble(def)); }
    int AsInt(int def = 0) const { return static_cast<int>(AsDouble(def)); }
    const std::string& AsString(const std::string& def = EmptyString()) const {
        return type == Type::String ? strVal : def;
    }

    static const std::string& EmptyString() {
        static const std::string empty;
        return empty;
    }
    static const JsonValue& NullSentinel() {
        static const JsonValue nullVal;
        return nullVal;
    }

    const JsonValue* Find(const std::string& key) const {
        if (type != Type::Object) return nullptr;
        for (const auto& kv : objVal) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }

    JsonValue* FindMutable(const std::string& key) {
        if (type != Type::Object) return nullptr;
        for (auto& kv : objVal) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }

    bool HasKey(const std::string& key) const { return Find(key) != nullptr; }

    void Set(const std::string& key, JsonValue val) {
        if (type != Type::Object) {
            type = Type::Object;
            objVal.clear();
        }
        for (auto& kv : objVal) {
            if (kv.first == key) {
                kv.second = std::move(val);
                return;
            }
        }
        objVal.emplace_back(key, std::move(val));
    }

    void Push(JsonValue val) {
        if (type != Type::Array) {
            type = Type::Array;
            arrVal.clear();
        }
        arrVal.push_back(std::move(val));
    }

    const JsonValue& operator[](const std::string& key) const {
        const JsonValue* f = Find(key);
        return f ? *f : NullSentinel();
    }

    double GetDouble(const std::string& key, double def = 0.0) const {
        const JsonValue* f = Find(key);
        return f ? f->AsDouble(def) : def;
    }
    float GetFloat(const std::string& key, float def = 0.0f) const {
        const JsonValue* f = Find(key);
        return f ? f->AsFloat(def) : def;
    }
    int GetInt(const std::string& key, int def = 0) const {
        const JsonValue* f = Find(key);
        return f ? f->AsInt(def) : def;
    }
    bool GetBool(const std::string& key, bool def = false) const {
        const JsonValue* f = Find(key);
        return f ? f->AsBool(def) : def;
    }
    std::string GetString(const std::string& key, const std::string& def = "") const {
        const JsonValue* f = Find(key);
        return (f && f->IsString()) ? f->strVal : def;
    }

    static std::string EscapeString(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 8);
        out.push_back('"');
        for (unsigned char c : s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                        out += buf;
                    } else {
                        out.push_back(static_cast<char>(c));
                    }
                    break;
            }
        }
        out.push_back('"');
        return out;
    }

    std::string Dump(int indent = 2) const {
        std::string out;
        DumpImpl(out, indent, 0);
        return out;
    }

private:
    void DumpImpl(std::string& out, int indent, int depth) const {
        auto pad = [&](int d) {
            if (indent > 0) out.append(static_cast<size_t>(d * indent), ' ');
        };
        switch (type) {
            case Type::Null:
                out += "null";
                break;
            case Type::Bool:
                out += (boolVal ? "true" : "false");
                break;
            case Type::Number: {
                if (!std::isfinite(numVal)) {
                    out += "0";
                    break;
                }
                double intPart = 0.0;
                if (std::modf(numVal, &intPart) == 0.0 && std::fabs(numVal) < 1e15) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%.0f", numVal);
                    out += buf;
                } else {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%.6g", numVal);
                    out += buf;
                }
                break;
            }
            case Type::String:
                out += EscapeString(strVal);
                break;
            case Type::Array: {
                if (arrVal.empty()) {
                    out += "[]";
                    break;
                }
                out += '[';
                if (indent > 0) out += '\n';
                for (size_t i = 0; i < arrVal.size(); ++i) {
                    pad(depth + 1);
                    arrVal[i].DumpImpl(out, indent, depth + 1);
                    if (i + 1 < arrVal.size()) out += ',';
                    if (indent > 0) out += '\n';
                }
                pad(depth);
                out += ']';
                break;
            }
            case Type::Object: {
                if (objVal.empty()) {
                    out += "{}";
                    break;
                }
                out += '{';
                if (indent > 0) out += '\n';
                for (size_t i = 0; i < objVal.size(); ++i) {
                    pad(depth + 1);
                    out += EscapeString(objVal[i].first);
                    out += (indent > 0) ? ": " : ":";
                    objVal[i].second.DumpImpl(out, indent, depth + 1);
                    if (i + 1 < objVal.size()) out += ',';
                    if (indent > 0) out += '\n';
                }
                pad(depth);
                out += '}';
                break;
            }
        }
    }
};

class Parser {
public:
    static bool Parse(const std::string& input, JsonValue& out, std::string* errOut = nullptr) {
        Parser p(input);
        p.SkipWhitespace();
        if (!p.ParseValue(out, 0)) {
            if (errOut) *errOut = p.error;
            return false;
        }
        p.SkipWhitespace();
        if (p.pos != p.src.size()) {
            p.SetError("Trailing characters after JSON root");
            if (errOut) *errOut = p.error;
            return false;
        }
        return true;
    }

private:
    const std::string& src;
    size_t pos = 0;
    std::string error;

    explicit Parser(const std::string& s) : src(s) {}

    void SetError(const std::string& msg) {
        if (error.empty()) {
            error = msg + " at offset " + std::to_string(pos);
        }
    }

    void SkipWhitespace() {
        while (pos < src.size()) {
            char c = src[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos;
            else break;
        }
    }

    bool ParseValue(JsonValue& out, int depth) {
        if (depth > 64) {
            SetError("Maximum JSON nesting depth exceeded");
            return false;
        }
        SkipWhitespace();
        if (pos >= src.size()) {
            SetError("Unexpected end of JSON input");
            return false;
        }
        char c = src[pos];
        if (c == '{') return ParseObject(out, depth + 1);
        if (c == '[') return ParseArray(out, depth + 1);
        if (c == '"') {
            std::string s;
            if (!ParseString(s)) return false;
            out = JsonValue::MakeString(std::move(s));
            return true;
        }
        if (c == 't' && src.compare(pos, 4, "true") == 0) {
            pos += 4;
            out = JsonValue::MakeBool(true);
            return true;
        }
        if (c == 'f' && src.compare(pos, 5, "false") == 0) {
            pos += 5;
            out = JsonValue::MakeBool(false);
            return true;
        }
        if (c == 'n' && src.compare(pos, 4, "null") == 0) {
            pos += 4;
            out = JsonValue::MakeNull();
            return true;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            return ParseNumber(out);
        }
        SetError("Unexpected token");
        return false;
    }

    static void AppendUtf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool ParseHex4(uint32_t& code) {
        if (pos + 4 > src.size()) {
            SetError("Truncated \\uXXXX escape");
            return false;
        }
        code = 0;
        for (int i = 0; i < 4; ++i) {
            char c = src[pos++];
            code <<= 4;
            if (c >= '0' && c <= '9') code |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') code |= static_cast<uint32_t>(10 + (c - 'a'));
            else if (c >= 'A' && c <= 'F') code |= static_cast<uint32_t>(10 + (c - 'A'));
            else {
                SetError("Invalid hex digit in \\uXXXX escape");
                return false;
            }
        }
        return true;
    }

    bool ParseString(std::string& out) {
        if (pos >= src.size() || src[pos] != '"') {
            SetError("Expected '\"'");
            return false;
        }
        ++pos;
        out.clear();
        while (pos < src.size()) {
            char c = src[pos++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos >= src.size()) {
                    SetError("Unterminated escape sequence");
                    return false;
                }
                char esc = src[pos++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        uint32_t cp = 0;
                        if (!ParseHex4(cp)) return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF && pos + 2 <= src.size() && src[pos] == '\\' && src[pos + 1] == 'u') {
                            size_t savePos = pos;
                            pos += 2;
                            uint32_t low = 0;
                            if (ParseHex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                                cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                            } else {
                                pos = savePos;
                            }
                        }
                        AppendUtf8(out, cp);
                        break;
                    }
                    default:
                        SetError("Invalid escape character");
                        return false;
                }
            } else {
                out.push_back(c);
            }
        }
        SetError("Unterminated string literal");
        return false;
    }

    bool ParseNumber(JsonValue& out) {
        size_t start = pos;
        if (src[pos] == '-') ++pos;
        if (pos >= src.size() || !std::isdigit(static_cast<unsigned char>(src[pos]))) {
            SetError("Invalid number");
            return false;
        }
        if (src[pos] == '0') {
            ++pos;
        } else {
            while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) ++pos;
        }
        if (pos < src.size() && src[pos] == '.') {
            ++pos;
            if (pos >= src.size() || !std::isdigit(static_cast<unsigned char>(src[pos]))) {
                SetError("Invalid fractional number");
                return false;
            }
            while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) ++pos;
        }
        if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
            ++pos;
            if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) ++pos;
            if (pos >= src.size() || !std::isdigit(static_cast<unsigned char>(src[pos]))) {
                SetError("Invalid number exponent");
                return false;
            }
            while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) ++pos;
        }
        std::string numStr = src.substr(start, pos - start);
        char* endPtr = nullptr;
        double val = std::strtod(numStr.c_str(), &endPtr);
        if (endPtr == numStr.c_str()) {
            SetError("Failed to parse number");
            return false;
        }
        out = JsonValue::MakeNumber(val);
        return true;
    }

    bool ParseArray(JsonValue& out, int depth) {
        ++pos; // consume '['
        out = JsonValue::MakeArray();
        SkipWhitespace();
        if (pos < src.size() && src[pos] == ']') {
            ++pos;
            return true;
        }
        while (pos < src.size()) {
            JsonValue elem;
            if (!ParseValue(elem, depth)) return false;
            out.arrVal.push_back(std::move(elem));
            SkipWhitespace();
            if (pos >= src.size()) {
                SetError("Unterminated array");
                return false;
            }
            if (src[pos] == ']') {
                ++pos;
                return true;
            }
            if (src[pos] != ',') {
                SetError("Expected ',' or ']' in array");
                return false;
            }
            ++pos;
        }
        SetError("Unterminated array");
        return false;
    }

    bool ParseObject(JsonValue& out, int depth) {
        ++pos; // consume '{'
        out = JsonValue::MakeObject();
        SkipWhitespace();
        if (pos < src.size() && src[pos] == '}') {
            ++pos;
            return true;
        }
        while (pos < src.size()) {
            SkipWhitespace();
            std::string key;
            if (!ParseString(key)) return false;
            SkipWhitespace();
            if (pos >= src.size() || src[pos] != ':') {
                SetError("Expected ':' after object key");
                return false;
            }
            ++pos;
            JsonValue val;
            if (!ParseValue(val, depth)) return false;
            out.Set(key, std::move(val));
            SkipWhitespace();
            if (pos >= src.size()) {
                SetError("Unterminated object");
                return false;
            }
            if (src[pos] == '}') {
                ++pos;
                return true;
            }
            if (src[pos] != ',') {
                SetError("Expected ',' or '}' in object");
                return false;
            }
            ++pos;
        }
        SetError("Unterminated object");
        return false;
    }
};

inline bool Parse(const std::string& input, JsonValue& out, std::string* errOut = nullptr) {
    return Parser::Parse(input, out, errOut);
}

} // namespace surreal::json
