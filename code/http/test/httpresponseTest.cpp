#include "../httpresponse.h"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
bool failMapping = false;

struct TestSite {
    std::filesystem::path root;

    TestSite() {
        char name[] = "/tmp/tinyweb-httpresponse-XXXXXX";
        char* directory = mkdtemp(name);
        if (!directory) {
            throw std::runtime_error("mkdtemp failed");
        }
        root = directory;
    }

    ~TestSite() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    void Write(const std::string& name, const std::string& body) const {
        const auto path = root / name;
        std::ofstream out(path, std::ios::binary);
        out << body;
        out.close();
        if (!out || chmod(path.c_str(), 0644) != 0) {
            throw std::runtime_error("cannot prepare test file");
        }
    }
};

size_t FdCount() {
    return std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
                         std::filesystem::directory_iterator{});
}

size_t MapCount(const std::filesystem::path& path) {
    std::ifstream in("/proc/self/maps");
    std::string line;
    size_t count = 0;
    while (std::getline(in, line)) {
        count += line.find(path.string()) != std::string::npos;
    }
    return count;
}
}

// 仅在测试二进制中用 --wrap=mmap 注入映射失败，不影响生产代码。
extern "C" void* __real_mmap(void*, size_t, int, int, int, off_t);
extern "C" void* __wrap_mmap(void* address, size_t length, int protection,
                            int flags, int fd, off_t offset) {
    if (failMapping) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    return __real_mmap(address, length, protection, flags, fd, offset);
}

int main() {
    try {
        TestSite site;
        site.Write("normal.txt", "normal-content");
        site.Write("empty.txt", "");
        site.Write("image.jpg", "IMAGE_BYTES");
        HttpResponse response;
        Buffer buffer;
        int failures = 0;
        auto check = [&](bool ok, const char* name) {
            std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
            failures += !ok;
        };
        auto run = [&](const char* name, std::string path, int status,
                       bool injectFailure, const std::string& expectedBody) {
            const size_t before = FdCount();
            buffer.RetrieveAll();
            response.Init(site.root.string(), path, true, 200);
            failMapping = injectFailure;
            response.MakeResponse(buffer);
            failMapping = false;

            const std::string text = buffer.RetrieveAllToString();
            const size_t end = text.find("\r\n\r\n");
            const size_t field = text.find("Content-length: ");
            bool ok = end != std::string::npos && field != std::string::npos &&
                      field < end && response.Code() == status &&
                      text.starts_with("HTTP/1.1 " + std::to_string(status) + " ") &&
                      before == FdCount();
            ok = ok && text.find("Connection: keep-alive\r\n") != std::string::npos &&
                 text.find("keep-alive:") == std::string::npos &&
                 text.find("Keep-Alive:") == std::string::npos;
            std::string body = end == std::string::npos ? "" : text.substr(end + 4);
            if (response.File()) {
                body.append(response.File(), response.FileLen());
            }
            if (ok) {
                ok = std::stoull(text.substr(field + 16)) == body.size() &&
                     text.find("Content-length: ", field + 1) == std::string::npos;
            }
            if (status >= 400 && !response.File()) {
                ok = ok && response.FileLen() == 0 &&
                     text.find("Content-type: text/html; charset=utf-8\r\n") !=
                         std::string::npos &&
                     body.find(std::to_string(status) + " : ") != std::string::npos;
            } else {
                ok = ok && body == expectedBody;
                if (expectedBody.empty()) {
                    ok = ok && !response.File() && response.FileLen() == 0;
                }
            }
            if (injectFailure) {
                ok = ok && status == 500 &&
                     text.starts_with("HTTP/1.1 500 Internal Server Error\r\n") &&
                     !response.File() && response.FileLen() == 0;
            }
            check(ok, name);
        };

        run("normal file", "/normal.txt", 200, false, "normal-content");
        run("empty file", "/empty.txt", 200, false, "");
        run("mmap failure returns 500 and clears mapping", "/normal.txt", 500, true, "");
        run("image mmap failure uses HTML error type", "/image.jpg", 500, true, "");
        run("response recovers after mmap failure", "/normal.txt", 200, false, "normal-content");

        site.Write("404.html", "NOT_FOUND_PAGE");
        run("mapped 404 page", "/missing", 404, false, "NOT_FOUND_PAGE");
        std::filesystem::remove(site.root / "404.html");
        run("inline 404 fallback", "/missing", 404, false, "");

        const size_t before = FdCount();
        std::string path = "/normal.txt";
        for (int i = 0; i < 10; ++i) {
            buffer.RetrieveAll();
            response.Init(site.root.string(), path, true, 200);
            response.MakeResponse(buffer);
        }
        check(MapCount(site.root / "normal.txt") == 1 && before == FdCount(),
              "repeated responses keep one mapping and no open fd");
        response.UnmapFile();
        check(MapCount(site.root / "normal.txt") == 0, "unmap releases final mapping");
        buffer.RetrieveAll();
        response.Init(site.root.string(), path, false, 200);
        response.MakeResponse(buffer);
        const auto closedResponse = buffer.RetrieveAllToString();
        check(closedResponse.find("Connection: close\r\n") != std::string::npos,
              "closed response advertises Connection close");
        check(closedResponse.find("timeout=") == std::string::npos &&
              closedResponse.find("max=") == std::string::npos,
              "response does not advertise unsupported keep-alive limits");

        buffer.RetrieveAll();
        std::string body = "version-v1\n";
        response.MakeTextResponse(buffer, body, "text/plain; charset=utf-8", true);
        body.assign("changed after response generation");
        const auto textResponse = buffer.RetrieveAllToString();
        check(!response.File() && response.FileLen() == 0 && MapCount(site.root / "normal.txt") == 0 &&
              textResponse.ends_with("\r\n\r\nversion-v1\n") &&
              textResponse.find("Content-length: 11\r\n") != std::string::npos,
              "text response owns its body and releases the previous file mapping");

        const std::string binaryBody("a\0b", 3);
        response.MakeTextResponse(buffer, binaryBody, "application/octet-stream", false);
        const auto binaryResponse = buffer.RetrieveAllToString();
        check(binaryResponse.ends_with(binaryBody) &&
              binaryResponse.find("Content-length: 3\r\n") != std::string::npos,
              "inline response length includes embedded NUL bytes");

        response.MakeTextResponse(buffer, "", "text/plain", false, 500);
        const auto emptyResponse = buffer.RetrieveAllToString();
        check(emptyResponse.starts_with("HTTP/1.1 500 Internal Server Error\r\n") &&
              emptyResponse.find("Content-length: 0\r\n\r\n") != std::string::npos,
              "empty inline error response preserves its status and length");
        bool rejected = false;
        try {
            response.MakeTextResponse(buffer, "body", "text/plain\r\nInjected: yes", false);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected && buffer.ReadableBytes() == 0, "invalid content type is rejected before writing headers");
        run("file response recovers after an inline response", "/normal.txt", 200, false, "normal-content");
        buffer.RetrieveAll();
        response.Init(site.root.string(), "/400.html", false, 400);
        response.MakeResponse(buffer);
        check(buffer.RetrieveAllToString().starts_with("HTTP/1.1 400 Bad Request\r\n") &&
              !response.File() && response.FileLen() == 0,
              "missing error page preserves the requested 400 status");
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
