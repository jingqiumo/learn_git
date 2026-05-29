#include "config.h"
#include <fstream>
#include <sstream>
#include <cstdlib>

Config::Config(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        // trim
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        val.erase(0, val.find_first_not_of(" \t"));
        val.erase(val.find_last_not_of(" \t") + 1);
        data_[key] = val;
    }
}

std::string Config::get(const std::string& key, const std::string& def) const {
    auto it = data_.find(key);
    return it != data_.end() ? it->second : def;
}

int Config::getInt(const std::string& key, int def) const {
    auto it = data_.find(key);
    return it != data_.end() ? std::stoi(it->second) : def;
}

uint16_t Config::getPort(const std::string& key, uint16_t def) const {
    auto it = data_.find(key);
    return it != data_.end() ? static_cast<uint16_t>(std::stoi(it->second)) : def;
}
