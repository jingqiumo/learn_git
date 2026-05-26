#include <iostream>
// 只需要这一个头文件！
#include <nlohmann/json.hpp>

// 为了方便，定义别名（推荐）
using json = nlohmann::json;

int main() {
    // ====================== 1. 创建 JSON 对象
    json data;
    data["name"] = "hsj";
    data["age"] = 20;
    data["is_linux"] = true;
    data["skills"] = {"C++", "Linux", "MySQL", "Redis"};

    // ====================== 2. 输出成字符串（序列化）
    std::cout << "JSON 格式输出：\n" << data.dump(4) << std::endl;

    // ====================== 3. 读取 JSON 数据
    std::string name = data["name"];
    int age = data["age"];

    std::cout << "\n姓名：" << name << std::endl;
    std::cout << "年龄：" << age << std::endl;

    return 0;
}