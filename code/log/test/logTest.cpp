#include "../log.h"

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <sys/wait.h>
#include <vector>

using namespace std;

namespace {
void Require(bool condition, const char* message) {
    if (!condition) {
        throw runtime_error(message);
    }
}

size_t FdCount() {
    return distance(filesystem::directory_iterator("/proc/self/fd"), filesystem::directory_iterator{});
}

void RunChild(const string& name, const filesystem::path& root) {
    auto* log = Log::Instance();
    const string directory = (root / "logs").string();
    if (name == "disabled") {
        LOG_ERROR("disabled-marker");
        Require(!log->IsOpen(), "uninitialized logger is enabled");
        return;
    }
    if (name == "queue_close") {
        BlockDeque<int> queue(1);
        queue.push_back(42);
        queue.Close();
        queue.Close();
        queue.push_back(7);
        int item = 0;
        Require(queue.pop(item) && item == 42, "close discarded queued data");
        Require(!queue.pop(item), "closed empty queue did not stop");
        Require(!queue.pop(item, 1), "timed pop ignored closed state");
        BlockDeque<int> empty(1);
        empty.Close();
        Require(!empty.pop(item), "close before pop lost its wakeup");
        return;
    }
    if (name == "init_failure") {
        const auto blocked = root / "blocked";
        ofstream(blocked) << "not a directory";
        const size_t before = FdCount();
        bool rejected = false;
        try {
            log->init(1, blocked.c_str(), ".log", 2);
        } catch (const exception&) {
            rejected = true;
        }
        Require(rejected && !log->IsOpen() && before == FdCount(), "failed log init leaked or published state");
    }
    const bool asynchronous = name == "async" || name == "concurrent";
    log->init(1, directory.c_str(), ".log", asynchronous ? 2 : 0);
    Require(log->IsOpen(), "logger did not become available");
    Require(log->GetQueueCapacity() == (asynchronous ? 2u : 0u), "queue capacity differs from configuration");
    if (name == "rotation") {
        for (int i = 0; i < 50001; ++i) {
            log->write(1, "rotation-%d", i);
        }
    } else if (name == "concurrent") {
        vector<thread> writers;
        for (int i = 0; i < 4; ++i) {
            writers.emplace_back([i] {
                for (int j = 0; j < 200; ++j) {
                    LOG_INFO("token-%d-%d", i, j);
                }
            });
        }
        for (auto& writer : writers) {
            writer.join();
        }
    } else if (name == "long_record") {
        const string body(8192, 'x');
        LOG_INFO("long:%s:end", body.c_str());
    } else {
        LOG_DEBUG("hidden-debug-marker");
        LOG_INFO("info-marker");
        LOG_WARN("warn-marker");
        LOG_ERROR("error-marker");
        if (asynchronous) {
            for (int i = 0; i < 1000; ++i) {
                LOG_INFO("queued-%d", i);
            }
        }
    }
}
}

int main(int argc, char* argv[]) {
    const vector<string> cases = {"disabled", "sync", "async", "init_failure",
                                   "queue_close", "concurrent", "long_record", "rotation"};
    if (argc != 2) {
        return 2;
    }
    const string name(argv[1]);
    if (name == "--list") {
        for (const auto& item : cases) {
            cout << item << '\n';
        }
        return 0;
    }
    filesystem::path root;
    try {
        Require(find(cases.begin(), cases.end(), name) != cases.end(), "unknown test case");
        char pattern[] = "/tmp/tinyweb-log-test-XXXXXX";
        const char* directory = mkdtemp(pattern);
        Require(directory != nullptr, "mkdtemp failed");
        root = directory;
        const pid_t child = fork();
        Require(child >= 0, "fork failed");
        if (child == 0) {
            try {
                RunChild(name, root);
                //正常退出触发日志单例析构,父进程随后检查排空后的文件
                exit(0);
            } catch (const exception& error) {
                cerr << error.what() << '\n';
                exit(1);
            }
        }
        int status = 0;
        Require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "logger child failed");
        string content;
        size_t files = 0;
        if (filesystem::exists(root / "logs")) {
            for (const auto& entry : filesystem::directory_iterator(root / "logs")) {
                ifstream input(entry.path());
                content.append(istreambuf_iterator<char>(input), istreambuf_iterator<char>());
                ++files;
            }
        }
        if (name == "disabled" || name == "queue_close") {
            Require(files == 0, "disabled logger created files");
        } else if (name == "rotation") {
            Require(files == 2 && count(content.begin(), content.end(), '\n') == 50001,
                    "rotation lost records or ignored its line limit");
        } else if (name == "concurrent") {
            Require(count(content.begin(), content.end(), '\n') == 800, "concurrent logging lost records");
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 200; ++j) {
                    const string token = "token-" + to_string(i) + "-" + to_string(j) + "\n";
                    Require(content.find(token) != string::npos, "a concurrent record was corrupted");
                }
            }
        } else if (name == "long_record") {
            Require(content.find("long:" + string(8192, 'x') + ":end\n") != string::npos,
                    "long record was truncated");
        } else {
            Require(content.find("hidden-debug-marker") == string::npos, "INFO threshold leaked DEBUG");
            for (const char* marker : {"info-marker", "warn-marker", "error-marker"}) {
                Require(content.find(marker) != string::npos, "INFO threshold hid a required level");
            }
            if (name == "async") {
                Require(count(content.begin(), content.end(), '\n') == 1003,
                        "async shutdown discarded queued records");
            }
        }
        filesystem::remove_all(root);
        cout << "PASS " << name << '\n';
        return 0;
    } catch (const exception& error) {
        error_code ignored;
        filesystem::remove_all(root, ignored);
        cerr << "FAIL " << name << ": " << error.what() << '\n';
        return 1;
    }
}
