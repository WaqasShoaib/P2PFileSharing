// File: P2PFileSharing/tracker.cpp

#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <thread>
#include <mutex>

using boost::asio::ip::tcp;

static std::unordered_map<std::string, std::vector<std::string>> registry;
static std::mutex registry_mutex;

void handle_session(tcp::socket sock) {
    try {
        boost::asio::streambuf buf;
        boost::asio::read_until(sock, buf, "\n");
        std::istream is(&buf);
        std::string line;
        std::getline(is, line);

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "REGISTER") {
            std::string filename, ip;
            unsigned short port;
            iss >> filename >> ip >> port;
            std::string peer = ip + ":" + std::to_string(port);

            {
                std::lock_guard<std::mutex> lock(registry_mutex);
                auto& vec = registry[filename];
                if (std::find(vec.begin(), vec.end(), peer) == vec.end())
                    vec.push_back(peer);
            }

            boost::asio::write(sock, boost::asio::buffer("OK\n"));
            std::cout << "[Tracker] REGISTER " << filename << " ← " << peer << "\n";
        }
        else if (cmd == "GETPEERS") {
            std::string filename;
            iss >> filename;

            std::string response;
            {
                std::lock_guard<std::mutex> lock(registry_mutex);
                auto it = registry.find(filename);
                if (it != registry.end()) {
                    for (size_t i = 0; i < it->second.size(); ++i) {
                        response += it->second[i];
                        if (i + 1 < it->second.size()) response += ";";
                    }
                }
            }
            response += "\n";
            boost::asio::write(sock, boost::asio::buffer(response));
            std::cout << "[Tracker] GETPEERS " << filename << " → " << response;
        }
        else {
            boost::asio::write(sock, boost::asio::buffer("ERROR Unknown command\n"));
        }

    }
    catch (std::exception& e) {
        std::cerr << "[Tracker] Session error: " << e.what() << "\n";
    }
}

int main() {
    try {
        boost::asio::io_context io;
        tcp::acceptor acceptor(io, { tcp::v4(), 8000 });
        std::cout << "[Tracker] Listening on port 8000...\n";

        while (true) {
            tcp::socket sock(io);
            acceptor.accept(sock);
            std::thread(handle_session, std::move(sock)).detach();
        }
    }
    catch (std::exception& e) {
        std::cerr << "[Tracker] Fatal: " << e.what() << "\n";
    }

    return 0;
}
