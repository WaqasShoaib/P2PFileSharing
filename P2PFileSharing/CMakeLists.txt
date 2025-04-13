# CMakeList.txt : CMake project for P2PFileSharing, include source and define
# project specific logic here.
#

# Add source to this project's executable.
add_executable (P2PFileSharing "P2PFileSharing.cpp" "P2PFileSharing.h" "server.cpp" "client.cpp")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET P2PFileSharing PROPERTY CXX_STANDARD 20)
endif()

# TODO: Add tests and install targets if needed.
