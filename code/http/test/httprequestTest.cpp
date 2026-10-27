#include "../httprequest.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace std;

namespace {
void Require(bool condition, const char* message) {
    if (!condition) {
        throw runtime_error(message);
    }
}

string FormRequest(const string& body, const string& type = "application/x-www-form-urlencoded") {
    return "POST /login HTTP/1.1\r\nHost: localhost\r\nContent-Type: " + type +
           "\r\nContent-Length: " + to_string(body.size()) + "\r\n\r\n" + body;
}

void CheckEverySplit(const string& wire, const string& path) {
    for (size_t split = 0; split <= wire.size(); ++split) {
        HttpRequest request;
        Buffer buffer;
        buffer.Append(wire.data(), split);
        const auto first = request.parse(buffer);
        Require(first == (split == wire.size() ? HttpRequest::ParseRes::Complete : HttpRequest::ParseRes::NeedMore),
                "fragment was completed or rejected at the wrong boundary");
        buffer.Append(wire.data() + split, wire.size() - split);
        Require(request.parse(buffer) == HttpRequest::ParseRes::Complete, "fragmented request did not complete");
        Require(request.path() == path && buffer.ReadableBytes() == 0, "parser rewrote path or left request data");
    }
}
}

int main() {
    int passed = 0;
    int failed = 0;
    const auto run = [&](const string& name, const function<void()>& test) {
        try {
            test();
            ++passed;
            cout << "PASS " << name << '\n';
        } catch (const exception& error) {
            ++failed;
            cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };
    run("GET preserves the original root path at every split", [] {
        CheckEverySplit("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n", "/");
    });
    run("POST parsing completes without linking MySQL at every split", [] {
        CheckEverySplit(FormRequest("username=test&password=a%2Bb"), "/login");
    });
    run("form decoding preserves the raw body", [] {
        const string body = "username=a+b%26c&password=%2B%3D%25&utf8=%E4%BD%A0";
        Buffer buffer;
        buffer.Append(FormRequest(body, "Application/X-WWW-Form-Urlencoded; charset=UTF-8"));
        HttpRequest request;
        Require(request.parse(buffer) == HttpRequest::ParseRes::Complete, "valid form was rejected");
        Require(request.body() == body && request.path() == "/login", "parser performed a business action");
        Require(request.GetPost("username") == "a b&c" && request.GetPost("password") == "+=%" &&
                request.GetPost("utf8") == "\xe4\xbd\xa0", "incorrect percent decoding");
    });
    run("form fields use the first equals sign and the last duplicate value", [] {
        Buffer buffer;
        buffer.Append(FormRequest("x=first&x=a=b&empty=&flag&&nul=%00"));
        HttpRequest request;
        Require(request.parse(buffer) == HttpRequest::ParseRes::Complete, "valid fields were rejected");
        Require(request.GetPost("x") == "a=b" && request.GetPost("empty").empty() &&
                request.GetPost("flag").empty() && request.GetPost("nul") == string(1, '\0'), "wrong field values");
    });
    run("non-form request body is opaque", [] {
        Buffer buffer;
        buffer.Append(FormRequest("username=raw%GG", "application/json"));
        HttpRequest request;
        Require(request.parse(buffer) == HttpRequest::ParseRes::Complete && request.GetPost("username").empty() &&
                request.body() == "username=raw%GG", "non-form body was decoded");
    });
    run("empty POST form completes without business execution", [] {
        CheckEverySplit(FormRequest(""), "/login");
    });
    run("pipeline preserves the next request and resets form fields", [] {
        const string next = "GET /video HTTP/1.1\r\nConnection: close\r\n\r\n";
        Buffer buffer;
        buffer.Append(FormRequest("username=first&password=secret") + next);
        HttpRequest request;
        Require(request.parse(buffer) == HttpRequest::ParseRes::Complete &&
                buffer.ReadableBytes() == next.size(), "first parse consumed the next request");
        request.Init();
        Require(request.parse(buffer) == HttpRequest::ParseRes::Complete && request.path() == "/video" &&
                request.GetPost("username").empty() && !request.IsKeepAlive(), "request state leaked across pipeline");
    });
    run("Connection tokens are merged and close takes priority", [] {
        Buffer buffer;
        buffer.Append("GET / HTTP/1.1\r\nConnection: Upgrade, KEEP-ALIVE\r\nConnection: close\r\n\r\n");
        HttpRequest request;
        Require(request.parse(buffer) == HttpRequest::ParseRes::Complete && !request.IsKeepAlive(), "close lost priority");
    });
    run("complete keep-alive token is recognized", [] {
        Buffer buffer;
        buffer.Append("GET / HTTP/1.1\r\nConnection: Upgrade, KEEP-ALIVE\r\n\r\n");
        HttpRequest request;
        Require(request.parse(buffer) == HttpRequest::ParseRes::Complete && request.IsKeepAlive(), "keep-alive not recognized");
    });
    run("partial header remains buffered", [] {
        Buffer buffer;
        buffer.Append("GET / HTTP/1.1\r\nHost: local");
        HttpRequest request;
        Require(request.parse(buffer) == HttpRequest::ParseRes::NeedMore &&
                string(buffer.Peek(), buffer.ReadableBytes()) == "Host: local", "partial header was consumed");
    });

    const vector<pair<string, string>> invalid = {
        {"invalid request line", "INVALID\r\n\r\n"},
        {"duplicate Content-Length", "GET / HTTP/1.1\r\nContent-Length: 0\r\ncontent-length: 0\r\n\r\n"},
        {"negative Content-Length", "GET / HTTP/1.1\r\nContent-Length: -1\r\n\r\n"},
        {"signed Content-Length", "GET / HTTP/1.1\r\nContent-Length: +1\r\n\r\n"},
        {"empty Content-Length", "GET / HTTP/1.1\r\nContent-Length:\r\n\r\n"},
        {"overflow Content-Length", "GET / HTTP/1.1\r\nContent-Length: 999999999999999999999999999\r\n\r\n"},
        {"junk Content-Length", "GET / HTTP/1.1\r\nContent-Length: 1x\r\n\r\n"},
        {"unsupported Transfer-Encoding", "GET / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"},
        {"invalid header name", "GET / HTTP/1.1\r\nBad Header: value\r\n\r\n"},
        {"control character in header", "GET / HTTP/1.1\r\nX-Test: a\x01" "b\r\n\r\n"},
        {"DEL in header", "GET / HTTP/1.1\r\nX-Test: a\x7f" "b\r\n\r\n"},
        {"trailing form percent", FormRequest("password=%")},
        {"short form percent", FormRequest("password=%2")},
        {"non-hex form percent", FormRequest("password=%GG")},
    };
    for (const auto& [name, wire] : invalid) {
        run(name, [&] {
            Buffer buffer;
            buffer.Append(wire);
            HttpRequest request;
            Require(request.parse(buffer) == HttpRequest::ParseRes::BadRequest, "invalid request was accepted");
        });
    }
    cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
