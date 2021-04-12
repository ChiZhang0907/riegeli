// Copyright 2017 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Make `pread()` available.
#if !defined(_XOPEN_SOURCE) || _XOPEN_SOURCE < 500
#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif

// Make `off_t` 64-bit even on 32-bit systems.
#undef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64

#include "riegeli/bytes/fd_reader.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <limits>
#include <string>
#include <tuple>

#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "riegeli/base/base.h"
#include "riegeli/base/chain.h"
#include "riegeli/base/errno_mapping.h"
#include "riegeli/base/memory_estimator.h"
#include "riegeli/base/status.h"
#include "riegeli/bytes/buffered_reader.h"
#include "riegeli/bytes/chain_reader.h"

namespace riegeli {

namespace {

class MMapRef {
 public:
  MMapRef() noexcept {}

  MMapRef(const MMapRef&) = delete;
  MMapRef& operator=(const MMapRef&) = delete;

  void operator()(absl::string_view data) const;
  void RegisterSubobjects(MemoryEstimator& memory_estimator) const;
  void DumpStructure(std::ostream& out) const;
};

void MMapRef::operator()(absl::string_view data) const {
  RIEGELI_CHECK_EQ(munmap(const_cast<char*>(data.data()), data.size()), 0)
      << ErrnoToCanonicalStatus(errno, "munmap() failed").message();
}

void MMapRef::RegisterSubobjects(MemoryEstimator& memory_estimator) const {}

void MMapRef::DumpStructure(std::ostream& out) const { out << "[mmap] { }"; }

}  // namespace

namespace internal {

void FdReaderCommon::SetFilename(int src) {
  if (src == 0) {
    filename_ = "/dev/stdin";
  } else {
    filename_ = absl::StrCat("/proc/self/fd/", src);
  }
}

int FdReaderCommon::OpenFd(absl::string_view filename, int flags) {
  // TODO: When `absl::string_view` becomes C++17 `std::string_view`:
  // `filename_ = filename`
  filename_.assign(filename.data(), filename.size());
again:
  const int src = open(filename_.c_str(), flags, 0666);
  if (ABSL_PREDICT_FALSE(src < 0)) {
    if (errno == EINTR) goto again;
    FailOperation("open()");
    return -1;
  }
  return src;
}

bool FdReaderCommon::FailOperation(absl::string_view operation) {
  const int error_number = errno;
  RIEGELI_ASSERT_NE(error_number, 0)
      << "Failed precondition of FdReaderCommon::FailOperation(): "
         "zero errno";
  RIEGELI_ASSERT(is_open())
      << "Failed precondition of FdReaderCommon::FailOperation(): "
         "Object closed";
  return Fail(
      ErrnoToCanonicalStatus(error_number, absl::StrCat(operation, " failed")));
}

bool FdReaderCommon::Fail(absl::Status status) {
  RIEGELI_ASSERT(!status.ok())
      << "Failed precondition of Object::Fail(): status not failed";
  return BufferedReader::Fail(
      Annotate(status, absl::StrCat("reading ", filename_)));
}

}  // namespace internal

void FdReaderBase::InitializePos(int src,
                                 absl::optional<Position> independent_pos) {
  if (independent_pos != absl::nullopt) {
    if (ABSL_PREDICT_FALSE(*independent_pos >
                           Position{std::numeric_limits<off_t>::max()})) {
      FailOverflow();
      return;
    }
    set_limit_pos(*independent_pos);
  } else {
    const off_t file_pos = lseek(src, 0, SEEK_CUR);
    if (ABSL_PREDICT_FALSE(file_pos < 0)) {
      FailOperation("lseek()");
      return;
    }
    set_limit_pos(IntCast<Position>(file_pos));
  }
}

inline bool FdReaderBase::SyncPos(int src) {
  if (!has_independent_pos_) {
    if (ABSL_PREDICT_FALSE(lseek(src, IntCast<off_t>(pos()), SEEK_SET) < 0)) {
      return FailOperation("lseek()");
    }
  }
  return true;
}

void FdReaderBase::Done() {
  if (ABSL_PREDICT_TRUE(healthy()) && available() > 0) {
    const int src = src_fd();
    SyncPos(src);
  }
  FdReaderCommon::Done();
}

bool FdReaderBase::ReadInternal(size_t min_length, size_t max_length,
                                char* dest) {
  RIEGELI_ASSERT_GT(min_length, 0u)
      << "Failed precondition of BufferedReader::ReadInternal(): "
         "nothing to read";
  RIEGELI_ASSERT_GE(max_length, min_length)
      << "Failed precondition of BufferedReader::ReadInternal(): "
         "max_length < min_length";
  RIEGELI_ASSERT(healthy())
      << "Failed precondition of BufferedReader::ReadInternal(): " << status();
  const int src = src_fd();
  if (ABSL_PREDICT_FALSE(max_length >
                         Position{std::numeric_limits<off_t>::max()} -
                             limit_pos())) {
    return FailOverflow();
  }
  for (;;) {
  again:
    const ssize_t length_read =
        has_independent_pos_
            ? pread(src, dest,
                    UnsignedMin(max_length,
                                size_t{std::numeric_limits<ssize_t>::max()}),
                    IntCast<off_t>(limit_pos()))
            : read(src, dest,
                   UnsignedMin(max_length,
                               size_t{std::numeric_limits<ssize_t>::max()}));
    if (ABSL_PREDICT_FALSE(length_read < 0)) {
      if (errno == EINTR) goto again;
      return FailOperation(has_independent_pos_ ? "pread()" : "read()");
    }
    if (ABSL_PREDICT_FALSE(length_read == 0)) return false;
    RIEGELI_ASSERT_LE(IntCast<size_t>(length_read), max_length)
        << (has_independent_pos_ ? "pread()" : "read()")
        << " read more than requested";
    move_limit_pos(IntCast<size_t>(length_read));
    if (IntCast<size_t>(length_read) >= min_length) return true;
    dest += length_read;
    min_length -= IntCast<size_t>(length_read);
    max_length -= IntCast<size_t>(length_read);
  }
}

bool FdReaderBase::Sync() {
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  if (available() > 0) {
    const int src = src_fd();
    return SyncPos(src);
  }
  return true;
}

bool FdReaderBase::SeekSlow(Position new_pos) {
  RIEGELI_ASSERT(new_pos < start_pos() || new_pos > limit_pos())
      << "Failed precondition of Reader::SeekSlow(): "
         "position in the buffer, use Seek() instead";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  const int src = src_fd();
  ClearBuffer();
  if (new_pos > limit_pos()) {
    // Seeking forwards.
    struct stat stat_info;
    if (ABSL_PREDICT_FALSE(fstat(src, &stat_info) < 0)) {
      return FailOperation("fstat()");
    }
    if (ABSL_PREDICT_FALSE(new_pos > IntCast<Position>(stat_info.st_size))) {
      // File ends.
      set_limit_pos(IntCast<Position>(stat_info.st_size));
      SyncPos(src);
      return false;
    }
  }
  set_limit_pos(new_pos);
  return SyncPos(src);
}

absl::optional<Position> FdReaderBase::Size() {
  if (ABSL_PREDICT_FALSE(!healthy())) return absl::nullopt;
  const int src = src_fd();
  struct stat stat_info;
  if (ABSL_PREDICT_FALSE(fstat(src, &stat_info) < 0)) {
    FailOperation("fstat()");
    return absl::nullopt;
  }
  return IntCast<Position>(stat_info.st_size);
}

void FdStreamReaderBase::InitializePos(int src,
                                       absl::optional<Position> assumed_pos) {
  if (assumed_pos != absl::nullopt) {
    set_limit_pos(*assumed_pos);
  } else {
    const off_t file_pos = lseek(src, 0, SEEK_CUR);
    if (ABSL_PREDICT_FALSE(file_pos < 0)) {
      FailOperation("lseek()");
      return;
    }
    set_limit_pos(IntCast<Position>(file_pos));
  }
}

bool FdStreamReaderBase::ReadInternal(size_t min_length, size_t max_length,
                                      char* dest) {
  RIEGELI_ASSERT_GT(min_length, 0u)
      << "Failed precondition of BufferedReader::ReadInternal(): "
         "nothing to read";
  RIEGELI_ASSERT_GE(max_length, min_length)
      << "Failed precondition of BufferedReader::ReadInternal(): "
         "max_length < min_length";
  RIEGELI_ASSERT(healthy())
      << "Failed precondition of BufferedReader::ReadInternal(): " << status();
  const int src = src_fd();
  if (ABSL_PREDICT_FALSE(max_length >
                         std::numeric_limits<Position>::max() - limit_pos())) {
    return FailOverflow();
  }
  for (;;) {
  again:
    const ssize_t length_read = read(
        src, dest,
        UnsignedMin(max_length, size_t{std::numeric_limits<ssize_t>::max()}));
    if (ABSL_PREDICT_FALSE(length_read < 0)) {
      if (errno == EINTR) goto again;
      return FailOperation("read()");
    }
    if (ABSL_PREDICT_FALSE(length_read == 0)) return false;
    RIEGELI_ASSERT_LE(IntCast<size_t>(length_read), max_length)
        << "read() read more than requested";
    move_limit_pos(IntCast<size_t>(length_read));
    if (IntCast<size_t>(length_read) >= min_length) return true;
    dest += length_read;
    min_length -= IntCast<size_t>(length_read);
    max_length -= IntCast<size_t>(length_read);
  }
}

void FdMMapReaderBase::SetFilename(int src) {
  if (src == 0) {
    filename_ = "/dev/stdin";
  } else {
    filename_ = absl::StrCat("/proc/self/fd/", src);
  }
}

int FdMMapReaderBase::OpenFd(absl::string_view filename, int flags) {
  // TODO: When `absl::string_view` becomes C++17 `std::string_view`:
  // `filename_ = filename`
  filename_.assign(filename.data(), filename.size());
again:
  const int src = open(filename_.c_str(), flags, 0666);
  if (ABSL_PREDICT_FALSE(src < 0)) {
    if (errno == EINTR) goto again;
    FailOperation("open()");
    return -1;
  }
  return src;
}

bool FdMMapReaderBase::FailOperation(absl::string_view operation) {
  const int error_number = errno;
  RIEGELI_ASSERT_NE(error_number, 0)
      << "Failed precondition of FdMMapReaderBase::FailOperation(): "
         "zero errno";
  RIEGELI_ASSERT(is_open())
      << "Failed precondition of FdMMapReaderBase::FailOperation(): "
         "Object closed";
  return Fail(
      ErrnoToCanonicalStatus(error_number, absl::StrCat(operation, " failed")));
}

void FdMMapReaderBase::InitializePos(int src,
                                     absl::optional<Position> independent_pos) {
  struct stat stat_info;
  if (ABSL_PREDICT_FALSE(fstat(src, &stat_info) < 0)) {
    FailOperation("fstat()");
    return;
  }
  if (ABSL_PREDICT_FALSE(IntCast<Position>(stat_info.st_size) >
                         std::numeric_limits<size_t>::max())) {
    Fail(absl::OutOfRangeError(absl::StrCat("mmap() cannot be used reading ",
                                            filename_, ": File too large")));
    return;
  }
  if (stat_info.st_size == 0) return;
  void* const data = mmap(nullptr, IntCast<size_t>(stat_info.st_size),
                          PROT_READ, MAP_SHARED, src, 0);
  if (ABSL_PREDICT_FALSE(data == MAP_FAILED)) {
    FailOperation("mmap()");
    return;
  }
  // `FdMMapReaderBase` derives from `ChainReader<Chain>` but the `Chain` to
  // read from was not known in `FdMMapReaderBase` constructor. This sets the
  // `Chain` and updates the `ChainReader` to read from it.
  ChainReader::Reset(std::forward_as_tuple(ChainBlock::FromExternal<MMapRef>(
      std::forward_as_tuple(),
      absl::string_view(static_cast<const char*>(data),
                        IntCast<size_t>(stat_info.st_size)))));
  if (independent_pos != absl::nullopt) {
    move_cursor(UnsignedMin(*independent_pos, available()));
  } else {
    const off_t file_pos = lseek(src, 0, SEEK_CUR);
    if (ABSL_PREDICT_FALSE(file_pos < 0)) {
      FailOperation("lseek()");
      return;
    }
    move_cursor(UnsignedMin(IntCast<Position>(file_pos), available()));
  }
}

inline bool FdMMapReaderBase::SyncPos(int src) {
  if (!has_independent_pos_) {
    if (ABSL_PREDICT_FALSE(lseek(src, IntCast<off_t>(pos()), SEEK_SET) < 0)) {
      return FailOperation("lseek()");
    }
  }
  return true;
}

void FdMMapReaderBase::Done() {
  if (ABSL_PREDICT_TRUE(healthy())) {
    const int src = src_fd();
    SyncPos(src);
  }
  ChainReader::Done();
  ChainReader::src().Clear();
}

bool FdMMapReaderBase::Fail(absl::Status status) {
  RIEGELI_ASSERT(!status.ok())
      << "Failed precondition of Object::Fail(): status not failed";
  return ChainReader::Fail(
      Annotate(status, absl::StrCat("reading ", filename_)));
}

bool FdMMapReaderBase::Sync() {
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  const int src = src_fd();
  return SyncPos(src);
}

}  // namespace riegeli
