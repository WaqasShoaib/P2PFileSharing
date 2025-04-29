#include "tracker_client.h"



bool register_file_with_tracker(const std::string& tracker_ip,
    unsigned short tracker_port,
    const std::string& filename,
    const std::string& my_ip,
    unsigned short my_port) {
    try {
        boost::asio::io_context io;
        tcp::socket sock(io);
        sock.connect({ boost::asio::ip::make_address(tracker_ip), tracker_port });
        std::string msg = "REGISTER " + filename + " " + my_ip +
            " " + std::to_string(my_port) + "\n";
        boost::asio::write(sock, boost::asio::buffer(msg));
        boost::asio::streambuf resp;
        boost::asio::read_until(sock, resp, "\n");
        std::istream is(&resp);
        std::string line;
        std::getline(is, line);
        return (line == "OK");
    }
    catch (...) {
        return false;
    }
}


bool register_with_retry(const std::string& tracker_ip, unsigned short tracker_port,const std::string& filename, const std::string& my_ip,
    unsigned short my_port, int max_retries) 
{
    for (int i = 0; i < max_retries; i++) {
        if (register_file_with_tracker(tracker_ip, tracker_port, filename, my_ip, my_port))
            return true;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return false;
}


std::vector<std::string> get_peers_from_tracker(const std::string& tracker_ip,unsigned short tracker_port,const std::string& filename) 
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
        std::istream is(&resp);
        std::string line;
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


