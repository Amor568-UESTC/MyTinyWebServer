#include "httprequest.h"

#include <algorithm>
#include <charconv>
#include <regex>
#include <string_view>
#include <utility>

#include "../log/log.h"

using namespace std;

namespace {
string ToLowerAscii(string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

//去掉字符串两端的空格和制表符
string TrimOWS(const string& text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

//按完整选项匹配Connection,避免把close-other识别为close
bool HasConnectionToken(const string& value, const string& wanted) {
    size_t begin = 0;
    while (begin < value.size()) {
        const auto comma = value.find(',', begin);
        const auto token = ToLowerAscii(TrimOWS(value.substr(
            begin, comma == string::npos ? string::npos : comma - begin)));
        if (token == wanted) {
            return true;
        }
        if (comma == string::npos) {
            break;
        }
        begin = comma + 1;
    }
    return false;
}

int FromHex(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool DecodeFormPart(string_view encoded, string& decoded) {
    decoded.clear();
    decoded.reserve(encoded.size());
    for (size_t i = 0; i < encoded.size(); ++i) {
        const char c = encoded[i];
        if (c == '+') {
            decoded.push_back(' ');
        } else if (c == '%') {
            if (encoded.size() - i < 3) {
                return false;
            }
            const int high = FromHex(encoded[i + 1]);
            const int low = FromHex(encoded[i + 2]);
            if (high < 0 || low < 0) {
                return false;
            }
            decoded.push_back(static_cast<char>(high * 16 + low));
            i += 2;
        } else {
            decoded.push_back(c);
        }
    }
    return true;
}
}

bool HttpRequest::ParseRequestLine_(const string& line)
{
    static const regex pattern("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    smatch match;
    if (!regex_match(line, match, pattern)) {
        LOG_ERROR("RequestLine Error");
        return false;
    }
    method_ = match[1];
    path_ = match[2];
    version_ = match[3];
    state_ = HEADERS;
    return true;
}

bool HttpRequest::ParseHeader_(const string& line)
{
    static const regex pattern(R"(^([!#$%&'*+\-.^_\x60|~0-9A-Za-z]+):[ \t]*(.*)$)");
    smatch match;
    if (!regex_match(line, match, pattern)) {
        return false;
    }
    string key = ToLowerAscii(match[1].str());
    string value = TrimOWS(match[2].str());
    for (unsigned char c : value) {
        //控制字符只允许横向制表符,拒绝DEL
        if ((c < 0x20 && c != '\t') || c == 0x7f) {
            return false;
        }
    }
    if (key == "content-length" && header_.contains(key)) {
        return false;
    }
    if (key == "connection" && header_.contains(key)) {
        header_[key] += "," + value;
    } else {
        header_[key] = move(value);
    }
    return true;
}

bool HttpRequest::FinishHeadrs_()
{
    if (header_.contains("transfer-encoding")) {
        return false;
    }
    contentLength_ = 0;
    const auto length = header_.find("content-length");
    if (length == header_.end()) {
        return true;
    }
    const string& text = length->second;
    const auto result = from_chars(text.data(), text.data() + text.size(), contentLength_);
    return result.ec == errc{} && result.ptr == text.data() + text.size();
}

bool HttpRequest::ParseBody_(const string& body)
{
    body_ = body;
    if (method_ == "POST" && IsFormUrlencoded() && !ParseFromUrlencoded_()) {
        return false;
    }
    state_ = FINISH;
    //请求体可能含密码,只记录长度
    LOG_DEBUG("Request body length:%zu", body_.size());
    return true;
}

bool HttpRequest::ParseFromUrlencoded_()
{
    unordered_map<string, string> fields;
    const string_view body(body_);
    size_t begin = 0;
    while (begin < body.size()) {
        const auto separator = body.find('&', begin);
        const auto end = separator == string_view::npos ? body.size() : separator;
        const auto pair = body.substr(begin, end - begin);
        if (!pair.empty()) {
            const auto equals = pair.find('=');
            string key;
            string value;
            if (!DecodeFormPart(pair.substr(0, equals), key) ||
                !DecodeFormPart(equals == string_view::npos ? string_view{} : pair.substr(equals + 1), value)) {
                return false;
            }
            fields[move(key)] = move(value);
        }
        if (separator == string_view::npos) {
            break;
        }
        begin = separator + 1;
    }
    post_ = move(fields);
    return true;
}

void HttpRequest::Init()
{
    method_.clear();
    path_.clear();
    version_.clear();
    body_.clear();
    state_ = REQUEST_LINE;
    header_.clear();
    post_.clear();
    contentLength_ = 0;
}

HttpRequest::ParseRes HttpRequest::parse(Buffer& buff)
{
    static constexpr char crlf[] = "\r\n";
    while (state_ != FINISH) {
        if (state_ == BODY) {
            if (buff.ReadableBytes() < contentLength_) {
                return ParseRes::NeedMore;
            }
            const string body(buff.Peek(), contentLength_);
            buff.Retrieve(contentLength_);
            return ParseBody_(body) ? ParseRes::Complete : ParseRes::BadRequest;
        }
        const char* end = buff.BeginWriteConst();
        const char* lineEnd = search(buff.Peek(), end, crlf, crlf + 2);
        if (lineEnd == end) {
            return ParseRes::NeedMore;
        }
        const string line(buff.Peek(), lineEnd);
        buff.RetrieveUntil(lineEnd + 2);
        if (state_ == REQUEST_LINE) {
            if (!ParseRequestLine_(line)) {
                return ParseRes::BadRequest;
            }
        } else if (state_ == HEADERS) {
            if (line.empty()) {
                if (!FinishHeadrs_()) {
                    return ParseRes::BadRequest;
                }
                state_ = BODY;
            } else if (!ParseHeader_(line)) {
                return ParseRes::BadRequest;
            }
        }
    }
    return ParseRes::Complete;
}

string HttpRequest::path() const { return path_; }
string& HttpRequest::path() { return path_; }
string HttpRequest::method() const { return method_; }
string HttpRequest::version() const { return version_; }

string HttpRequest::GetPost(const string& key) const
{
    const auto field = post_.find(key);
    return field == post_.end() ? "" : field->second;
}

string HttpRequest::GetPost(const char* key) const
{
    assert(key != nullptr);
    return GetPost(string(key));
}

bool HttpRequest::IsFormUrlencoded() const
{
    const auto type = header_.find("content-type");
    return type != header_.end() &&
        ToLowerAscii(TrimOWS(type->second.substr(0, type->second.find(';')))) ==
            "application/x-www-form-urlencoded";
}

bool HttpRequest::IsKeepAlive() const
{
    const auto connection = header_.find("connection");
    return version_ == "1.1" && connection != header_.end() &&
        HasConnectionToken(connection->second, "keep-alive") &&
        !HasConnectionToken(connection->second, "close");
}
