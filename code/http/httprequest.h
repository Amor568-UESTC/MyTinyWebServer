#pragma once

#include <string>
#include <unordered_map>

#include "../buffer/buffer.h"

class HttpRequest
{
public:
    enum PARSE_STATE {
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH,
    };

    enum HTTP_CODE {
        NO_REQUEST = 0,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECT,
    };

private:
    bool ParseRequestLine_(const std::string& line);
    bool ParseHeader_(const std::string& line);
    bool FinishHeadrs_();
    bool ParseBody_(const std::string& body);
    bool ParseFromUrlencoded_();

    PARSE_STATE state_;
    std::string method_;
    std::string path_;
    std::string version_;
    std::string body_;
    std::unordered_map<std::string, std::string> header_;
    std::unordered_map<std::string, std::string> post_;
    std::size_t contentLength_ = 0;

public:
    HttpRequest() { Init(); }
    ~HttpRequest() = default;

    void Init();

    enum class ParseRes {
        NeedMore,
        Complete,
        BadRequest,
    };
    ParseRes parse(Buffer& buff);

    std::string path() const;
    std::string& path();
    std::string method() const;
    std::string version() const;
    const std::string& body() const { return body_; }
    std::string GetPost(const std::string& key) const;
    std::string GetPost(const char* key) const;
    bool IsFormUrlencoded() const;
    bool IsKeepAlive() const;
};
