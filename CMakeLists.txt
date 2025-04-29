cmake_minimum_required(VERSION 3.10)
project(P2PFileSharing)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Include Boost
include_directories("D:/libraries/boost_1_88_0")



add_executable(run
    P2PFileSharing/P2PFileSharing.cpp
    P2PFileSharing/common.cpp
    P2PFileSharing/utilities.cpp
    P2PFileSharing/server.cpp
    P2PFileSharing/tracker_client.cpp
    P2PFileSharing/leecher.cpp
    P2PFileSharing/http_ui.cpp
)

# Add executables separately
#add_executable(server P2PFileSharing/server.cpp)
#add_executable(client P2PFileSharing/client.cpp)
add_executable(peer   P2PFileSharing/peer.cpp)   # NEW: just this line for peer
add_executable(tracker P2PFileSharing/tracker.cpp)

