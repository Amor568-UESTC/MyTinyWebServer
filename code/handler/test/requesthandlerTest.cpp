#include "../requesthandler.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace std;

namespace {
int verificationCalls = 0;
bool accepted = false;
bool login = false;
string user;
string password;

void Require(bool condition, const char* message) {
    if (!condition) {
        throw runtime_error(message);
    }
}

bool Verify(const string& name, const string& secret, bool isLogin) {
    ++verificationCalls;
    user = name;
    password = secret;
    login = isLogin;
    return accepted;
}

HttpRequest Parse(const string& path, const string& method = "GET", const string& body = "",
                  const string& type = "application/x-www-form-urlencoded") {
    HttpRequest request;
    Buffer buffer;
    buffer.Append(method + " " + path + " HTTP/1.1\r\nContent-Type: " + type +
                  "\r\nContent-Length: " + to_string(body.size()) + "\r\n\r\n" + body);
    Require(request.parse(buffer) == HttpRequest::ParseRes::Complete, "request fixture failed to parse");
    return request;
}
}

int main() {
    int passed = 0;
    int failed = 0;
    const RequestHandler handler(Verify);
    const auto run = [&](const string& name, const function<void()>& test) {
        verificationCalls = 0;
        try {
            test();
            ++passed;
            cout << "PASS " << name << '\n';
        } catch (const exception& error) {
            ++failed;
            cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };
    const vector<pair<string, string>> routes = {
        {"/", "/index.html"}, {"/index", "/index.html"}, {"/login", "/login.html"},
        {"/register", "/register.html"}, {"/welcome", "/welcome.html"}, {"/video", "/video.html"},
        {"/picture", "/picture.html"}, {"/css/style.css", "/css/style.css"},
        {"/missing", "/missing"}, {"/../outside", "/../outside"},
    };
    for (const auto& [path, expected] : routes) {
        run("file route " + path, [&] {
            const auto request = Parse(path);
            const auto result = handler.Handle(request);
            Require(result.kind == HandlerResult::Kind::File && result.path == expected && result.status == 200,
                    "file route changed");
            Require(request.path() == path && verificationCalls == 0, "routing mutated parser data or accessed login");
        });
    }
    run("version endpoint returns owned text without verification", [&] {
        const auto result = handler.Handle(Parse("/api/version"));
        Require(result.kind == HandlerResult::Kind::Text && result.status == 200 && result.path.empty() &&
                result.body == "TinyWebServer v1\n" && result.contentType == "text/plain; charset=utf-8" &&
                verificationCalls == 0, "version response is incorrect");
    });
    for (const string path : {"/login", "/login.html", "/register", "/register.html"}) {
        for (const bool success : {false, true}) {
            run(path + (success ? " success" : " failure"), [&] {
                accepted = success;
                const auto request = Parse(path, "POST", "username=alice&password=a%2Bb");
                Require(verificationCalls == 0, "parsing executed business logic");
                const auto result = handler.Handle(request);
                Require(verificationCalls == 1 && user == "alice" && password == "a+b" &&
                        login == path.starts_with("/login"), "wrong verification arguments");
                Require(result.path == (success ? "/welcome.html" : "/error.html") && request.path() == path,
                        "login result changed the request or selected the wrong page");
            });
        }
    }
    run("non-form POST keeps its static page", [&] {
        const auto result = handler.Handle(Parse("/login", "POST", "{}", "application/json"));
        Require(result.path == "/login.html" && verificationCalls == 0, "non-form POST attempted verification");
    });
    run("unrelated form POST does not verify users", [&] {
        const auto result = handler.Handle(Parse("/submit", "POST", "username=alice&password=secret"));
        Require(result.path == "/submit" && verificationCalls == 0, "unrelated route attempted verification");
    });
    run("null verification function is rejected", [] {
        bool rejected = false;
        try {
            RequestHandler invalid(nullptr);
        } catch (const invalid_argument&) {
            rejected = true;
        }
        Require(rejected, "null verification function was accepted");
    });
    cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
