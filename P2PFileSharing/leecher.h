#pragma once
#include <vector>
#include <string>
#include "utilities.h"
#include "common.h"
#include "tracker_client.h"

void run_leecher_parallel(const std::vector<std::string>& all_peers,
    const std::string& request_fn,
    const std::string& save_fn,
    unsigned short my_port,
    const std::string& tracker_ip,
    unsigned short tracker_port);
