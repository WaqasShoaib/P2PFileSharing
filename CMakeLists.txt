cmake_minimum_required(VERSION 3.10)
project(P2PFileSharing)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Include Boost
include_directories("D:/libraries/boost_1_88_0")

# Add executables separately
add_executable(server P2PFileSharing/server.cpp)
add_executable(client P2PFileSharing/client.cpp)
add_executable(peer   P2PFileSharing/peer.cpp)   # NEW: just this line for peer
add_executable(tracker P2PFileSharing/tracker.cpp)
