#pragma once
#include "common.h"
#include <vector>
#include <string>
#include "utilities.h"



bool register_file_with_tracker(const std::string& tracker_ip,
    unsigned short tracker_port,
    const std::string& filename,
    const std::string& my_ip,
    unsigned short my_port);
bool register_with_retry(const std::string& tracker_ip, unsigned short tracker_port, const std::string& filename, const std::string& my_ip,
    unsigned short my_port, int max_retries = 3);

std::vector<std::string> get_peers_from_tracker(const std::string& tracker_ip, unsigned short tracker_port, const std::string& filename);
