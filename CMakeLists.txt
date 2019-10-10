cmake_minimum_required (VERSION 3.13.1)
project (channel)

add_executable (channel_test src/channel_test.cpp)

set_property(TARGET channel_test PROPERTY CXX_STANDARD 17)

include_directories(./include /usr/local/include)
target_link_libraries(channel_test /usr/local/lib/libgtest.a)


