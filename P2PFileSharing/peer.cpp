// File: P2PFileSharing/peer.cpp

#include <boost/asio.hpp>
#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <string>
#include <filesystem>

using boost::asio::ip::tcp;

// === TRACKER COMMUNICATION ===

bool register_file_with_tracker(const std::string& tracker_ip, unsigned short tracker_port, const std::string& filename, unsigned short my_port) {
    try {
        boost::asio::io_context io;
        tcp::socket sock(io);
        sock.connect({ boost::asio::ip::make_address(tracker_ip), tracker_port });

        std::string message = "REGISTER " + filename + " 127.0.0.1 " + std::to_string(my_port) + "\n";
        boost::asio::write(sock, boost::asio::buffer(message));

        boost::asio::streambuf response;
        boost::asio::read_until(sock, response, "\n");
        std::istream is(&response);
        std::string line;
        std::getline(is, line);
        return line == "OK";
    }
    catch (...) {
        return false;
    }
}

std::vector<std::string> get_peers_from_tracker(const std::string& tracker_ip, unsigned short tracker_port, const std::string& filename) {
    std::vector<std::string> peers;
    try {
        boost::asio::io_context io;
        tcp::socket sock(io);
        sock.connect({ boost::asio::ip::make_address(tracker_ip), tracker_port });

        std::string message = "GETPEERS " + filename + "\n";
        boost::asio::write(sock, boost::asio::buffer(message));

        boost::asio::streambuf response;
        boost::asio::read_until(sock, response, "\n");
        std::istream is(&response);
        std::string line;
        std::getline(is, line);

        std::stringstream ss(line);
        std::string peer;
        while (std::getline(ss, peer, ';')) {
            peers.push_back(peer);
        }
    }
    catch (...) {}
    return peers;
}

// === SERVER MODE ===

void run_server(unsigned short port) {
    try {
        boost::asio::io_context io;
        tcp::acceptor acceptor(io, { tcp::v4(), port });
        std::cout << "[Peer] Listening for file requests on port " << port << "\n";

        while (true) {
            tcp::socket socket(io);
            acceptor.accept(socket);
            std::cout << "[Peer] Client connected for download.\n";

            boost::asio::streambuf buf;
            boost::asio::read_until(socket, buf, "\n");
            std::istream is(&buf);
            std::string filename;
            std::getline(is, filename);

            std::ifstream file(filename, std::ios::binary);
            if (!file) {
                std::cerr << "[Peer] File not found: " << filename << "\n";
                continue;
            }

            std::vector<char> buffer(1024);
            while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0) {
                boost::asio::write(socket, boost::asio::buffer(buffer.data(), file.gcount()));
            }

            std::cout << "[Peer] File sent successfully: " << filename << "\n";
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[Peer] Server error: " << e.what() << "\n";
    }
}

// === CLIENT MODE ===

void run_client(const std::string& peer_ip, unsigned short peer_port, const std::string& filename) {
    try {
        boost::asio::io_context io;
        tcp::socket socket(io);
        socket.connect({ boost::asio::ip::make_address(peer_ip), peer_port });

        std::string request = filename + "\n";
        boost::asio::write(socket, boost::asio::buffer(request));

        std::filesystem::create_directory("downloads");
        std::ofstream outfile("downloads/" + filename, std::ios::binary);
        if (!outfile) {
            std::cerr << "[Peer] Failed to create file.\n";
            return;
        }

        std::vector<char> buffer(1024);
        boost::system::error_code error;
        while (true) {
            size_t bytes = socket.read_some(boost::asio::buffer(buffer), error);
            if (error == boost::asio::error::eof) break;
            if (error) {
                std::cerr << "[Peer] Read error: " << error.message() << "\n";
                break;
            }
            outfile.write(buffer.data(), bytes);
        }

        std::cout << "[Peer] File downloaded successfully: " << filename << "\n";

    }
    catch (const std::exception& e) {
        std::cerr << "[Peer] Client error: " << e.what() << "\n";
    }
}

// === MAIN MENU ===

int main() {
    std::string tracker_ip = "127.0.0.1";
    unsigned short tracker_port = 8000;

    std::cout << "Choose mode:\n1. Sender (Seeder)\n2. Receiver (Leecher)\nChoice: ";
    int choice;
    std::cin >> choice;
    std::cin.ignore();

    if (choice == 1) {
        std::string filename;
        unsigned short my_port;
        std::cout << "Enter file name to share: ";
        std::getline(std::cin, filename);
        std::cout << "Enter port to listen on: ";
        std::cin >> my_port;

        if (register_file_with_tracker(tracker_ip, tracker_port, filename, my_port)) {
            std::cout << "[Peer] File registered with tracker.\n";
            run_server(my_port);  // Accept clients
        }
        else {
            std::cerr << "[Peer] Failed to register file with tracker.\n";
        }

    }
    else if (choice == 2) {
        std::string filename;
        std::cout << "Enter file name to download: ";
        std::getline(std::cin, filename);

        std::vector<std::string> peers = get_peers_from_tracker(tracker_ip, tracker_port, filename);
        if (peers.empty()) {
            std::cerr << "[Peer] No peers found for that file.\n";
            return 1;
        }

        std::string peer_ip;
        unsigned short peer_port;
        std::stringstream ss(peers[0]);
        std::getline(ss, peer_ip, ':');
        ss >> peer_port;

        run_client(peer_ip, peer_port, filename);
    }

    return 0;
}
