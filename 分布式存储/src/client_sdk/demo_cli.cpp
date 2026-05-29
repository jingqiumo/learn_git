#include "kv_client.h"
#include <iostream>
#include <string>
#include <sstream>
#include <vector>

void showMenu() {
    std::cout << "\n=========== DKV CLI ===========" << std::endl;
    std::cout << "  put <key> <value>    — 写入键值" << std::endl;
    std::cout << "  get <key>            — 读取键值" << std::endl;
    std::cout << "  del <key>            — 删除键值" << std::endl;
    std::cout << "  scan <start> <end>   — 范围扫描" << std::endl;
    std::cout << "  help                 — 显示帮助" << std::endl;
    std::cout << "  quit                 — 退出" << std::endl;
    std::cout << "==============================" << std::endl;
    std::cout << "> " << std::flush;
}

int main(int argc, char* argv[]) {
    std::string ip = "127.0.0.1";
    uint16_t port = 9100;

    if (argc == 3) {
        ip = argv[1];
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    }

    std::cout << "DKV Client connecting to " << ip << ":" << port << "..." << std::endl;

    KvClient client;
    if (!client.connect(ip, port)) {
        std::cerr << "Failed to connect to " << ip << ":" << port << std::endl;
        return -1;
    }
    std::cout << "Connected! Type 'help' for commands." << std::endl;

    std::string line;
    while (true) {
        showMenu();
        if (!std::getline(std::cin, line) || line == "quit") break;
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "put") {
            std::string key, value;
            iss >> key;
            std::getline(iss, value);
            // trim leading space
            if (!value.empty() && value[0] == ' ') value.erase(0, 1);

            if (key.empty() || value.empty()) {
                std::cout << "Usage: put <key> <value>" << std::endl;
                continue;
            }
            if (client.put(key, value)) {
                std::cout << "OK" << std::endl;
            } else {
                std::cout << "ERROR: put failed" << std::endl;
            }

        } else if (cmd == "get") {
            std::string key;
            iss >> key;
            if (key.empty()) {
                std::cout << "Usage: get <key>" << std::endl;
                continue;
            }
            std::string value;
            if (client.get(key, value)) {
                std::cout << "\"" << value << "\"" << std::endl;
            } else {
                std::cout << "(not found)" << std::endl;
            }

        } else if (cmd == "del") {
            std::string key;
            iss >> key;
            if (key.empty()) {
                std::cout << "Usage: del <key>" << std::endl;
                continue;
            }
            if (client.del(key)) {
                std::cout << "OK" << std::endl;
            } else {
                std::cout << "(not found)" << std::endl;
            }

        } else if (cmd == "scan") {
            std::string start, end;
            iss >> start >> end;
            auto kvs = client.scan(start, end, 100);
            if (kvs.empty()) {
                std::cout << "(empty)" << std::endl;
            } else {
                for (auto& kv : kvs) {
                    std::cout << "  " << kv.first << " => " << kv.second << std::endl;
                }
                std::cout << kvs.size() << " result(s)" << std::endl;
            }

        } else if (cmd == "help") {
            // menu was already shown
        } else {
            std::cout << "Unknown command: " << cmd << std::endl;
        }
    }

    client.close();
    std::cout << "Bye!" << std::endl;
    return 0;
}
