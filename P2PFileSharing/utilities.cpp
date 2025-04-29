
#include "utilities.h"
#include "common.h"
std::string get_local_ip()
{
    try {
        boost::asio::io_context io;
        boost::asio::ip::udp::socket sock(io);
        sock.connect({ boost::asio::ip::make_address("8.8.8.8"), 53 });
        return sock.local_endpoint().address().to_string();
    }
    catch (...) {
        return "127.0.0.1";
    }
}

unsigned short find_free_port() 
{
    boost::asio::io_context io;
    tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), 0));
    return acceptor.local_endpoint().port();
}

bool verify_file_integrity(const std::string& filename, size_t expected_size)
{
    try {
        std::ifstream file(filename, std::ios::binary);
        if (!file) {
            std::cerr << "Cannot open file for verification: " << filename << "\n";
            return false;
        }

        // Check file size
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        if (size != expected_size) {
            std::cerr << "File size mismatch: expected " << expected_size
                << ", got " << size << "\n";
            return false;
        }

        // Read entire file to check for corruption
        file.seekg(0, std::ios::beg);
        std::vector<char> buffer(4096);
        size_t total_read = 0;

        while (file) {
            file.read(buffer.data(), buffer.size());
            size_t read_count = file.gcount();
            if (read_count == 0) break;

            // Check for blocks of zeros or other patterns that might indicate corruption
            bool all_zeros = true;
            for (size_t i = 0; i < read_count; i++) {
                if (buffer[i] != 0) {
                    all_zeros = false;
                    break;
                }
            }

            if (all_zeros && read_count == buffer.size()) {
                std::cerr << "Warning: Found block of all zeros at offset "
                    << total_read << "\n";
            }

            total_read += read_count;
        }

        std::cout << "File integrity verified: " << filename
            << " (" << total_read << " bytes)\n";
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error verifying file: " << e.what() << "\n";
        return false;
    }
}

size_t get_filesize_from_peer(const std::string& ip,unsigned short port,const std::string& filename) {
    try {
        boost::asio::io_context io;
        tcp::socket sock(io);
        sock.connect({ boost::asio::ip::make_address(ip), port });
        std::string msg = "FILESIZE " + filename + "\n";
        boost::asio::write(sock, boost::asio::buffer(msg));
        boost::asio::streambuf buf;
        boost::asio::read_until(sock, buf, "\n");
        std::string line;
        std::getline(std::istream(&buf), line);
        return std::stoull(line);
    }
    catch (...) { return 0; }
}
