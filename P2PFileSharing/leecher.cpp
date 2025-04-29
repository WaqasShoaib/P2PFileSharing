#include "leecher.h"

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
