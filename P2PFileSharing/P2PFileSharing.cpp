#include "common.h"
#include "server.h"
#include "tracker_client.h"
#include "leecher.h"
#include "http_ui.h"
#include "utilities.h"






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

