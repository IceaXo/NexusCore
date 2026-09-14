#include "../ClientOptions.h"
#include <iostream>

int main() {
    ClientOptions options;
    std::string error;
    if (!ParseClientOptions({}, options, error) || options.server != "127.0.0.1" ||
        options.ui != "http://127.0.0.1:7778/p5_ui.html") return 1;
    for (const std::string port : {"0", "65536", "-1", "abc", "12abc", "9999999999999999999999"}) {
        if (ParseClientOptions({"--port", port}, options, error)) return 2;
    }
    for (const auto& args : std::vector<std::vector<std::string>>{
        {"--port"}, {"--server", ""}, {"--file", "--port", "7777"}, {"--unknown"}}) {
        if (ParseClientOptions(args, options, error)) return 3;
    }
    if (!ParseClientOptions({"--server", "localhost", "--port", "65535"}, options, error) ||
        options.port != 65535 || options.ui != "http://localhost:7778/p5_ui.html") return 4;
    if (!ParseClientOptions({"--file", "C:/demo/p5_ui.html", "--server", "localhost"}, options, error) ||
        !options.file || options.ui != "C:/demo/p5_ui.html") return 5;
    if (!ParseClientOptions({"--help"}, options, error) || !options.help) return 6;
    std::cout << "Client startup defaults and argument boundaries passed\n";
    return 0;
}
