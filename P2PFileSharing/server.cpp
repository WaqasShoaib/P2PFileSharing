
#include "server.h"
#include "common.h"               


void run_server(unsigned short port)
{
    try {
        boost::asio::io_context io;
        tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), port));
        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "[Server] Listening on port " << port << "...\n";
        }
        while (true) {
            tcp::socket sock(io);
            acceptor.accept(sock);
            boost::asio::streambuf buf;
            boost::asio::read_until(sock, buf, "\n");
            std::string line;
            std::getline(std::istream(&buf), line);
            std::istringstream iss(line);
            std::string cmd; iss >> cmd;

            auto resolve = [&](const std::string& fn) {
                // 1) Always serve the real file in shared_files/
                std::filesystem::path shared = std::filesystem::path("shared_files") / fn;
                if (std::filesystem::exists(shared)) return shared.string();

                // 2) Maybe it’s a downloaded file (for Leecher)
                std::filesystem::path dl = std::filesystem::path("downloads") / fn;
                if (std::filesystem::exists(dl)) return dl.string();

                // 3) Check current folder (fallback)
                if (std::filesystem::exists(fn)) return fn;

                // 4) Otherwise, return where the Leecher would put it
                return dl.string();
                };


            if (cmd == "FILESIZE") {
                std::string fn; iss >> fn;
                std::string path = resolve(fn);
                size_t sz = std::filesystem::exists(path) ?
                    std::filesystem::file_size(path) : 0;
                boost::asio::write(sock, boost::asio::buffer(std::to_string(sz) + "\n"));
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << "[Server] FILESIZE " << fn << ": " << sz << " bytes\n";
            }
            else if (cmd == "SENDCHUNK") {
                std::string fn; size_t idx;
                iss >> fn >> idx;
                std::string path = resolve(fn);

                try {
                    std::ifstream f(path, std::ios::binary);
                    if (!f) {
                        std::lock_guard<std::mutex> lock(cout_mutex);
                        std::cerr << "[Server] File not found: " << path << "\n";
                        continue;
                    }

                    // Get file size to check boundaries
                    f.seekg(0, std::ios::end);
                    size_t file_size = f.tellg();

                    // Calculate chunk info
                    size_t offset = idx * CHUNK_SIZE;
                    if (offset >= file_size) {
                        std::lock_guard<std::mutex> lock(cout_mutex);
                        std::cerr << "[Server] Chunk index " << idx << " out of bounds for " << path << "\n";
                        continue;
                    }

                    // Calculate actual chunk size (may be less for last chunk)
                    size_t actual_chunk_size = std::min(CHUNK_SIZE, file_size - offset);

                    // Read the chunk
                    f.seekg(offset, std::ios::beg);
                    if (!f) {
                        std::lock_guard<std::mutex> lock(cout_mutex);
                        std::cerr << "[Server] Seek error to position " << offset << " in " << path << "\n";
                        continue;
                    }

                    std::vector<char> chunk(actual_chunk_size);
                    f.read(chunk.data(), actual_chunk_size);
                    size_t got = f.gcount();

                    if (got != actual_chunk_size) {
                        std::lock_guard<std::mutex> lock(cout_mutex);
                        std::cerr << "[Server] Read error: expected " << actual_chunk_size
                            << " but got " << got << " bytes\n";
                    }

                    // Send the chunk
                    boost::asio::write(sock, boost::asio::buffer(chunk.data(), got));

                    std::lock_guard<std::mutex> lock(cout_mutex);
                    std::cout << "[Server] Sent chunk " << idx << " (" << got << " bytes)\n";
                }
                catch (const std::exception& e) {
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    std::cerr << "[Server] Error sending chunk " << idx << ": " << e.what() << "\n";
                }
            }
            else {
                std::string fn = cmd;
                std::string path = resolve(fn);
                std::ifstream f(path, std::ios::binary);
                size_t total = 0;
                if (f) {
                    std::vector<char> chunk(CHUNK_SIZE);
                    while (f.read(chunk.data(), CHUNK_SIZE) || f.gcount() > 0) {
                        size_t n = f.gcount();
                        total += n;
                        boost::asio::write(sock, boost::asio::buffer(chunk.data(), n));
                    }
                }
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << "[Server] Sent full file: " << fn << " (" << total << " bytes)\n";
            }
        }
    }
    catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cerr << "[Server] Error: " << e.what() << "\n";
    }
}
