#pragma once
#include <string>

std::string get_local_ip();
unsigned short find_free_port();
bool verify_file_integrity(const std::string& filename, size_t expected_size);
size_t get_filesize_from_peer(const std::string& ip,unsigned short port,const std::string& filename);