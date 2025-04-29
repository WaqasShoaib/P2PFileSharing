#include "http_ui.h"
#include <iomanip>
#include <ctime>


std::string format_file_size(uintmax_t size) 
{
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    int unit_index = 0;
    double size_d = static_cast<double>(size);

    while (size_d >= 1024.0 && unit_index < 4) {
        size_d /= 1024.0;
        unit_index++;
    }

    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << size_d << " " << units[unit_index];
    return ss.str();
}

// Helper function to get the current time as a string
std::string get_current_time() 
{
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}


std::string get_common_css() {
    std::stringstream css;
    css << "<style>";
    css << "* { box-sizing: border-box; }";
    css << "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 0; background-color: #f5f5f5; color: #333; }";
    css << ".container { max-width: 1200px; margin: 0 auto; padding: 20px; }";
    css << "header { background-color: #2c3e50; color: white; padding: 20px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
    css << "h1 { margin: 0; font-size: 24px; }";
    css << "h2 { color: #2c3e50; margin-top: 30px; border-bottom: 2px solid #eee; padding-bottom: 10px; }";
    css << "h3 { color: #3498db; margin-top: 20px; }";
    css << ".card { background: white; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.05); padding: 20px; margin-bottom: 20px; }";
    css << ".info-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 15px; margin-bottom: 20px; }";
    css << ".info-card { background: #f8f9fa; padding: 15px; border-radius: 8px; border-left: 4px solid #3498db; }";
    css << ".info-card strong { display: block; margin-bottom: 5px; color: #2c3e50; }";
    css << "form { margin: 20px 0; display: flex; flex-direction: column; }";
    css << ".form-row { display: flex; gap: 10px; margin-bottom: 10px; flex-wrap: wrap; }";
    css << "input[type='text'] { flex: 1; padding: 12px; border: 1px solid #ddd; border-radius: 4px; font-size: 14px; min-width: 200px; }";
    css << "button { background: #3498db; color: white; border: none; padding: 12px 20px; border-radius: 4px; cursor: pointer; font-weight: bold; transition: background 0.2s; }";
    css << "button:hover { background: #2980b9; }";
    css << ".file-list { list-style-type: none; padding: 0; }";
    css << ".file-item { display: flex; justify-content: space-between; padding: 12px; background: white; margin-bottom: 8px; border-radius: 4px; box-shadow: 0 1px 3px rgba(0,0,0,0.05); }";
    css << ".file-name { font-weight: bold; color: #2c3e50; }";
    css << ".file-size { color: #7f8c8d; font-size: 0.9em; }";
    css << ".progress-bar { height: 20px; background-color: #ecf0f1; border-radius: 4px; margin: 10px 0; overflow: hidden; }";
    css << ".progress-fill { height: 100%; background-color: #3498db; border-radius: 4px; transition: width 0.3s ease; }";
    css << ".badge { display: inline-block; padding: 5px 10px; border-radius: 20px; font-size: 12px; font-weight: bold; text-transform: uppercase; }";
    css << ".badge-success { background-color: #2ecc71; color: white; }";
    css << ".badge-progress { background-color: #f39c12; color: white; }";
    css << ".button-row { display: flex; gap: 10px; margin-top: 20px; }";
    css << ".nav-link { display: inline-block; background: #3498db; color: white; padding: 10px 15px; text-decoration: none; border-radius: 5px; margin-right: 10px; }";
    css << ".nav-link:hover { background: #2980b9; }";
    css << "footer { margin-top: 40px; text-align: center; color: #7f8c8d; font-size: 0.9em; padding: 20px; }";
    css << "@media (max-width: 768px) { .form-row { flex-direction: column; } input[type='text'] { width: 100%; } }";
    css << "</style>";
    return css.str();
}

// Helper function to include common header
std::string get_page_header(const std::string& title) {
    std::stringstream header;
    header << "<!DOCTYPE html><html lang='en'><head>";
    header << "<meta charset='UTF-8'>";
    header << "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    header << "<title>" << title << " - P2P File Sharing</title>";
    header << get_common_css();
    header << "</head><body>";
    header << "<header><div class='container'>";
    header << "<h1>P2P File Sharing System</h1>";
    header << "</div></header>";
    header << "<div class='container'>";
    return header.str();
}

// Helper function to include common footer
std::string get_page_footer() {
    std::stringstream footer;
    footer << "<footer>P2P File Sharing Application &copy; " << get_current_time().substr(0, 4) << "</footer>";
    footer << "</div></body></html>";
    return footer.str();
}

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
        html << get_page_header("Home");

        // System information cards
        html << "<h2>System Information</h2>";
        html << "<div class='info-grid'>";
        html << "<div class='info-card'><strong>Your IP Address</strong>" << local_ip << "</div>";
        html << "<div class='info-card'><strong>P2P Port</strong>" << p2p_port << "</div>";
        html << "<div class='info-card'><strong>Tracker</strong>" << tracker_ip << ":" << tracker_port << "</div>";
        html << "<div class='info-card'><strong>Current Time</strong>" << get_current_time() << "</div>";
        html << "</div>";

        // Navigation buttons
        html << "<div class='button-row'>";
        html << "<a href='/progress' class='nav-link'>View Downloads</a>";
        html << "<a href='/available' class='nav-link'>Available Files</a>";
        html << "</div>";

        // Share file card
        html << "<div class='card'>";
        html << "<h2>Share a File</h2>";
        html << "<p>Select a file from your computer to share it on the P2P network.</p>";
        html << "<form action='/share' method='post'>";
        html << "<div class='form-row'>";
        html << "<input type='text' name='filename' placeholder='Path to file' required>";
        html << "<button type='submit'>Share File</button>";
        html << "</div>";
        html << "</form>";
        html << "</div>";

        // Download file card
        html << "<div class='card'>";
        html << "<h2>Download a File</h2>";
        html << "<p>Enter the name of a file to download from the P2P network.</p>";
        html << "<form action='/download' method='post'>";
        html << "<div class='form-row'>";
        html << "<input type='text' name='filename' placeholder='Filename to download' required>";
        html << "</div>";
        html << "<div class='form-row'>";
        html << "<input type='text' name='saveas' placeholder='Save as (optional)'>";
        html << "<button type='submit'>Download File</button>";
        html << "</div>";
        html << "</form>";
        html << "</div>";

        // Downloaded files list
        html << "<div class='card'>";
        html << "<h2>Your Downloaded Files</h2>";

        bool has_files = false;
        html << "<ul class='file-list'>";
        try {
            for (const auto& entry : std::filesystem::directory_iterator("downloads")) {
                if (entry.is_regular_file()) {
                    has_files = true;
                    html << "<li class='file-item'>";
                    html << "<span class='file-name'>" << entry.path().filename().string() << "</span>";
                    html << "<span class='file-size'>" << format_file_size(entry.file_size()) << "</span>";
                    html << "</li>";
                }
            }
        }
        catch (...) {
            html << "<li>Error accessing downloads directory</li>";
        }

        if (!has_files) {
            html << "<li>No downloaded files yet</li>";
        }

        html << "</ul>";
        html << "</div>";

        html << get_page_footer();
        res.set_content(html.str(), "text/html");
        });

    // Available files page
    http.Get("/available", [local_ip, p2p_port, tracker_ip, tracker_port](auto& req, auto& res) {
        std::stringstream html;
        html << get_page_header("Available Files");

        html << "<h2>Available Shared Files</h2>";
        html << "<p>Files available in your shared directory:</p>";

        // Shared files list
        html << "<div class='card'>";
        bool has_files = false;
        html << "<ul class='file-list'>";
        try {
            for (const auto& entry : std::filesystem::directory_iterator("shared_files")) {
                if (entry.is_regular_file()) {
                    has_files = true;
                    html << "<li class='file-item'>";
                    html << "<span class='file-name'>" << entry.path().filename().string() << "</span>";
                    html << "<span class='file-size'>" << format_file_size(entry.file_size()) << "</span>";
                    html << "</li>";
                }
            }
        }
        catch (...) {
            html << "<li>Error accessing shared files directory</li>";
        }

        if (!has_files) {
            html << "<li>No shared files yet</li>";
        }

        html << "</ul>";
        html << "</div>";

        html << "<div class='button-row'>";
        html << "<a href='/' class='nav-link'>Back to Home</a>";
        html << "</div>";

        html << get_page_footer();
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
        std::string status_class = "card";

        if (filename.empty()) {
            message = "Error: No filename provided";
            status_class = "card error";
        }
        else if (!file_found) {
            std::string paths;
            for (const auto& p : checked_paths) paths += "<li>" + p + "</li>";
            message = "<p>Error: File not found in:</p><ul>" + paths + "</ul>";
            status_class = "card error";
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

                if (success) {
                    message = "File <strong>" + basename + "</strong> registered successfully with the tracker!";
                    status_class = "card success";
                }
                else {
                    message = "Failed to register file with the tracker. Please try again.";
                    status_class = "card error";
                }
            }
            catch (const std::exception& e) {
                message = std::string("Error: ") + e.what();
                status_class = "card error";
            }
        }

        // Response page
        std::stringstream html;
        html << get_page_header("File Sharing Result");

        html << "<h2>File Sharing Result</h2>";
        html << "<div class='" << status_class << "'>";
        html << "<h3>" << (success ? "Success!" : "Error") << "</h3>";
        html << "<p>" << message << "</p>";
        html << "</div>";

        html << "<div class='button-row'>";
        html << "<a href='/' class='nav-link'>Back to Home</a>";
        html << "</div>";

        html << get_page_footer();

        res.set_content(html.str(), "text/html");
        });

    // Handle download form submission
    http.Post("/download", [p2p_port, tracker_ip, tracker_port](auto& req, auto& res) {
        std::string filename = req.get_param_value("filename");
        std::string saveas = req.get_param_value("saveas");

        if (saveas.empty()) saveas = filename;

        std::string message;
        std::string status_class = "card";
        bool success = false;

        if (filename.empty()) {
            message = "Error: No filename provided";
            status_class = "card error";
        }
        else {
            auto peers = get_peers_from_tracker(tracker_ip, tracker_port, filename);
            if (peers.empty()) {
                message = "Error: No peers found for this file. The file may not exist on the network.";
                status_class = "card error";
            }
            else {
                // Start download in separate thread to avoid blocking
                std::thread download_thread([peers, filename, saveas, p2p_port, tracker_ip, tracker_port]() {
                    run_leecher_parallel(peers, filename, saveas, p2p_port, tracker_ip, tracker_port);
                    });
                download_thread.detach();
                message = "Download started for <strong>" + filename + "</strong>";
                if (saveas != filename) {
                    message += " (saving as <strong>" + saveas + "</strong>)";
                }
                message += ". The file will be saved to the 'downloads' directory.";
                status_class = "card success";
                success = true;
            }
        }

        // Response page
        std::stringstream html;
        html << get_page_header("Download Result");

        html << "<h2>Download Result</h2>";
        html << "<div class='" << status_class << "'>";
        html << "<h3>" << (success ? "Download Started" : "Error") << "</h3>";
        html << "<p>" << message << "</p>";
        html << "</div>";

        html << "<div class='button-row'>";
        html << "<a href='/' class='nav-link'>Back to Home</a>";
        html << "<a href='/progress' class='nav-link'>View Download Progress</a>";
        html << "</div>";

        html << get_page_footer();

        res.set_content(html.str(), "text/html");
        });

    // Add progress endpoint to the HTTP server
    http.Get("/progress", [](auto& req, auto& res) {
        std::stringstream html;
        html << get_page_header("Download Progress");
        html << "<meta http-equiv='refresh' content='3'>";

        html << "<h2>Active Downloads</h2>";
        html << "<p>This page refreshes automatically every 3 seconds.</p>";

        std::lock_guard<std::mutex> lock(downloads_mutex);
        if (active_downloads.empty()) {
            html << "<div class='card'>";
            html << "<p>No active downloads at the moment.</p>";
            html << "</div>";
        }
        else {
            for (const auto& [filename, progress] : active_downloads) {
                float percent = progress.total_chunks > 0 ?
                    (progress.completed_chunks * 100.0f / progress.total_chunks) : 0.0f;

                html << "<div class='card'>";
                html << "<h3>" << filename;
                if (filename != progress.filename) {
                    html << " <small>(saving as " << progress.filename << ")</small>";
                }
                html << "</h3>";

                // Status badge
                html << "<span class='badge " << (progress.finished ? "badge-success" : "badge-progress") << "'>";
                html << (progress.finished ? "Completed" : "Downloading");
                html << "</span>";

                // Progress information
                html << "<p>Progress: " << progress.completed_chunks << " of " << progress.total_chunks << " chunks</p>";

                // Progress bar
                html << "<div class='progress-bar'>";
                html << "<div class='progress-fill' style='width: " << percent << "%'></div>";
                html << "</div>";

                // Percentage display
                html << "<p>" << std::fixed << std::setprecision(1) << percent << "% complete</p>";

                html << "</div>";
            }
        }

        html << "<div class='button-row'>";
        html << "<a href='/' class='nav-link'>Back to Home</a>";
        html << "</div>";

        html << get_page_footer();
        res.set_content(html.str(), "text/html");
        });
}