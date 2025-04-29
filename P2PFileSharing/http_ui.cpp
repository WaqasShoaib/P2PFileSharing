#include "http_ui.h"





void setup_http_server(
    httplib::Server& http,
    const std::string& local_ip,
    unsigned short http_port,
    unsigned short p2p_port,
    const std::string& tracker_ip,
    unsigned short tracker_port
) {
    // Root page with forms and info
    http.Get("/", [local_ip, p2p_port, tracker_ip, tracker_port](auto& req, auto& res) {
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
    http.Post("/share", [local_ip, p2p_port, tracker_ip, tracker_port](auto& req, auto& res) {
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
    http.Post("/download", [p2p_port, tracker_ip, tracker_port](auto& req, auto& res) {
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
}