#include "../pluginmanager.h"
#include "../pluginreloader.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std;
using namespace chrono_literals;

namespace {
void Require(bool condition, const char* message) {
    if (!condition) {
        throw runtime_error(message);
    }
}

struct Gate {
    mutex lock;
    condition_variable condition;
    bool entered = false;
    bool released = false;

    static void Wait(void* user) {
        auto& gate = *static_cast<Gate*>(user);
        unique_lock<mutex> locker(gate.lock);
        gate.entered = true;
        gate.condition.notify_all();
        gate.condition.wait(locker, [&] { return gate.released; });
    }

    bool Entered() {
        unique_lock<mutex> locker(lock);
        return condition.wait_for(locker, 3s, [&] { return entered; });
    }

    void Release() {
        lock_guard<mutex> locker(lock);
        released = true;
        condition.notify_all();
    }
};

class Control {
public:
    explicit Control(const string& path, Gate& gate) {
        handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        Require(handle_ != nullptr, "cannot open controlled fixture");
        dlerror();
        auto setHook = reinterpret_cast<void (*)(void (*)(void*), void*)>(dlsym(handle_, "fixture_set_hook"));
        const char* error = dlerror();
        if (error != nullptr || setHook == nullptr) {
            dlclose(handle_);
            throw runtime_error("cannot install controlled fixture hook");
        }
        setHook(Gate::Wait, &gate);
    }
    ~Control() { dlclose(handle_); }
private:
    void* handle_ = nullptr;
};

void ExpectFailure(PluginManager& manager, const string& path) {
    const auto before = manager.Snapshot();
    bool failed = false;
    try {
        manager.Reload(path);
    } catch (const exception&) {
        failed = true;
    }
    Require(failed && manager.Snapshot() == before, "failed reload replaced the current module");
}
} //namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        return 2;
    }
    const auto directory = filesystem::absolute(argv[1]);
    const auto tracePath = directory / "manager-lifecycle.log";
    setenv("TINYWEB_PLUGIN_TEST_TRACE", tracePath.c_str(), 1);
    const auto path = [&](const string& name) { return (directory / (name + ".so")).string(); };
    size_t passed = 0;
    const auto run = [&](const char* name, const function<void()>& operation) {
        ofstream(tracePath, ios::trunc).close();
        operation();
        ++passed;
        cout << "PASS " << name << '\n';
    };
    try {
        run("manager starts without a module", [] {
            PluginManager manager;
            Require(manager.Snapshot() == nullptr, "unexpected initial module");
        });
        run("new snapshot uses v2 while retained snapshot uses v1", [&] {
            PluginManager manager;
            manager.Reload(path("valid"));
            auto old = manager.Snapshot();
            weak_ptr<Module> observed = old;
            manager.Reload(path("versionTwo"));
            Require(manager.Snapshot()->Handle("/api/version") == "TinyWebServer v2\n", "new snapshot is stale");
            Require(old->Handle("/api/version") == "TinyWebServer v1\n", "old snapshot was changed");
            Require(!observed.expired(), "old module released too soon");
            old.reset();
            Require(observed.expired(), "old module was retained after its last snapshot");
        });
        for (const string name : {"missing", "badAbi", "badSize", "createFailure", "noEntry"}) {
            run(("failed reload preserves current module: " + name).c_str(), [&] {
                PluginManager manager;
                manager.Reload(path("valid"));
                ExpectFailure(manager, path(name));
                Require(manager.Snapshot()->Handle("/api/version") == "TinyWebServer v1\n", "original module failed");
            });
        }
        run("in-flight plugin call completes before old instance is destroyed", [&] {
            Gate gate;
            Control control(path("blockingHandle"), gate);
            PluginManager manager;
            manager.Reload(path("blockingHandle"));
            weak_ptr<Module> old = manager.Snapshot();
            string body;
            thread request([module = manager.Snapshot(), &body] { body = module->Handle("/api/version"); });
            const bool entered = gate.Entered();
            try {
                Require(entered, "plugin call did not reach the blocking point");
                manager.Reload(path("versionTwo"));
                Require(!old.expired(), "active plugin instance was destroyed");
                Require(manager.Snapshot()->Handle("/api/version") == "TinyWebServer v2\n", "new calls cannot use v2");
            } catch (...) {
                gate.Release();
                request.join();
                throw;
            }
            gate.Release();
            request.join();
            Require(body == "TinyWebServer v1\n" && old.expired(), "old call or its lifetime was corrupted");
            ifstream trace(tracePath, ios::binary);
            const string events(istreambuf_iterator<char>(trace), {});
            Require(events == "CCD", "old instance was not destroyed after the active call");
        });
        run("slow initialization does not block existing snapshots", [&] {
            Gate gate;
            Control control(path("blockingCreate"), gate);
            PluginManager manager;
            manager.Reload(path("valid"));
            const auto before = manager.Snapshot();
            exception_ptr failure;
            thread reload([&] {
                try {
                    manager.Reload(path("blockingCreate"));
                } catch (...) {
                    failure = current_exception();
                }
            });
            const bool entered = gate.Entered();
            const bool usable = manager.Snapshot() == before && before->Handle("/api/version") == "TinyWebServer v1\n";
            gate.Release();
            reload.join();
            Require(entered && usable && !failure && manager.Snapshot() != before, "slow load blocked or broke publication");
        });
        run("concurrent requests and reloads retain valid module snapshots", [&] {
            auto manager = make_shared<PluginManager>();
            manager->Reload(path("valid"));
            atomic<bool> success = true;
            vector<thread> workers;
            for (int i = 0; i < 4; ++i) {
                workers.emplace_back([&] {
                    for (int j = 0; j < 200; ++j) {
                        const auto module = manager->Snapshot();
                        const auto body = module->Handle("/api/version");
                        if (body != "TinyWebServer v1\n" && body != "TinyWebServer v2\n") {
                            success.store(false);
                        }
                    }
                });
            }
            for (int i = 0; i < 2; ++i) {
                workers.emplace_back([&, i] {
                    try {
                        for (int j = 0; j < 10; ++j) {
                            manager->Reload(path((j + i) % 2 == 0 ? "versionTwo" : "valid"));
                        }
                    } catch (...) {
                        success.store(false);
                    }
                });
            }
            for (auto& worker : workers) {
                worker.join();
            }
            Require(success.load(), "concurrent snapshots returned invalid data");
        });
        run("reload executor rejects an empty callback", [] {
            bool rejected = false;
            try {
                PluginReloader reloader(nullptr);
            } catch (const invalid_argument&) {
                rejected = true;
            }
            Require(rejected, "empty reload callback was accepted");
        });
        run("reload executor survives callback failure", [] {
            atomic<int> calls = 0;
            promise<void> first;
            promise<void> second;
            auto firstDone = first.get_future();
            auto secondDone = second.get_future();
            PluginReloader reloader([&] {
                if (++calls == 1) {
                    first.set_value();
                    throw runtime_error("expected test reload failure");
                }
                second.set_value();
            });
            Require(reloader.Request(), "first reload request rejected");
            Require(firstDone.wait_for(3s) == future_status::ready, "first callback did not run");
            Require(reloader.Request(), "second reload request rejected");
            Require(secondDone.wait_for(3s) == future_status::ready, "executor stopped after an error");
        });
        run("busy reload executor coalesces pending requests and joins on destruction", [] {
            Gate gate;
            atomic<int> calls = 0;
            promise<void> second;
            auto done = second.get_future();
            {
                PluginReloader reloader([&] {
                    if (++calls == 1) {
                        Gate::Wait(&gate);
                    } else {
                        second.set_value();
                    }
                });
                reloader.Request();
                const bool entered = gate.Entered();
                for (int i = 0; i < 20; ++i) {
                    reloader.Request();
                }
                gate.Release();
                Require(entered && done.wait_for(3s) == future_status::ready, "queued reload did not finish");
            }
            Require(calls.load() == 2, "pending reload requests were not coalesced");
        });
        run("reload executor destructor waits for the active callback", [] {
            Gate gate;
            auto reloader = make_unique<PluginReloader>([&] { Gate::Wait(&gate); });
            reloader->Request();
            const bool entered = gate.Entered();
            promise<void> started;
            auto destructionStarted = started.get_future();
            auto destroyed = async(launch::async, [owned = std::move(reloader), &started]() mutable {
                started.set_value();
                owned.reset();
            });
            destructionStarted.wait();
            const bool waiting = destroyed.wait_for(20ms) == future_status::timeout;
            gate.Release();
            destroyed.get();
            Require(entered && waiting, "reload executor did not wait for its active callback");
        });
    } catch (const exception& error) {
        cerr << "FAIL after " << passed << " manager checks: " << error.what() << '\n';
        unsetenv("TINYWEB_PLUGIN_TEST_TRACE");
        return 1;
    }
    unsetenv("TINYWEB_PLUGIN_TEST_TRACE");
    cout << passed << " plugin manager checks passed\n";
    return 0;
}
