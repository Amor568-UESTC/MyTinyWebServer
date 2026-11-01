#include "../module.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std;

namespace {

void Require(bool condition, const string& message) {
    if (!condition) {
        throw runtime_error(message);
    }
}

void ExpectError(const function<void()>& operation, const string& expected) {
    try {
        operation();
    } catch (const exception& error) {
        Require(string(error.what()).find(expected) != string::npos,
                "Unexpected error: " + string(error.what()));
        return;
    }
    throw runtime_error("Expected an exception: " + expected);
}

string ReadTrace(const filesystem::path& path) {
    ifstream file(path, ios::binary);
    return string(istreambuf_iterator<char>(file), istreambuf_iterator<char>());
}

} //namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: moduleTest FIXTURE_DIRECTORY\n";
        return 1;
    }
    const auto directory = filesystem::absolute(argv[1]);
    const auto tracePath = directory / "lifecycle.log";
    if (setenv("TINYWEB_PLUGIN_TEST_TRACE", tracePath.c_str(), 1) != 0) {
        cerr << "Cannot set fixture trace path\n";
        return 1;
    }

    size_t passed = 0;
    auto run = [&](const string& name, const function<void()>& operation) {
        ofstream(tracePath, ios::trunc).close();
        operation();
        ++passed;
        cout << "PASS " << name << '\n';
    };
    auto load = [&](const string& name) {
        return Module::Load((directory / (name + ".so")).string());
    };

    try {
        run("real_plugin_and_body_ownership", [&] {
            string body;
            {
                const auto module = load("business");
                body = module->Handle("/api/version");
            }
            Require(body == "TinyWebServer v1\n", "Unexpected version body");
        });
        run("empty_path", [&] {
            ExpectError([] { Module::Load(""); }, "nonempty");
        });
        run("nul_in_library_path", [&] {
            const string path = (directory / "business.so").string() + string("\0suffix", 7);
            ExpectError([&] { Module::Load(path); }, "NUL");
        });
        run("missing_file", [&] {
            ExpectError([&] { load("missing"); }, "dlopen failed");
        });

        const vector<pair<string, string>> loadFailures = {
            {"noEntry", "dlsym failed"}, {"nullApi", "API is null"},
            {"badAbi", "mismatch"}, {"badSize", "mismatch"},
            {"missingCreate", "missing functions"}, {"missingHandle", "missing functions"},
            {"missingDestroy", "missing functions"}
        };
        for (const auto& [name, error] : loadFailures) {
            run(name, [&] {
                ExpectError([&] { load(name); }, error);
                Require(ReadTrace(tracePath) == "U", "Failed load did not close its library");
            });
        }
        for (const string name : {"createFailure", "nullInstance", "partialCreateFailure"}) {
            run(name, [&] {
                ExpectError([&] { load(name); }, "create failed");
                const string expected = name == "partialCreateFailure" ? "CDU" : "CU";
                Require(ReadTrace(tracePath) == expected, "Incorrect initialization rollback");
            });
        }
        for (const string name : {"handleFailure", "oversizedBody", "ignoredAppendFailure",
                                  "invalidBody", "ignoredInvalidBody"}) {
            run(name, [&] {
                {
                    const auto module = load(name);
                    ExpectError([&] { module->Handle("/api/version"); }, "handle failed");
                    Require(ReadTrace(tracePath) == "C", "Module released during its call");
                }
                Require(ReadTrace(tracePath) == "CDU", "Incorrect destruction order");
            });
        }
        run("empty_body", [&] {
            Require(load("emptyBody")->Handle("/api/version").empty(), "Expected an empty body");
        });
        run("binary_body", [&] {
            Require(load("binaryBody")->Handle("/api/version") == string("A\0B", 3),
                    "Binary body was truncated");
        });
        run("chunked_body", [&] {
            Require(load("chunkedBody")->Handle("/api/version") == "TinyWebServer v1\n",
                    "Append chunks were not combined");
        });
        run("exact_body_limit", [&] {
            Require(load("exactLimit")->Handle("/api/version") == string(64 * 1024, 'x'),
                    "Exact response limit was rejected");
        });
        run("shared_ownership_and_destruction_order", [&] {
            auto module = load("valid");
            auto retained = module;
            module.reset();
            Require(ReadTrace(tracePath) == "C", "Shared module released too soon");
            Require(retained->Handle("/api/version") == "TinyWebServer v1\n", "Retained module failed");
            retained.reset();
            Require(ReadTrace(tracePath) == "CDU", "Instance must be destroyed before library close");
        });
        run("unsupported_route", [&] {
            const auto module = load("business");
            ExpectError([&] { module->Handle("/missing"); }, "handle failed");
        });
        run("concurrent_calls_have_separate_response_buffers", [&] {
            const auto module = load("business");
            atomic<bool> success = true;
            vector<thread> threads;
            for (int i = 0; i < 4; ++i) {
                threads.emplace_back([module, &success] {
                    try {
                        for (int j = 0; j < 100; ++j) {
                            if (module->Handle("/api/version") != "TinyWebServer v1\n") {
                                success.store(false);
                            }
                        }
                    } catch (...) {
                        success.store(false);
                    }
                });
            }
            for (auto& thread : threads) {
                thread.join();
            }
            Require(success.load(), "Concurrent response data was corrupted");
        });
    } catch (const exception& error) {
        cerr << "FAIL after " << passed << " checks: " << error.what() << '\n';
        unsetenv("TINYWEB_PLUGIN_TEST_TRACE");
        return 1;
    }
    unsetenv("TINYWEB_PLUGIN_TEST_TRACE");
    cout << passed << " module checks passed\n";
    return 0;
}
