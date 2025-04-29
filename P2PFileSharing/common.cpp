#include "common.h"

// Initialize globals
std::string tracker_ip;
unsigned short tracker_port;
std::string local_ip;
unsigned short p2p_port;
std::unordered_map<std::string, DownloadProgress> active_downloads;
std::mutex downloads_mutex;
std::mutex cout_mutex;
