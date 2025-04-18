// File: P2PFileSharing/peer.cpp

#include <boost/asio.hpp>
#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <string>
#include <filesystem>
#include <sstream>
#include <cmath>
#include <mutex>
static std::mutex cout_mutex;


using boost::asio::ip::tcp;
constexpr size_t CHUNK_SIZE = 1024;

// ─── Tracker Helpers (unchanged) ─────────────────────────────────────────────

bool register_file_with_tracker(const std::string& tracker_ip,
    unsigned short tracker_port,
    const std::string& filename,
    unsigned short my_port)
{
    try {
        boost::asio::io_context io;
        tcp::socket sock(io);
        sock.connect({ boost::asio::ip::make_address(tracker_ip), tracker_port });

        std::string msg = "REGISTER " + filename +
            " 127.0.0.1 " + std::to_string(my_port) + "\n";
        boost::asio::write(sock, boost::asio::buffer(msg));

        boost::asio::streambuf resp;
        boost::asio::read_until(sock, resp, "\n");
        std::string line;
        std::istream is(&resp);
        std::getline(is, line);
        return line == "OK";
    }
    catch (...) {
        return false;
    }
}

std::vector<std::string> get_peers_from_tracker(const std::string& tracker_ip,
    unsigned short tracker_port,
    const std::string& filename)
{
    std::vector<std::string> peers;
    try {
        boost::asio::io_context io;
        tcp::socket sock(io);
        sock.connect({ boost::asio::ip::make_address(tracker_ip), tracker_port });

        std::string msg = "GETPEERS " + filename + "\n";
        boost::asio::write(sock, boost::asio::buffer(msg));

        boost::asio::streambuf resp;
        boost::asio::read_until(sock, resp, "\n");
        std::string line;
        std::istream is(&resp);
        std::getline(is, line);

        std::stringstream ss(line);
        std::string peer;
        while (std::getline(ss, peer, ';')) {
            if (!peer.empty()) peers.push_back(peer);
        }
    }
    catch (...) {}
    return peers;
}

// ─── Server: handle FILESIZE, SENDCHUNK, or full file ───────────────────────

void run_server(unsigned short port) {
    try {
        boost::asio::io_context io;
        tcp::acceptor acceptor(io, { tcp::v4(), port });
        std::cout << "[Peer] Listening on port " << port << "...\n";

        while (true) {
            tcp::socket sock(io);
            acceptor.accept(sock);
            std::cout << "[Peer] Connection from "
                << sock.remote_endpoint() << "\n";

            // Read the request line
            boost::asio::streambuf buf;
            boost::asio::read_until(sock, buf, "\n");
            std::string line;
            std::getline(std::istream(&buf), line);

            std::istringstream iss(line);
            std::string cmd; iss >> cmd;

            if (cmd == "FILESIZE") {
                // FILESIZE <filename>
                std::string filename; iss >> filename;
                std::ifstream f(filename, std::ios::binary | std::ios::ate);
                size_t filesize = f ? f.tellg() : 0;
                std::string resp = std::to_string(filesize) + "\n";
                boost::asio::write(sock, boost::asio::buffer(resp));
                std::cout << "[Peer] FILESIZE " << filename
                    << " → " << filesize << "\n";

            }
            else if (cmd == "SENDCHUNK") {
                // SENDCHUNK <filename> <idx>
                std::string filename; size_t idx;
                iss >> filename >> idx;
                std::ifstream f(filename, std::ios::binary);
                if (!f) {
                    std::cerr << "[Peer] File not found: " << filename << "\n";
                }
                else {
                    f.seekg(idx * CHUNK_SIZE, std::ios::beg);
                    std::vector<char> chunk(CHUNK_SIZE);
                    f.read(chunk.data(), CHUNK_SIZE);
                    size_t got = f.gcount();
                    boost::asio::write(sock, boost::asio::buffer(chunk.data(), got));
                    std::cout << "[Peer] Sent chunk " << idx
                        << " (" << got << " bytes)\n";
                }

            }
            else {
                // Fallback: cmd is filename → send entire file
                std::string filename = cmd;
                std::ifstream f(filename, std::ios::binary);
                if (!f) {
                    std::cerr << "[Peer] File not found: " << filename << "\n";
                }
                else {
                    std::vector<char> chunk(CHUNK_SIZE);
                    while (f.read(chunk.data(), CHUNK_SIZE) || f.gcount() > 0) {
                        size_t got = f.gcount();
                        boost::asio::write(sock,
                            boost::asio::buffer(chunk.data(), got));
                    }
                    std::cout << "[Peer] Sent full file: " << filename << "\n";
                }
            }
        }

    }
    catch (const std::exception& e) {
        std::cerr << "[Peer] Server error: " << e.what() << "\n";
    }
}

// ─── Leecher: parallel chunk download ────────────────────────────────────────

size_t get_filesize_from_peer(const std::string& peer_ip, unsigned short peer_port,
    const std::string& filename)
{
    try {
        boost::asio::io_context io;
        tcp::socket sock(io);
        sock.connect({ boost::asio::ip::make_address(peer_ip), peer_port });

        std::string msg = "FILESIZE " + filename + "\n";
        boost::asio::write(sock, boost::asio::buffer(msg));

        boost::asio::streambuf buf;
        boost::asio::read_until(sock, buf, "\n");
        std::string line;
        std::getline(std::istream(&buf), line);
        return std::stoull(line);
    }
    catch (...) {
        return 0;
    }
}

void run_leecher_parallel(const std::vector<std::string>& peers,
    const std::string& filename)
{
    if (peers.empty()) {
        std::cerr << "[Leecher] No peers available\n";
        return;
    }

    // 1. Get total file size from first peer
    auto [ip0, port0] = [&]() {
        auto& p = peers[0];
        auto pos = p.find(':');
        return std::make_pair(p.substr(0, pos),
            static_cast<unsigned short>(std::stoi(p.substr(pos + 1))));
        }();
    size_t filesize = get_filesize_from_peer(ip0, port0, filename);
    if (filesize == 0) {
        std::cerr << "[Leecher] Failed to get file size\n";
        return;
    }
    size_t total_chunks = (filesize + CHUNK_SIZE - 1) / CHUNK_SIZE;

    // 2. Prepare output file with correct size
    std::filesystem::create_directory("downloads");
    std::ofstream outfile("downloads/" + filename, std::ios::binary | std::ios::trunc);
    outfile.seekp(filesize - 1);
    outfile.write("", 1);
    outfile.close();

    // 3. Spawn threads to download each chunk
    std::vector<std::thread> threads;
    for (size_t idx = 0; idx < total_chunks; ++idx) {
        threads.emplace_back([&, idx]() {
            // Round‑robin select a peer
            auto& p = peers[idx % peers.size()];
            auto pos = p.find(':');
            std::string ip = p.substr(0, pos);
            unsigned short port = static_cast<unsigned short>(std::stoi(p.substr(pos + 1)));

            try {
                boost::asio::io_context io;
                tcp::socket sock(io);
                sock.connect({ boost::asio::ip::make_address(ip), port });

                // Request chunk
                std::string req = "SENDCHUNK " + filename +
                    " " + std::to_string(idx) + "\n";
                boost::asio::write(sock, boost::asio::buffer(req));

                // Read chunk
                std::vector<char> buffer(CHUNK_SIZE);
                size_t to_read = std::min(CHUNK_SIZE, filesize - idx * CHUNK_SIZE);
                size_t got = 0;
                boost::system::error_code ec;
                while (got < to_read) {
                    size_t n = sock.read_some(
                        boost::asio::buffer(buffer.data() + got, to_read - got), ec);
                    if (ec && ec != boost::asio::error::eof) break;
                    got += n;
                    if (ec == boost::asio::error::eof) break;
                }

                // Write into file
                std::fstream out("downloads/" + filename,
                    std::ios::binary | std::ios::in | std::ios::out);
                out.seekp(idx * CHUNK_SIZE);
                out.write(buffer.data(), got);
                out.close();

                {
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    std::cout << "[Leecher] Chunk " << idx
                        << " downloaded from " << ip << ":" << port << "\n";
                }

            }
            catch (const std::exception& e) {
                std::cerr << "[Leecher] Chunk " << idx
                    << " failed: " << e.what() << "\n";
            }
            });
    }

    // Join all threads
    for (auto& t : threads) t.join();
    std::cout << "[Leecher] All chunks downloaded, file complete.\n";
}

// ─── MAIN MENU ──────────────────────────────────────────────────────────────

int main() {
    std::string tracker_ip = "127.0.0.1";
    unsigned short tracker_port = 8000;

    std::cout << "Mode:\n"
        << "1. Sender (Seeder)\n"
        << "2. Receiver (Leecher)\n"
        << "Choice: ";
    int choice; std::cin >> choice; std::cin.ignore();

    if (choice == 1) {
        // Seeder
        std::string filename; unsigned short my_port;
        std::cout << "File to share: "; std::getline(std::cin, filename);
        std::cout << "Listen port: "; std::cin >> my_port;

        if (register_file_with_tracker(
            tracker_ip, tracker_port, filename, my_port)) {
            std::cout << "[Peer] Registered " << filename << " with tracker.\n";
            run_server(my_port);
        }
        else {
            std::cerr << "[Peer] Tracker registration failed.\n";
        }

    }
    else if (choice == 2) {
        // Leecher (parallel chunk download)
        std::string filename;
        std::cout << "File to download: "; std::getline(std::cin, filename);

        auto peers = get_peers_from_tracker(tracker_ip, tracker_port, filename);
        if (peers.empty()) {
            std::cerr << "[Peer] No peers found.\n";
            return 1;
        }

        run_leecher_parallel(peers, filename);
    }

    return 0;
}
