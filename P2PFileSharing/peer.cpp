// File: P2PFileSharing/peer.cpp

#include <boost/asio.hpp>
#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <string>
#include <filesystem>
#include <sstream>
#include <mutex>
#include <algorithm>
#include <chrono>
#include "httplib.h" // Include cpp-httplib single-header
#include "queue"

using boost::asio::ip::tcp;
constexpr size_t CHUNK_SIZE = 1024 * 256; // Increased to 256KB chunks
static std::mutex cout_mutex;

// Global download progress tracking
struct DownloadProgress {
    std::string filename;
    size_t total_chunks;
    size_t completed_chunks;
    bool finished;
    std::mutex mutex;
};

std::unordered_map<std::string, DownloadProgress> active_downloads;
std::mutex downloads_mutex;

// ─── Utility: auto-detect local IP via UDP ─────────────────────────────────
std::string get_local_ip() {
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

// ─── Find free TCP port ──────────────────────────────────────────────────
unsigned short find_free_port() {
    boost::asio::io_context io;
    tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), 0));
    return acceptor.local_endpoint().port();
}

// ─── Tracker Helpers ────────────────────────────────────────────────────
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

// More robust registration with retry
bool register_with_retry(const std::string& tracker_ip, unsigned short tracker_port,
    const std::string& filename, const std::string& my_ip,
    unsigned short my_port, int max_retries = 3) {
    for (int i = 0; i < max_retries; i++) {
        if (register_file_with_tracker(tracker_ip, tracker_port, filename, my_ip, my_port))
            return true;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return false;
}

std::vector<std::string> get_peers_from_tracker(const std::string& tracker_ip,
    unsigned short tracker_port,
    const std::string& filename) {
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

// File integrity checking function
bool verify_file_integrity(const std::string& filename, size_t expected_size) {
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

// ─── P2P Server: serve FILESIZE, SENDCHUNK, full file ─────────────────────
void run_server(unsigned short port) {
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

// ─── Leecher: parallel chunk download ──────────────────────────────────────
size_t get_filesize_from_peer(const std::string& ip,
    unsigned short port,
    const std::string& filename) {
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

void run_leecher_parallel(const std::vector<std::string>& all_peers,
    const std::string& request_fn,
    const std::string& save_fn,
    unsigned short my_port,
    const std::string& tracker_ip,
    unsigned short tracker_port)
{
    // DEBUG: show where we're writing
    {
        std::lock_guard lk(cout_mutex);
        auto cwd = std::filesystem::current_path();
        std::cout << "[Leecher] CWD = " << cwd << "\n"
            << "          will write to: "
            << (cwd / "downloads" / save_fn) << "\n";
    }

    // Filter out self from peers
    auto peers = all_peers;
    std::string self_ep = get_local_ip() + ":" + std::to_string(my_port);
    peers.erase(std::remove(peers.begin(), peers.end(), self_ep), peers.end());
    if (peers.empty()) peers.push_back(self_ep);

    // Get file size from peer
    auto [ip0, port0] = [&]() {
        auto p = peers[0];
        auto pos = p.find(':');
        return std::make_pair(p.substr(0, pos),
            static_cast<unsigned short>(std::stoi(p.substr(pos + 1))));
        }();

    size_t filesize = get_filesize_from_peer(ip0, port0, request_fn);
    if (!filesize) {
        std::lock_guard lk(cout_mutex);
        std::cerr << "[Leecher] Unable to get filesize for " << request_fn << "\n";
        return;
    }

    size_t total_chunks = (filesize + CHUNK_SIZE - 1) / CHUNK_SIZE;

    // Initialize progress tracking
    {
        std::lock_guard lk(downloads_mutex);
        auto& dp = active_downloads[save_fn];
        dp.filename = request_fn;
        dp.total_chunks = total_chunks;
        dp.completed_chunks = 0;
        dp.finished = false;
    }

    // Create download directory if it doesn't exist
    std::filesystem::create_directory("downloads");

    // Open output file - don't pre-size it
    std::ofstream out("downloads/" + save_fn, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::lock_guard lk(cout_mutex);
        std::cerr << "[Leecher] Failed to open output file: downloads/" << save_fn << "\n";
        return;
    }

    // Create work queue for chunks
    std::queue<size_t> work;
    for (size_t i = 0; i < total_chunks; ++i) work.push(i);

    // Mutexes for thread safety
    std::mutex work_mutex, file_mutex;

    // Determine how many threads to use
    size_t max_threads = std::min<size_t>(peers.size(), 8);

    // Track failed chunks to retry
    std::vector<size_t> failed_chunks;
    std::mutex failed_chunks_mutex;
    int max_retries = 3;

    // Create worker threads
    std::vector<std::thread> pool;
    for (size_t t = 0; t < max_threads; ++t) {
        pool.emplace_back([&]() {
            while (true) {
                size_t idx;
                { // Get next chunk from work queue
                    std::lock_guard lk(work_mutex);
                    if (work.empty()) break;
                    idx = work.front();
                    work.pop();
                }

                // Select peer using round-robin
                auto [ip, port] = [&]() {
                    auto p = peers[idx % peers.size()];
                    auto pos = p.find(':');
                    return std::make_pair(p.substr(0, pos),
                        static_cast<unsigned short>(std::stoi(p.substr(pos + 1))));
                    }();

                try {
                    // Set up connection to peer
                    boost::asio::io_context io;
                    tcp::socket sock(io);

                    // Add connection timeout
                    sock.connect({ boost::asio::ip::make_address(ip), port });

                    // Set operation timeout
                   // Set socket receive timeout (platform dependent)
#ifdef _WIN32
    // Windows-specific
                    DWORD timeout_ = 5000; // 5 seconds in milliseconds
                    setsockopt(sock.native_handle(), SOL_SOCKET, SO_RCVTIMEO,
                        (const char*)&timeout_, sizeof(timeout_));
#else
    // POSIX systems (Linux, macOS, etc.)
                    struct timeval tv;
                    tv.tv_sec = 5;  // 5 seconds
                    tv.tv_usec = 0;
                    setsockopt(sock.native_handle(), SOL_SOCKET, SO_RCVTIMEO,
                        (const char*)&tv, sizeof(tv));
#endif
                    // Request specific chunk
                    std::string req = "SENDCHUNK " + request_fn + " " + std::to_string(idx) + "\n";
                    boost::asio::write(sock, boost::asio::buffer(req));

                    // Calculate chunk size - last chunk may be smaller
                    size_t need = std::min(CHUNK_SIZE, filesize - idx * CHUNK_SIZE);
                    std::vector<char> buf(need);
                    size_t got = 0;
                    boost::system::error_code ec;

                    // Read with timeout
                    auto start_time = std::chrono::steady_clock::now();
                    bool timeout = false;

                    while (got < need) {
                        size_t n = sock.read_some(boost::asio::buffer(buf.data() + got, need - got), ec);

                        if (ec) {
                            if (ec != boost::asio::error::eof) {
                                std::lock_guard lk(cout_mutex);
                                std::cerr << "[Leecher] Read error: " << ec.message() << "\n";
                                break;
                            }
                            // EOF is expected when chunk is complete
                            break;
                        }

                        got += n;

                        // Check for timeout (10 seconds total)
                        auto now = std::chrono::steady_clock::now();
                        if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() > 10) {
                            timeout = true;
                            std::lock_guard lk(cout_mutex);
                            std::cerr << "[Leecher] Timeout getting chunk " << idx << "\n";
                            break;
                        }
                    }

                    // Only write if we got all expected data
                    if (got == need) {
                        // Write chunk to file
                        {
                            std::lock_guard lk(file_mutex);
                            out.seekp(idx * CHUNK_SIZE);
                            if (!out) {
                                std::lock_guard log_lk(cout_mutex);
                                std::cerr << "[Leecher] Error seeking to position " << (idx * CHUNK_SIZE) << "\n";

                                // Add to failed chunks
                                std::lock_guard fc_lk(failed_chunks_mutex);
                                failed_chunks.push_back(idx);
                                continue;
                            }

                            out.write(buf.data(), got);
                            if (!out) {
                                std::lock_guard log_lk(cout_mutex);
                                std::cerr << "[Leecher] Error writing chunk " << idx << "\n";

                                std::lock_guard fc_lk(failed_chunks_mutex);
                                failed_chunks.push_back(idx);
                                continue;
                            }

                            out.flush(); // Force write to disk
                        }

                        // Update progress
                        {
                            std::lock_guard lk(downloads_mutex);
                            auto& dp = active_downloads[save_fn];
                            if (++dp.completed_chunks == dp.total_chunks)
                                dp.finished = true;
                        }

                        std::lock_guard lk(cout_mutex);
                        std::cout << "[Leecher] Chunk " << idx
                            << " from " << ip << ":" << port
                            << " (" << got << "/" << need << ")\n";
                    }
                    else {
                        // Re-queue failed chunk if haven't retried too many times
                        std::lock_guard fc_lk(failed_chunks_mutex);
                        if (std::find(failed_chunks.begin(), failed_chunks.end(), idx) == failed_chunks.end()) {
                            std::lock_guard lk(work_mutex);
                            work.push(idx);
                            failed_chunks.push_back(idx);

                            std::lock_guard log_lk(cout_mutex);
                            std::cerr << "[Leecher] Incomplete chunk " << idx
                                << " (" << got << "/" << need << "). Re-queuing.\n";
                        }
                        else {
                            std::lock_guard log_lk(cout_mutex);
                            std::cerr << "[Leecher] Chunk " << idx << " failed multiple times. Giving up.\n";
                        }
                    }
                }
                catch (const std::exception& e) {
                    // Re-queue failed chunk if haven't retried too many times
                    std::lock_guard fc_lk(failed_chunks_mutex);
                    if (std::find(failed_chunks.begin(), failed_chunks.end(), idx) == failed_chunks.end()) {
                        std::lock_guard lk(work_mutex);
                        work.push(idx);
                        failed_chunks.push_back(idx);

                        std::lock_guard log_lk(cout_mutex);
                        std::cerr << "[Leecher] Chunk " << idx << " failed: " << e.what()
                            << ". Re-queuing.\n";
                    }
                    else {
                        std::lock_guard log_lk(cout_mutex);
                        std::cerr << "[Leecher] Chunk " << idx << " failed multiple times. Giving up.\n";
                    }
                }
            }
            });
    }

    // Wait for all worker threads to finish
    for (auto& th : pool) th.join();

    // Close the output file
    out.close();

    // Verify file integrity
    try {
        std::string file_path = "downloads/" + save_fn;
        std::ifstream verify_file(file_path, std::ios::binary);
        if (!verify_file) {
            std::lock_guard lk(cout_mutex);
            std::cerr << "[Leecher] Could not open file for verification\n";
        }
        else {
            verify_file.seekg(0, std::ios::end);
            size_t actual_size = verify_file.tellg();
            verify_file.close();

            std::lock_guard lk(cout_mutex);
            if (actual_size != filesize) {
                std::cerr << "[Leecher] WARNING: File size mismatch! Expected: "
                    << filesize << ", Got: " << actual_size << "\n";
            }
            else {
                std::cout << "[Leecher] File integrity check passed: " << actual_size << " bytes\n";

                // Run deeper verification
                if (verify_file_integrity(file_path, filesize)) {
                    // Get local IP for registration
                    std::string local_ip = get_local_ip();

                    // Auto-register the file with the tracker after successful download
                    std::thread([request_fn, tracker_ip, tracker_port, local_ip, my_port]() {
                        // Register the downloaded file with the tracker so it can be shared
                        bool registered = register_with_retry(
                            tracker_ip, tracker_port,
                            request_fn,  // Use original filename for registration
                            local_ip, my_port);

                        std::lock_guard log_lk(cout_mutex);
                        if (registered) {
                            std::cout << "[AutoSeeder] Successfully registered downloaded file: "
                                << request_fn << " for seeding\n";
                        }
                        else {
                            std::cerr << "[AutoSeeder] Failed to register file: " << request_fn << "\n";
                        }
                        }).detach();
                }
            }
        }
    }
    catch (const std::exception& e) {
        std::lock_guard lk(cout_mutex);
        std::cerr << "[Leecher] File verification error: " << e.what() << "\n";
    }

    std::lock_guard lk(cout_mutex);
    std::cout << "[Leecher] All chunks done. Saved as " << save_fn << "\n";
}

// ─── MAIN ──────────────────────────────────────────────────────────────────
int main() {
    // Detect environment
    std::string tracker_ip = "127.0.0.1";
    unsigned short tracker_port = 8000;
    std::string local_ip = get_local_ip();
    unsigned short p2p_port = find_free_port();
    unsigned short http_port = find_free_port();

    // Create needed directories
    try {
        std::filesystem::create_directory("downloads");
        std::filesystem::create_directory("shared_files");
    }
    catch (...) {}

    // Start P2P server socket concurrently
    std::thread server_thread([&]() { run_server(p2p_port); });

    // Start HTTP UI
    httplib::Server http;

    // Enhanced HTTP UI setup
    http.Get("/", [&](auto& req, auto& res) {
        std::stringstream html;
        html << "<html><head><title>P2P File Sharing</title>";
        html << "<style>";
        html << "body { font-family: Arial, sans-serif; margin: 20px; }";
        html << "h1 { color: #2c3e50; }";
        html << ".info { background: #f8f9fa; padding: 10px; border-radius: 5px; }";
        html << "form { margin: 20px 0; }";
        html << "input, select, button { margin: 5px; padding: 8px; }";
        html << "button { background: #3498db; color: white; border: none; cursor: pointer; }";
        html << ".files { margin-top: 20px; }";
        html << "</style></head><body>";
        html << "<h1>P2P File Sharing</h1>";
        html << "<div class='info'>";
        html << "<p><strong>Your IP:</strong> " << local_ip << "</p>";
        html << "<p><strong>P2P port:</strong> " << p2p_port << "</p>";
        html << "<p><strong>Tracker:</strong> " << tracker_ip << ":" << tracker_port << "</p>";
        html << "</div>";

        // Form for file sharing
        html << "<form action='/share' method='post'>";
        html << "<h3>Share a File</h3>";
        html << "<input type='text' name='filename' placeholder='Path to file'>";
        html << "<button type='submit'>Share</button>";
        html << "</form>";

        // Form for file downloading
        html << "<form action='/download' method='post'>";
        html << "<h3>Download a File</h3>";
        html << "<input type='text' name='filename' placeholder='Filename to download'>";
        html << "<input type='text' name='saveas' placeholder='Save as (optional)'>";
        html << "<button type='submit'>Download</button>";
        html << "</form>";

        // Show available local files
        html << "<div class='files'>";
        html << "<h3>Available Local Files</h3>";
        html << "<ul>";
        try {
            for (const auto& entry : std::filesystem::directory_iterator("downloads")) {
                if (entry.is_regular_file()) {
                    html << "<li>" << entry.path().filename().string()
                        << " (" << entry.file_size() << " bytes)</li>";
                }
            }
        }
        catch (...) {
            html << "<li>No files found or error accessing directory</li>";
        }
        html << "</ul></div>";

        html << "<p><a href='/progress'>View Download Progress</a></p>";

        html << "</body></html>";
        res.set_content(html.str(), "text/html");
        });

    // Handle share form submission
    http.Post("/share", [&](auto& req, auto& res) {
        std::string filename = req.get_param_value("filename");
        std::filesystem::path file_path{ filename };
        bool file_found = false;
        std::vector<std::string> checked_paths;

        // 1. Check absolute path
        if (!file_path.is_absolute()) {
            auto abs_path = std::filesystem::absolute(file_path);
            checked_paths.push_back(abs_path.string());
            file_found = std::filesystem::exists(abs_path);
            if (file_found) file_path = abs_path;
        }

        // 2. Check relative path (original input)
        if (!file_found) {
            checked_paths.push_back(file_path.string());
            file_found = std::filesystem::exists(file_path);
        }

        // 3. Check shared_files directory
        if (!file_found) {
            auto shared_path = std::filesystem::path("shared_files") / filename;
            checked_paths.push_back(shared_path.string());
            file_found = std::filesystem::exists(shared_path);
            if (file_found) file_path = shared_path;
        }

        bool success = false;
        std::string message;

        if (filename.empty()) {
            message = "Error: No filename provided";
        }
        else if (!file_found) {
            std::string paths;
            for (const auto& p : checked_paths) paths += "\n- " + p;
            message = "Error: File not found in:" + paths;
        }
        else {
            // Extract basename and prepare destination
            std::string basename = file_path.filename().string();
            std::filesystem::path dest = std::filesystem::path("shared_files") / basename;

            try {
                // Skip copy if file is already in shared_files
                if (file_path != dest) {
                    std::filesystem::copy_file(
                        file_path,
                        dest,
                        std::filesystem::copy_options::overwrite_existing
                    );
                }

                // Register with tracker
                success = register_with_retry(
                    tracker_ip, tracker_port,
                    basename, local_ip, p2p_port
                );
                message = success ? "File registered successfully" : "Failed to register file";
            }
            catch (const std::exception& e) {
                message = std::string("Error: ") + e.what();
            }
        }

        // Redirect response
        std::stringstream html;
        html << "<html><head><meta http-equiv='refresh' content='2;url=/'></head><body>"
            << "<h2>" << message << "</h2><p>Redirecting back...</p></body></html>";
        res.set_content(html.str(), "text/html");
        });
    // Handle download form submission

    http.Post("/download", [&](auto& req, auto& res) {
        std::string filename = req.get_param_value("filename");
        std::string saveas = req.get_param_value("saveas");

        if (saveas.empty()) saveas = filename;

        std::string message;
        if (filename.empty()) {
            message = "Error: No filename provided";
        }
        else {
            auto peers = get_peers_from_tracker(tracker_ip, tracker_port, filename);
            if (peers.empty()) {
                message = "Error: No peers found for this file";
            }
            else {
                // Start download in separate thread to avoid blocking
                std::thread download_thread([peers, filename, saveas, p2p_port, tracker_ip, tracker_port]() {
                    run_leecher_parallel(peers, filename, saveas, p2p_port, tracker_ip, tracker_port);
                    });
                download_thread.detach();
                message = "Download started for " + filename;
            }
        }

        std::stringstream html;
        html << "<html><head><meta http-equiv='refresh' content='2;url=/'></head><body>";
        html << "<h2>" << message << "</h2>";
        html << "<p>Redirecting back...</p>";
        html << "</body></html>";
        res.set_content(html.str(), "text/html");
        });

    // Add progress endpoint to the HTTP server
    http.Get("/progress", [&](auto& req, auto& res) {
        std::stringstream html;
        html << "<html><head>";
        html << "<meta http-equiv='refresh' content='2'>";
        html << "<style>";
        html << "body { font-family: Arial, sans-serif; margin: 20px; }";
        html << "h1 { color: #2c3e50; }";
        html << ".progress-bar { height: 20px; background-color: #ecf0f1; border-radius: 4px; margin: 10px 0; }";
        html << ".progress-fill { height: 100%; background-color: #3498db; border-radius: 4px; }";
        html << "</style>";
        html << "</head><body>";
        html << "<h1>Download Progress</h1>";

        std::lock_guard<std::mutex> lock(downloads_mutex);
        if (active_downloads.empty()) {
            html << "<p>No active downloads</p>";
        }
        else {
            for (const auto& [filename, progress] : active_downloads) {
                float percent = progress.total_chunks > 0 ?
                    (progress.completed_chunks * 100.0f / progress.total_chunks) : 0.0f;
                html << "<div>";
                html << "<h3>" << filename << " (" << progress.filename << ")</h3>";
                html << "<p>" << progress.completed_chunks << "/" << progress.total_chunks
                    << " chunks (" << percent << "%)</p>";
                html << "<div class='progress-bar'>";
                html << "<div class='progress-fill' style='width: " << percent << "%'></div>";
                html << "</div>";
                html << "<p>" << (progress.finished ? "Complete" : "Downloading...") << "</p>";
                html << "</div>";
            }
        }

        html << "<p><a href='/'>Back to Home</a></p>";
        html << "</body></html>";
        res.set_content(html.str(), "text/html");
        });

    // Start the HTTP server in a separate thread
    std::thread ui_thread([&]() { http.listen(local_ip.c_str(), http_port); });

    std::cout << "HTTP UI running at http://" << local_ip << ":" << http_port << "\n";
    std::cout << "P2P server running on port " << p2p_port << "\n";

    // Handle command-line interface as well
    std::string command;
    while (true) {
        std::cout << "\nCommands:\n";
        std::cout << "1. share <filename> - Share a file\n";
        std::cout << "2. download <filename> [saveas] - Download a file\n";
        std::cout << "3. list - List downloaded files\n";
        std::cout << "4. exit - Exit the program\n";
        std::cout << "> ";

        std::getline(std::cin, command);
        std::istringstream iss(command);
        std::string cmd;
        iss >> cmd;

        if (cmd == "share") {
            std::string filename;
            iss >> filename;
            if (filename.empty()) {
                std::cout << "Error: No filename provided\n";
                continue;
            }

            if (!std::filesystem::exists(filename)) {
                std::cout << "Error: File not found\n";
                continue;
            }

            std::string basename = std::filesystem::path(filename).filename().string();
            std::filesystem::copy_file(
                filename,
                "shared_files/" + basename,
                std::filesystem::copy_options::overwrite_existing
            );

            bool success = register_with_retry(tracker_ip, tracker_port, basename, local_ip, p2p_port);
            std::cout << (success ? "File registered successfully\n" : "Failed to register file\n");
        }
        else if (cmd == "download") {
            std::string filename, saveas;
            iss >> filename;
            iss >> saveas;

            if (filename.empty()) {
                std::cout << "Error: No filename provided\n";
                continue;
            }

            if (saveas.empty()) saveas = filename;

            auto peers = get_peers_from_tracker(tracker_ip, tracker_port, filename);
            if (peers.empty()) {
                std::cout << "Error: No peers found for this file\n";
                continue;
            }

            std::thread download_thread([peers, filename, saveas, p2p_port, tracker_ip, tracker_port]() {
                run_leecher_parallel(peers, filename, saveas, p2p_port, tracker_ip, tracker_port);
                });
            download_thread.detach();
            std::cout << "Download started for " << filename << "\n";
        }
        else if (cmd == "list") {
            std::cout << "Downloaded files:\n";
            try {
                for (const auto& entry : std::filesystem::directory_iterator("downloads")) {
                    if (entry.is_regular_file()) {
                        std::cout << "- " << entry.path().filename().string()
                            << " (" << entry.file_size() << " bytes)\n";
                    }
                }
            }
            catch (...) {
                std::cout << "Error accessing downloads directory\n";
            }
        }
        else if (cmd == "exit") {
            break;
        }
        else {
            std::cout << "Unknown command\n";
        }
    }

    // Clean up
    http.stop();
    ui_thread.join();
    server_thread.join();

    return 0;
}