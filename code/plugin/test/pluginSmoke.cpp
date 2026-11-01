#include "../module.h"

#include <exception>
#include <iostream>
#include <string>

using namespace std;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: pluginSmoke PATH_TO_PLUGIN\n";
        return 1;
    }
    try {
        string body;
        {
            const auto module = Module::Load(argv[1]);
            body = module->Handle("/api/version");
        }
        //Module已经释放,正文仍由主程序持有
        cout << body;
        return 0;
    } catch (const exception& error) {
        cerr << error.what() << '\n';
        return 1;
    }
}
