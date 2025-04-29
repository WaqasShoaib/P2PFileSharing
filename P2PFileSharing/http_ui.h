#pragma once 
#include "httplib.h"
#include <string>
#include <filesystem>
#include <thread>
#include <sstream>

#include "common.h"
#include "tracker_client.h"
#include "leecher.h"


struct PeerInfo;

void setup_http_server(
    httplib::Server& http,
    const std::string& local_ip,
    unsigned short http_port,
    unsigned short p2p_port,
    const std::string& tracker_ip,
    unsigned short tracker_port
);

