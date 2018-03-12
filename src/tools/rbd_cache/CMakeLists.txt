add_executable(rbd-cache
  ${CMAKE_SOURCE_DIR}/src/os/CacheStore/SyncFile.cc
  #imagecache/ImageStore.cc
  #imagecache/MetaStore.cc
  main.cc)
target_link_libraries(rbd-cache
  librados
  global)
install(TARGETS rbd-cache DESTINATION bin)
