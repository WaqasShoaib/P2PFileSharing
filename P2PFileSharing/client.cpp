#include <boost/asio.hpp>
#include <iostream>
#include <fstream>

using boost::asio::ip::tcp;

int main() {
    try {
        boost::asio::io_context io;
        tcp::resolver resolver(io);
        tcp::resolver::results_type endpoints = resolver.resolve("127.0.0.1", "9000");

        tcp::socket socket(io);
        boost::asio::connect(socket, endpoints);
        std::cout << "[Client] Connected to server.\n";

        std::string filename = "sample.txt";
        std::ifstream file(filename, std::ios::binary);
        if (!file) {
            std::cerr << "[Client] Failed to open file.\n";
            return 1;
        }

        // Send filename size
        uint32_t name_len = static_cast<uint32_t>(filename.size());
        boost::asio::write(socket, boost::asio::buffer(&name_len, sizeof(name_len)));

        // Send filename
        boost::asio::write(socket, boost::asio::buffer(filename));

        // Send file content
        file.seekg(0, std::ios::end);
        size_t filesize = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<char> buffer(filesize);
        file.read(buffer.data(), filesize);
        boost::asio::write(socket, boost::asio::buffer(buffer));

        std::cout << "[Client] File sent successfully.\n";
    }
    catch (std::exception& e) {
        std::cerr << "[Client] Error: " << e.what() << std::endl;
    }

    return 0;
}
