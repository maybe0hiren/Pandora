cmake_minimum_required(VERSION 3.16)
project(Pandora LANGUAGES CXX)

# Integration build. Each module also builds standalone from its own folder.
add_subdirectory(shared)
add_subdirectory(metadata)
# add_subdirectory(networking)   # Member 1
# add_subdirectory(storage)      # Member 2
# add_subdirectory(security)     # Member 4
