#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <nlohmann/json.hpp>
#include <vector>

using json = nlohmann::json;

// ======================== 全局状态 ========================
std::atomic<bool> g_running{true};
std::atomic<bool> g_loggedIn{false};
int g_myId = -1;
std::string g_myName;
int g_sock = -1;
std::mutex g_sendMutex;
std::mutex g_coutMutex;

// 用于唤醒主线程：收到需要关注的响应时通知
std::mutex g_responseMutex;
std::condition_variable g_responseCv;
std::atomic<bool> g_responseArrived{false};

// ======================== 发送 JSON 消息 ========================
void sendJson(const json& js) {
    std::lock_guard<std::mutex> lock(g_sendMutex);
    std::string data = js.dump();
    if (::send(g_sock, data.data(), data.size(), 0) < 0) {
        std::lock_guard<std::mutex> cl(g_coutMutex);
        std::cerr << "[发送失败] 服务器可能已断开" << std::endl;
    }
}

// ======================== 安全取字段（兼容 from/fromid, to/toid） ========================
int safeGetFrom(const json& js) {
    if (js.contains("from")) return js["from"];
    if (js.contains("fromid")) return js["fromid"];
    return -1;
}

int safeGetTo(const json& js) {
    if (js.contains("to")) return js["to"];
    if (js.contains("toid")) return js["toid"];
    return -1;
}

// ======================== 显示收到的消息 ========================
void showMessage(const json& js) {
    std::lock_guard<std::mutex> lock(g_coutMutex);
    std::cout << "\n========================================" << std::endl;
    std::cout << "| 新消息" << std::endl;
    std::cout << "| 来自: " << safeGetFrom(js) << std::endl;
    std::cout << "| 时间: " << js.value("time", "未知") << std::endl;
    std::cout << "| 内容: " << js.value("message", "") << std::endl;
    std::cout << "| msgID: " << js.value("messageID", -1) << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "> " << std::flush;

    // 自动发送确认
    json ack;
    ack["msgtype"] = 6;
    ack["messageID"] = js["messageID"];
    sendJson(ack);
}

// ======================== 从缓冲区提取完整 JSON 对象（处理粘包） ========================
// 返回解析成功的 JSON 对象，并消耗掉 buffer 中对应的部分
// 若没有完整对象则返回空的 vector
std::vector<json> extractJsonObjects(std::string& buffer) {
    std::vector<json> result;
    while (!buffer.empty()) {
        // 跳过前导空白
        size_t start = buffer.find('{');
        if (start == std::string::npos) {
            buffer.clear();
            break;
        }
        buffer.erase(0, start);

        // 括号匹配，追踪字符串状态
        int depth = 0;
        bool inString = false;
        size_t end = std::string::npos;
        for (size_t i = 0; i < buffer.size(); ++i) {
            char c = buffer[i];
            if (c == '"' && (i == 0 || buffer[i - 1] != '\\')) {
                inString = !inString;
            }
            if (!inString) {
                if (c == '{') ++depth;
                else if (c == '}') --depth;
                if (depth == 0) {
                    end = i;
                    break;
                }
            }
        }
        if (end == std::string::npos) {
            // 对象不完整，等待更多数据
            break;
        }

        std::string jsonStr = buffer.substr(0, end + 1);
        buffer.erase(0, end + 1);

        try {
            result.push_back(json::parse(jsonStr));
        } catch (const json::parse_error&) {
            // 解析失败，丢弃该段
        }
    }
    return result;
}

// ======================== 接收线程 ========================
void recvThread() {
    char buf[65536];
    std::string buffer;
    while (g_running) {
        int n = ::recv(g_sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            std::lock_guard<std::mutex> lock(g_coutMutex);
            std::cout << "\n[与服务器断开连接]" << std::endl;
            g_running = false;
            g_responseArrived = true;
            g_responseCv.notify_one();
            break;
        }
        buf[n] = '\0';
        buffer += buf;

        auto objects = extractJsonObjects(buffer);
        for (auto& js : objects) {
            int msgtype = js["msgtype"];
            switch (msgtype) {
            case 3: { // REF_ACK
                {
                    std::lock_guard<std::mutex> lock(g_coutMutex);
                    if (js["error"] == 0) {
                        std::cout << "\n[注册成功] 您的账号 ID 为: " << js["id"] << std::endl;
                    } else {
                        std::cout << "\n[注册失败] " << js.value("error_message", "") << std::endl;
                    }
                }
                g_responseArrived = true;
                g_responseCv.notify_one();
                break;
            }
            case 4: { // LOGIN_ACK
                if (js["error"] == 0) {
                    g_loggedIn = true;
                    g_myName = js["name"];
                    {
                        std::lock_guard<std::mutex> lock(g_coutMutex);
                        std::cout << "\n[登录成功] 欢迎, " << g_myName << "!" << std::endl;
                    }
                } else {
                    {
                        std::lock_guard<std::mutex> lock(g_coutMutex);
                        std::cout << "\n[登录失败] " << js.value("error_message", "") << std::endl;
                    }
                }
                g_responseArrived = true;
                g_responseCv.notify_one();
                break;
            }
            case 5: // ONE_MESSAGE
                showMessage(js);
                break;
            case 6: // ACK_MESSAGE
                break;
            case 8: { // ADD_FRIEND_ACK
                {
                    std::lock_guard<std::mutex> lock(g_coutMutex);
                    if (js.value("error", 0) == 1) {
                        std::cout << "\n[添加好友] " << js.value("error_message", "失败") << std::endl;
                    } else {
                        std::cout << "\n[好友申请已发送]" << std::endl;
                    }
                }
                g_responseArrived = true;
                g_responseCv.notify_one();
                break;
            }
            case 11: { // GET_ADDLIST 响应
                std::lock_guard<std::mutex> lock(g_coutMutex);
                std::cout << "\n[好友申请] ID:" << js["user_id"]
                          << "  昵称:" << js["user_name"]
                          << "  时间:" << js.value("time", "") << std::endl;
                break;
            }
            case 12: { // GET_FRILIST 响应
                std::lock_guard<std::mutex> lock(g_coutMutex);
                std::cout << "\n[好友] ID:" << js["user_id"]
                          << "  昵称:" << js["user_name"]
                          << "  备注:" << js.value("remark", "")
                          << "  添加时间:" << js.value("time", "") << std::endl;
                break;
            }
            default:
                break;
            }
        }
    }
}

// ======================== 菜单 ========================
void showPreLoginMenu() {
    std::cout << "\n========== 聊天系统 ==========" << std::endl;
    std::cout << "1. 登录" << std::endl;
    std::cout << "2. 注册" << std::endl;
    std::cout << "3. 退出" << std::endl;
    std::cout << "==============================" << std::endl;
    std::cout << "> " << std::flush;
}

void showMainMenu() {
    std::cout << "\n========== 主菜单 ==========" << std::endl;
    std::cout << "1.  发送消息" << std::endl;
    std::cout << "2.  添加好友" << std::endl;
    std::cout << "3.  查看好友申请" << std::endl;
    std::cout << "4.  同意好友申请" << std::endl;
    std::cout << "5.  拒绝好友申请" << std::endl;
    std::cout << "6.  查看好友列表" << std::endl;
    std::cout << "7.  退出登录" << std::endl;
    std::cout << "=============================" << std::endl;
    std::cout << "> " << std::flush;
}

// ======================== 等待响应（用于登录/注册等需要确认的操作） ========================
void waitForResponse() {
    std::unique_lock<std::mutex> lock(g_responseMutex);
    g_responseCv.wait_for(lock, std::chrono::seconds(5), [] {
        return g_responseArrived.load();
    });
    g_responseArrived = false;
}

// ======================== 主函数 ========================
int main(int argc, char* argv[]) {
    std::string ip = "127.0.0.1";
    int port = 8000;

    if (argc == 3) {
        ip = argv[1];
        port = std::stoi(argv[2]);
    }

    // 1. 创建 socket
    g_sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock < 0) {
        std::cerr << "创建 socket 失败" << std::endl;
        return -1;
    }

    // 2. 连接服务器
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "IP 地址无效: " << ip << std::endl;
        close(g_sock);
        return -1;
    }

    std::cout << "正在连接 " << ip << ":" << port << "..." << std::endl;
    if (::connect(g_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "连接服务器失败" << std::endl;
        close(g_sock);
        return -1;
    }
    std::cout << "连接成功!" << std::endl;

    // 3. 启动接收线程
    std::thread recv(recvThread);

    // 4. 主循环
    std::string input;
    while (g_running) {
        if (!g_loggedIn) {
            showPreLoginMenu();
        } else {
            showMainMenu();
        }

        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;

        // ========== 未登录状态 ==========
        if (!g_loggedIn) {
            if (input == "1") {
                std::cout << "请输入账号 ID: ";
                std::string sid;
                std::getline(std::cin, sid);
                std::cout << "请输入密码: ";
                std::string pwd;
                std::getline(std::cin, pwd);

                g_myId = std::stoi(sid);
                json js;
                js["msgtype"] = 1;
                js["id"] = g_myId;
                js["password"] = pwd;
                sendJson(js);

                // 等待登录响应，期间不显示菜单
                waitForResponse();

            } else if (input == "2") {
                std::cout << "请输入用户名: ";
                std::string name;
                std::getline(std::cin, name);
                std::cout << "请输入密码: ";
                std::string pwd;
                std::getline(std::cin, pwd);

                json js;
                js["msgtype"] = 2;
                js["name"] = name;
                js["password"] = pwd;
                sendJson(js);

                waitForResponse();

            } else if (input == "3") {
                std::cout << "再见!" << std::endl;
                g_running = false;
                break;
            }
        }
        // ========== 已登录状态 ==========
        else {
            if (input == "1") {
                std::cout << "发送给 (用户ID): ";
                std::string toid;
                std::getline(std::cin, toid);
                std::cout << "消息内容: ";
                std::string msg;
                std::getline(std::cin, msg);

                json js;
                js["msgtype"] = 5;
                js["fromid"] = g_myId;
                js["toid"] = std::stoi(toid);
                js["message"] = msg;
                sendJson(js);
                std::cout << "[消息已发送]" << std::endl;

            } else if (input == "2") {
                std::cout << "要添加的用户 ID: ";
                std::string fid;
                std::getline(std::cin, fid);

                json js;
                js["msgtype"] = 7;
                js["user_id"] = g_myId;
                js["friend_id"] = std::stoi(fid);
                sendJson(js);

            } else if (input == "3") {
                json js;
                js["msgtype"] = 11;
                js["user_id"] = g_myId;
                sendJson(js);

            } else if (input == "4") {
                std::cout << "要同意的用户 ID: ";
                std::string fid;
                std::getline(std::cin, fid);

                json js;
                js["msgtype"] = 9;
                js["user_id"] = g_myId;
                js["friend_id"] = std::stoi(fid);
                sendJson(js);
                std::cout << "[已同意好友申请]" << std::endl;

            } else if (input == "5") {
                std::cout << "要拒绝的用户 ID: ";
                std::string fid;
                std::getline(std::cin, fid);

                json js;
                js["msgtype"] = 10;
                js["user_id"] = g_myId;
                js["friend_id"] = std::stoi(fid);
                sendJson(js);
                std::cout << "[已拒绝好友申请]" << std::endl;

            } else if (input == "6") {
                json js;
                js["msgtype"] = 12;
                js["user_id"] = g_myId;
                sendJson(js);

            } else if (input == "7") {
                g_loggedIn = false;
                g_myId = -1;
                g_myName.clear();
                std::cout << "[已退出登录]" << std::endl;
            }
        }
    }

    // 5. 清理
    g_running = false;
    shutdown(g_sock, SHUT_RDWR);
    close(g_sock);
    if (recv.joinable()) recv.join();
    return 0;
}
