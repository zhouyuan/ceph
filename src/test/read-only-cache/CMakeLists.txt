

add_executable(read_only_cache_simple_policy  test_simple_policy.cc)

target_link_libraries(read_only_cache_simple_policy os global ${BLKID_LIBRARIES} ${CMAKE_DL_LIBS} ${UNITTEST_LIBS})
