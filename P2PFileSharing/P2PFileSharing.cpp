#include "common.h"
#include "server.h"
#include "tracker_client.h"
#include "leecher.h"
#include "http_ui.h"
#include "utilities.h"



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

    // Set up the HTTP server with all routes
    setup_http_server(http, local_ip, http_port, p2p_port, tracker_ip, tracker_port);

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

