#pragma once

#include <string>
#include <cstdint>
#include <unordered_map>

// 简单的配置文件解析器（INI 风格）
class Config {
public:
    explicit Config(const std::string& filepath);
    bool loaded() const { return !data_.empty(); }

    std::string get(const std::string& key, const std::string& def = "") const;
    int getInt(const std::string& key, int def = 0) const;
    uint16_t getPort(const std::string& key, uint16_t def = 0) const;

private:
    std::unordered_map<std::string, std::string> data_;
};
