#pragma once 
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
#include "httplib.h" 
#include "queue"
namespace fs = std::filesystem;
using boost::asio::ip::tcp;
constexpr size_t CHUNK_SIZE = 1024 * 256; // 256KB

extern std::mutex cout_mutex;
extern std::string tracker_ip;
extern unsigned short tracker_port;
extern std::string local_ip;
extern unsigned short p2p_port;

// Progress tracking
struct DownloadProgress 
{
    std::string filename;
    size_t total_chunks;
    size_t completed_chunks;
    bool finished;
    std::mutex mutex;
};

extern std::unordered_map<std::string, DownloadProgress> active_downloads;
extern std::mutex downloads_mutex;