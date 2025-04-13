#include <boost/asio.hpp>
#include <iostream>
#include <fstream>
#include <vector>

using boost::asio::ip::tcp;

int main() {
    try {
        boost::asio::io_context io;

        tcp::endpoint endpoint(tcp::v4(), 9000);
        tcp::acceptor acceptor(io, endpoint);
        std::cout << "[Server] Waiting for client to connect...\n";

        tcp::socket socket(io);
        acceptor.accept(socket);
        std::cout << "[Server] Client connected.\n";

        // --- Step 1: Receive filename length ---
        uint32_t name_len = 0;
        boost::asio::read(socket, boost::asio::buffer(&name_len, sizeof(name_len)));

        // --- Step 2: Receive filename string ---
        std::vector<char> name_buffer(name_len);
        boost::asio::read(socket, boost::asio::buffer(name_buffer));
        std::string filename(name_buffer.begin(), name_buffer.end());

        std::cout << "[Server] Receiving file: " << filename << "\n";

        std::ofstream outfile(filename, std::ios::binary);
        if (!outfile) {
            std::cerr << "[Server] Failed to create file.\n";
            return 1;
        }

        // --- Step 3: Receive file content ---
        std::vector<char> buffer(1024);
        while (true) {
            boost::system::error_code error;
            size_t bytes_received = socket.read_some(boost::asio::buffer(buffer), error);

            if (error == boost::asio::error::eof) {
                std::cout << "[Server] Transfer complete (EOF reached).\n";
                break;
            }
            else if (error) {
                std::cerr << "[Server] Unexpected error: " << error.message() << "\n";
                break;
            }

            outfile.write(buffer.data(), bytes_received);
        }

        outfile.close();
        std::cout << "[Server] File saved as: " << filename << "\n";

    }
    catch (std::exception& e) {
        std::cerr << "[Server] Error: " << e.what() << std::endl;
    }

    return 0;
}
