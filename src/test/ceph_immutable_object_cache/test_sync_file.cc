#include "gtest/gtest.h"
#include "include/Context.h"
#include "include/buffer_fwd.h"
#include "common/Mutex.h"
#include "common/Cond.h"
#include "global/global_init.h"
#include "common/ceph_argparse.h"
#include "global/global_context.h"

#include "librbd/cache/SharedPersistentObjectCacherFile.h"

using namespace librbd::cache;

int clean_file(std::string file) {
  
  std::string cache_file(g_ceph_context->_conf.get_val<std::string>("rbd_shared_cache_path") 
    + "/ceph_immutable_obj_cache/" + file);
   
  return std::remove(cache_file.c_str());
}

TEST(TestSyncFile, test_create_file) {
  clean_file("test_sync_file");
  SyncFile* m_sync_file = new SyncFile(g_ceph_context, "test_sync_file");
  ASSERT_TRUE(m_sync_file->create() >  0);
  ASSERT_TRUE(m_sync_file->get_file_size() == 0);
  delete m_sync_file;
  clean_file("test_sync_file");
}


TEST(TestSyncFile, test_open_file) {
  clean_file("test_sync_file");
  SyncFile* m_sync_file = new SyncFile(g_ceph_context, "test_sync_file");
  ASSERT_EQ(m_sync_file->open_file(), -1);
  ASSERT_GT(m_sync_file->create(), 0);
  ASSERT_GT(m_sync_file->open_file(), 0);
  delete m_sync_file; 
  clean_file("test_sync_file");
}

TEST(TestSyncFile, test_write_object_to_file) {
  clean_file("test_sync_file_1");
  clean_file("test_sync_file_2");
  SyncFile* m_sync_file_1 = new SyncFile(g_ceph_context, "test_sync_file_1");
  SyncFile* m_sync_file_2 = new SyncFile(g_ceph_context, "test_sync_file_2");
  ASSERT_GT(m_sync_file_1->create(), 0);
  ASSERT_GT(m_sync_file_2->create(), 0);
  ASSERT_TRUE(m_sync_file_1->get_file_size() == 0);
  ASSERT_TRUE(m_sync_file_2->get_file_size() == 0);
  bufferlist* buf_1 = new ceph::bufferlist();
  bufferlist* buf_2 = new ceph::bufferlist();
  buf_1->append(std::string(1024, '0'));
  buf_2->append(std::string(4096, '0'));
  ASSERT_TRUE(m_sync_file_1->write_object_to_file(*buf_1, 1024) == 1024);
  ASSERT_TRUE(m_sync_file_2->write_object_to_file(*buf_2, 4096) == 4096);
  ASSERT_TRUE(m_sync_file_1->get_file_size() == 1024);
  ASSERT_TRUE(m_sync_file_2->get_file_size() == 4096);
  delete m_sync_file_1;
  delete m_sync_file_2;
  delete buf_1;
  delete buf_2;
  clean_file("test_sync_file_1");
  clean_file("test_sync_file_2");
}

TEST(TestSyncFile, test_read_object_from_file) {
  clean_file("test_sync_file_1");
  clean_file("test_sync_file_2");
  SyncFile* m_sync_file_1 = new SyncFile(g_ceph_context, "test_sync_file_1");
  SyncFile* m_sync_file_2 = new SyncFile(g_ceph_context, "test_sync_file_2");
  bufferlist* buf_1 = new ceph::bufferlist();
  bufferlist* buf_2 = new ceph::bufferlist();

  ASSERT_EQ(m_sync_file_1->read_object_from_file(buf_1, 0, 1024), -1);
  ASSERT_EQ(m_sync_file_2->read_object_from_file(buf_2, 0, 1024), -1);

  ASSERT_GT(m_sync_file_1->create(), 0);
  ASSERT_GT(m_sync_file_2->create(), 0);
  ASSERT_TRUE(m_sync_file_1->get_file_size() == 0);
  ASSERT_TRUE(m_sync_file_2->get_file_size() == 0);
  ASSERT_EQ(m_sync_file_1->read_object_from_file(buf_1, 0, 1024), 0);
  ASSERT_EQ(m_sync_file_2->read_object_from_file(buf_2, 0, 1024), 0);

  buf_1->append(std::string(1024, '0'));
  buf_2->append(std::string(4096, '2'));
  ASSERT_TRUE(m_sync_file_1->write_object_to_file(*buf_1, 1024) == 1024);
  ASSERT_TRUE(m_sync_file_2->write_object_to_file(*buf_2, 4096) == 4096);
  ASSERT_TRUE(m_sync_file_1->get_file_size() == 1024);
  ASSERT_TRUE(m_sync_file_2->get_file_size() == 4096);
  ASSERT_EQ(m_sync_file_1->read_object_from_file(buf_1, 0, 1024), 1024);
  ASSERT_EQ(m_sync_file_2->read_object_from_file(buf_2, 0, 4096), 4096);

  delete m_sync_file_1;
  delete m_sync_file_2;
  delete buf_1;
  delete buf_2;
  clean_file("test_sync_file_1");
  clean_file("test_sync_file_2");
}
