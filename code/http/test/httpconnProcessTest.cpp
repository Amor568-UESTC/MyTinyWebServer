#include "../httpconn.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace std;

namespace {
void Require(bool condition, const char* message) {
    if (!condition) {
        throw runtime_error(message);
    }
}

struct Site {
    filesystem::path root;
    string documentRoot;

    Site() {
        char pattern[] = "/tmp/tinyweb-process-test-XXXXXX";
        const char* directory = mkdtemp(pattern);
        Require(directory != nullptr, "mkdtemp failed");
        root = directory;
        filesystem::create_directory(root / "site");
        documentRoot = (root / "site").string();
        ofstream(root / "site/index.html") << "INDEX_PAGE";
        ofstream(root / "outside") << "PRIVATE_FILE";
        chmod((root / "site/index.html").c_str(), 0644);
        HttpConn::srcDir = documentRoot.c_str();
    }

    ~Site() {
        HttpConn::srcDir = nullptr;
        error_code ignored;
        filesystem::remove_all(root, ignored);
    }
};

class Connection : public HttpConn {
public:
    bool readClosed = false;
    bool IsReadClose() const override { return readClosed; }

    void Feed(const string& wire) { readBuff_.Append(wire); }

    string TakeResponse() {
        string wire(writeBuff_.Peek(), writeBuff_.ReadableBytes());
        if (iovCnt_ == 2) {
            wire.append(static_cast<const char*>(iov_[1].iov_base), iov_[1].iov_len);
        }
        //模拟完整发送后由连接管理者再次调用process
        writeBuff_.RetrieveAll();
        iov_[0] = {};
        iov_[1] = {};
        iovCnt_ = 0;
        return wire;
    }
};

void CheckResponse(const string& wire, int status, const string& body) {
    const auto end = wire.find("\r\n\r\n");
    const auto length = wire.find("Content-length: ");
    Require(wire.starts_with("HTTP/1.1 " + to_string(status) + " ") &&
            end != string::npos && length < end, "invalid response framing or status");
    Require(wire.substr(end + 4) == body && stoull(wire.substr(length + 16)) == body.size(),
            "wrong response body or length");
}

string Get(const string& path, bool keepAlive = false) {
    return "GET " + path + " HTTP/1.1\r\nConnection: " + (keepAlive ? "keep-alive" : "close") + "\r\n\r\n";
}
}

int main() {
    int passed = 0;
    int failed = 0;
    Site site;
    const auto run = [&](const char* name, const function<void()>& test) {
        try {
            test();
            ++passed;
            cout << "PASS " << name << '\n';
        } catch (const exception& error) {
            ++failed;
            cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };
    run("root route still produces a mapped file response", [] {
        Connection connection;
        connection.Feed(Get("/"));
        Require(connection.process(), "complete request did not produce a response");
        CheckResponse(connection.TakeResponse(), 200, "INDEX_PAGE");
    });
    run("version endpoint copies text into the response buffer", [] {
        Connection connection;
        connection.Feed(Get("/api/version"));
        Require(connection.process(), "version request did not produce a response");
        const auto wire = connection.TakeResponse();
        CheckResponse(wire, 200, "TinyWebServer v1\n");
        Require(wire.find("Content-type: text/plain; charset=utf-8\r\n") != string::npos, "wrong version content type");
    });
    run("partial version request waits for remaining input", [] {
        Connection connection;
        connection.Feed("GET /api/ver");
        Require(!connection.process() && connection.ToWriteBytes() == 0, "partial request produced output");
        connection.Feed("sion HTTP/1.1\r\n\r\n");
        Require(connection.process(), "fragmented version request did not complete");
        CheckResponse(connection.TakeResponse(), 200, "TinyWebServer v1\n");
    });
    run("file then text pipeline resets mapping and keep-alive", [] {
        Connection connection;
        connection.Feed(Get("/", true) + Get("/api/version"));
        Require(connection.process() && connection.IsKeepAlive(), "first response did not keep the connection");
        CheckResponse(connection.TakeResponse(), 200, "INDEX_PAGE");
        Require(connection.process() && !connection.IsKeepAlive(), "second response ignored Connection close");
        CheckResponse(connection.TakeResponse(), 200, "TinyWebServer v1\n");
    });
    run("text then file pipeline uses the correct response kind", [] {
        Connection connection;
        connection.Feed(Get("/api/version", true) + Get("/"));
        Require(connection.process(), "first pipeline response missing");
        CheckResponse(connection.TakeResponse(), 200, "TinyWebServer v1\n");
        Require(connection.process(), "second pipeline response missing");
        CheckResponse(connection.TakeResponse(), 200, "INDEX_PAGE");
    });
    run("read-closed pipeline finishes its buffered requests", [] {
        Connection connection;
        connection.readClosed = true;
        connection.Feed(Get("/api/version", true) + Get("/api/version", true));
        Require(connection.process() && connection.IsKeepAlive(), "buffered next request was discarded");
        connection.TakeResponse();
        Require(connection.process() && !connection.IsKeepAlive(), "last response kept a read-closed connection");
        CheckResponse(connection.TakeResponse(), 200, "TinyWebServer v1\n");
    });
    run("invalid form returns 400 before reaching user verification", [] {
        Connection connection;
        connection.Feed("POST /login HTTP/1.1\r\nContent-Type: application/x-www-form-urlencoded\r\n"
                        "Content-Length: 3\r\nConnection: keep-alive\r\n\r\nx=%");
        Require(connection.process() && !connection.IsKeepAlive(), "invalid form did not close the response");
        Require(connection.TakeResponse().starts_with("HTTP/1.1 400 Bad Request\r\n"), "invalid form did not produce status 400");
    });
    run("business file result still respects the document root", [] {
        Connection connection;
        connection.Feed(Get("/../outside"));
        Require(connection.process(), "traversal response missing");
        const auto wire = connection.TakeResponse();
        Require(wire.starts_with("HTTP/1.1 403 Forbidden\r\n") && wire.find("PRIVATE_FILE") == string::npos,
                "handler bypassed the safe file opener");
    });
    cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
