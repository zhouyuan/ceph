// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "librbd/cache/file/AioFile.h"
#include "include/Context.h"
#include "common/dout.h"
#include "common/WorkQueue.h"
#include "librbd/ImageCtx.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <utility>

#define dout_subsys ceph_subsys_rbd
#undef dout_prefix
#define dout_prefix *_dout << "librbd::file::AioFile: " << this << " " \
                           <<  __func__ << ": "

namespace librbd {
namespace cache {
namespace file {

namespace {

// helper for supporting lamba move captures
template <typename T, typename F>
struct CaptureImpl {
    T t;
    F f;

    CaptureImpl(T &&t, F &&f) : t(std::forward<T>(t)), f(std::forward<F>(f)) {
    }

    template <typename ...Ts> auto operator()(Ts&&...args )
      -> decltype(f(t, std::forward<Ts>(args)...)) {
        return f(t, std::forward<Ts>(args)...);
    }

    template <typename ...Ts> auto operator()(Ts&&...args) const
      -> decltype(f(t, std::forward<Ts>(args)...))
    {
      return f(t, std::forward<Ts>(args)...);
    }
};

template <typename T, typename F>
CaptureImpl<T, F> make_capture(T &&t, F &&f) {
    return CaptureImpl<T, F>(std::forward<T>(t), std::forward<F>(f) );
}

} // anonymous namespace

template <typename I>
AioFile<I>::AioFile(I &image_ctx, ContextWQ &work_queue,
                    const std::string &name)
  : m_image_ctx(image_ctx), m_work_queue(work_queue),
    m_name("/fast/rbd_cache." + name) /* TODO */ {
}

template <typename I>
AioFile<I>::~AioFile() {
  // TODO force proper cleanup
  if (m_fd != -1) {
    ::close(m_fd);
  }
}

template <typename I>
void AioFile<I>::open(Context *on_finish) {
  m_work_queue.queue(new FunctionContext([this, on_finish](int r) {
      while (true) {
        m_fd = ::open(m_name.c_str(), O_CREAT | O_NOATIME | O_RDWR,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
        if (m_fd == -1) {
          r = -errno;
          if (r == -EINTR) {
            continue;
          }
          on_finish->complete(r);
          return;
        }
        break;
      }

      on_finish->complete(0);
  }));
}

template <typename I>
void AioFile<I>::close(Context *on_finish) {
  assert(m_fd >= 0);
  m_work_queue.queue(new FunctionContext([this, on_finish](int r) {
      while (true) {
        r = ::close(m_fd);
        if (r == -1) {
          r = -errno;
          if (r == -EINTR) {
            continue;
          }
          on_finish->complete(r);
          return;
        }
        break;
      }

      m_fd = -1;
      on_finish->complete(0);
  }));
}

template <typename I>
void AioFile<I>::read(uint64_t offset, uint64_t length, ceph::bufferlist *bl,
                      Context *on_finish) {
  m_work_queue.queue(new FunctionContext(
    [this, offset, length, bl, on_finish](int r) {
      bufferptr bp = buffer::create(length);

      char *buffer = reinterpret_cast<char *>(bp.c_str());
      uint64_t count = 0;
      while (count < length) {
        ssize_t ret_val = pread64(m_fd, buffer, length - count, offset + count);
        if (ret_val == 0) {
          break;
        } else if (ret_val < 0) {
          r = -errno;
          if (r == -EINTR) {
            continue;
          }

          on_finish->complete(r);
        }

        count += ret_val;
        buffer += ret_val;
      }

      on_finish->complete(0);
    }));
}

template <typename I>
void AioFile<I>::write(uint64_t offset, ceph::bufferlist &&bl,
                       Context *on_finish) {
  m_work_queue.queue(new FunctionContext(
    make_capture(std::move(bl), [this, offset, on_finish](bufferlist &bl,
                                                          int r) {
      r = bl.write_fd(m_fd, offset);
      on_finish->complete(r);
    })));
}

template <typename I>
void AioFile<I>::discard(uint64_t offset, uint64_t length, Context *on_finish) {
  m_work_queue.queue(new FunctionContext(
    [this, offset, length, on_finish](int r) {
      while (true) {
        r = fallocate(m_fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                      offset, length);
        if (r == -1) {
          r = -errno;
          if (r == EINTR) {
            continue;
          }
          on_finish->complete(r);
          return;
        }
        break;
      }
      on_finish->complete(0);
    }));
}

template <typename I>
void AioFile<I>::truncate(uint64_t length, Context *on_finish) {
  m_work_queue.queue(new FunctionContext([this, length, on_finish](int r) {
      while (true) {
        r = ftruncate(m_fd, length);
        if (r == -1) {
          r = -errno;
          if (r == EINTR) {
            continue;
          }
          on_finish->complete(r);
          return;
        }
        break;
      }
      on_finish->complete(0);
    }));
}

template <typename I>
void AioFile<I>::fsync(Context *on_finish) {
  m_work_queue.queue(new FunctionContext([this, on_finish](int r) {
      r = ::fsync(m_fd);
      if (r == -1) {
        r = -errno;
        on_finish->complete(r);
        return;
      }
      on_finish->complete(0);
    }));
}

template <typename I>
void AioFile<I>::fdatasync(Context *on_finish) {
  m_work_queue.queue(new FunctionContext([this, on_finish](int r) {
      r = ::fdatasync(m_fd);
      if (r == -1) {
        r = -errno;
        on_finish->complete(r);
        return;
      }
      on_finish->complete(0);
    }));
}

} // namespace file
} // namespace cache
} // namespace librbd

template class librbd::cache::file::AioFile<librbd::ImageCtx>;
