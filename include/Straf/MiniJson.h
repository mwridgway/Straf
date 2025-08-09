// Minimal JSON parser for controlled config parsing
// Supports: null, true/false, numbers (integer/float), strings with basic escapes, arrays, objects
// Not a full spec implementation (limited \uXXXX support), but robust enough for config.json
// License: MIT (c) 2025 Straf authors

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <variant>
#include <optional>
#include <cctype>
#include <stdexcept>

namespace Straf::MiniJson {

struct Value;
using Object = std::unordered_map<std::string, Value>;
using Array  = std::vector<Value>;

struct Value {
    using Var = std::variant<std::nullptr_t, bool, double, std::string, Object, Array>;
    Var v;
    Value() : v(nullptr) {}
    explicit Value(std::nullptr_t) : v(nullptr) {}
    explicit Value(bool b) : v(b) {}
    explicit Value(double d) : v(d) {}
    explicit Value(std::string s) : v(std::move(s)) {}
    explicit Value(Object o) : v(std::move(o)) {}
    explicit Value(Array a) : v(std::move(a)) {}

    bool is_null()   const { return std::holds_alternative<std::nullptr_t>(v); }
    bool is_bool()   const { return std::holds_alternative<bool>(v); }
    bool is_number() const { return std::holds_alternative<double>(v); }
    bool is_string() const { return std::holds_alternative<std::string>(v); }
    bool is_object() const { return std::holds_alternative<Object>(v); }
    bool is_array()  const { return std::holds_alternative<Array>(v); }

    const Object& as_object() const { return std::get<Object>(v); }
    const Array&  as_array()  const { return std::get<Array>(v); }
    const std::string& as_string() const { return std::get<std::string>(v); }
    double as_number() const { return std::get<double>(v); }
    bool as_bool() const { return std::get<bool>(v); }

    Object& get_object() { return std::get<Object>(v); }
    Array&  get_array()  { return std::get<Array>(v); }
    std::string& get_string() { return std::get<std::string>(v); }
};

class Parser {
public:
    explicit Parser(std::string_view s) : src_(s), i_(0) {}

    Value parse() {
        skip_ws();
        Value val = parse_value();
        skip_ws();
        if (i_ != src_.size()) throw std::runtime_error("Trailing characters after JSON value");
        return val;
    }

private:
    std::string_view src_;
    size_t i_;

    void skip_ws(){ while (i_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[i_]))) ++i_; }
    bool match(char c){ if (i_ < src_.size() && src_[i_] == c) { ++i_; return true; } return false; }
    char peek() const { return i_ < src_.size() ? src_[i_] : '\0'; }
    char get(){ if (i_ >= src_.size()) throw std::runtime_error("Unexpected end of input"); return src_[i_++]; }

    Value parse_value(){
        if (i_ >= src_.size()) throw std::runtime_error("Unexpected end of input");
        char c = src_[i_];
        if (c == 'n') return parse_null();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == '"') return parse_string();
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        throw std::runtime_error("Invalid JSON value");
    }

    Value parse_null(){ expect_literal("null"); return Value(nullptr); }
    Value parse_bool(){
        if (i_+3 < src_.size() && src_.substr(i_, 4) == "true") { i_ += 4; return Value(true); }
        if (i_+4 < src_.size() && src_.substr(i_, 5) == "false") { i_ += 5; return Value(false); }
        throw std::runtime_error("Invalid boolean literal");
    }

    void expect_literal(const char* lit){
        size_t n = std::char_traits<char>::length(lit);
        if (i_ + n > src_.size() || src_.substr(i_, n) != std::string_view(lit, n))
            throw std::runtime_error("Expected literal");
        i_ += n;
    }

    Value parse_number(){
        size_t start = i_;
        if (match('-')) {}
        if (!std::isdigit(static_cast<unsigned char>(peek()))) throw std::runtime_error("Invalid number");
        if (match('0')) {
            // ok
        } else {
            while (std::isdigit(static_cast<unsigned char>(peek()))) ++i_;
        }
        if (match('.')){
            if (!std::isdigit(static_cast<unsigned char>(peek()))) throw std::runtime_error("Invalid number fraction");
            while (std::isdigit(static_cast<unsigned char>(peek()))) ++i_;
        }
        if (peek() == 'e' || peek() == 'E'){
            ++i_;
            if (peek() == '+' || peek() == '-') ++i_;
            if (!std::isdigit(static_cast<unsigned char>(peek()))) throw std::runtime_error("Invalid number exponent");
            while (std::isdigit(static_cast<unsigned char>(peek()))) ++i_;
        }
        double d = std::stod(std::string(src_.substr(start, i_ - start)));
        return Value(d);
    }

    Value parse_string(){
        if (!match('"')) throw std::runtime_error("Expected string");
        std::string out;
        while (true){
            char c = get();
            if (c == '"') break;
            if (c == '\\'){
                char e = get();
                switch(e){
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        // Skip \uXXXX minimally (no surrogate handling). Parse hex and emit '?' placeholder.
                        for (int k=0;k<4;++k){ char h = get(); (void)h; }
                        out.push_back('?');
                        break;
                    }
                    default: throw std::runtime_error("Invalid escape");
                }
            } else {
                out.push_back(c);
            }
        }
        return Value(std::move(out));
    }

    Value parse_array(){
        if (!match('[')) throw std::runtime_error("Expected '['");
        skip_ws();
        Array arr;
        if (match(']')) return Value(std::move(arr));
        while (true){
            skip_ws();
            arr.emplace_back(parse_value());
            skip_ws();
            if (match(']')) break;
            if (!match(',')) throw std::runtime_error("Expected ',' in array");
        }
        return Value(std::move(arr));
    }

    Value parse_object(){
        if (!match('{')) throw std::runtime_error("Expected '{'");
        skip_ws();
        Object obj;
        if (match('}')) return Value(std::move(obj));
        while (true){
            skip_ws();
            if (peek() != '"') throw std::runtime_error("Expected string key");
            std::string key = std::move(parse_string().get_string());
            skip_ws();
            if (!match(':')) throw std::runtime_error("Expected ':' after key");
            skip_ws();
            obj.emplace(std::move(key), parse_value());
            skip_ws();
            if (match('}')) break;
            if (!match(',')) throw std::runtime_error("Expected ',' in object");
        }
        return Value(std::move(obj));
    }
};

inline std::optional<Value> Parse(std::string_view s){
    try {
        Parser p(s);
        return p.parse();
    } catch (...) {
        return std::nullopt;
    }
}

inline const Value* Find(const Object& obj, const std::string& key){
    auto it = obj.find(key);
    if (it == obj.end()) return nullptr;
    return &it->second;
}

} // namespace Straf::MiniJson
