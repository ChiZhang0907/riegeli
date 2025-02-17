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

#ifndef RIEGELI_ENDIAN_ENDIAN_READING_H_
#define RIEGELI_ENDIAN_ENDIAN_READING_H_

#include <stdint.h>

#include <cstring>

#include "absl/base/optimization.h"
#include "absl/types/optional.h"
#include "riegeli/bytes/reader.h"
#include "riegeli/endian/endian_common.h"

namespace riegeli {

// Reads a number in a fixed width Little/Big Endian encoding.
//
// Returns `absl::nullopt` on failure, with the current position unchanged.
absl::optional<uint16_t> ReadLittleEndian16(Reader& src);
absl::optional<uint32_t> ReadLittleEndian32(Reader& src);
absl::optional<uint64_t> ReadLittleEndian64(Reader& src);
absl::optional<uint16_t> ReadBigEndian16(Reader& src);
absl::optional<uint32_t> ReadBigEndian32(Reader& src);
absl::optional<uint64_t> ReadBigEndian64(Reader& src);

// Reads an array of numbers in a fixed width Little/Big Endian encoding.
//
// This is faster than reading them individually for native endianness.
//
// Returns `false` on failure, with unspecified current position.
bool ReadLittleEndian16s(Reader& src, absl::Span<uint16_t> dest);
bool ReadLittleEndian32s(Reader& src, absl::Span<uint32_t> dest);
bool ReadLittleEndian64s(Reader& src, absl::Span<uint64_t> dest);
bool ReadBigEndian16s(Reader& src, absl::Span<uint16_t> dest);
bool ReadBigEndian32s(Reader& src, absl::Span<uint32_t> dest);
bool ReadBigEndian64s(Reader& src, absl::Span<uint64_t> dest);

// Reads a number in a fixed width Little/Big Endian encoding from an array.
//
// Reads `sizeof(uint{16,32,64}_t)` bytes  from `src[]`.
uint16_t ReadLittleEndian16(const char* src);
uint32_t ReadLittleEndian32(const char* src);
uint64_t ReadLittleEndian64(const char* src);
uint16_t ReadBigEndian16(const char* src);
uint32_t ReadBigEndian32(const char* src);
uint64_t ReadBigEndian64(const char* src);

// Reads an array of numbers in a fixed width Little/Big Endian encoding from an
// array.
//
// This is faster than reading them individually for native endianness.
//
// Reads `dest.size() * sizeof(uint{16,32,64}_t)` bytes  from `src[]`.
void ReadLittleEndian16s(const char* src, absl::Span<uint16_t> dest);
void ReadLittleEndian32s(const char* src, absl::Span<uint32_t> dest);
void ReadLittleEndian64s(const char* src, absl::Span<uint64_t> dest);
void ReadBigEndian16s(const char* src, absl::Span<uint16_t> dest);
void ReadBigEndian32s(const char* src, absl::Span<uint32_t> dest);
void ReadBigEndian64s(const char* src, absl::Span<uint64_t> dest);

// Implementation details follow.

inline absl::optional<uint16_t> ReadLittleEndian16(Reader& src) {
  if (ABSL_PREDICT_FALSE(!src.Pull(sizeof(uint16_t)))) return absl::nullopt;
  const uint16_t data = ReadLittleEndian16(src.cursor());
  src.move_cursor(sizeof(uint16_t));
  return data;
}

inline absl::optional<uint32_t> ReadLittleEndian32(Reader& src) {
  if (ABSL_PREDICT_FALSE(!src.Pull(sizeof(uint32_t)))) return absl::nullopt;
  const uint32_t data = ReadLittleEndian32(src.cursor());
  src.move_cursor(sizeof(uint32_t));
  return data;
}

inline absl::optional<uint64_t> ReadLittleEndian64(Reader& src) {
  if (ABSL_PREDICT_FALSE(!src.Pull(sizeof(uint64_t)))) return absl::nullopt;
  const uint64_t data = ReadLittleEndian64(src.cursor());
  src.move_cursor(sizeof(uint64_t));
  return data;
}

inline absl::optional<uint16_t> ReadBigEndian16(Reader& src) {
  if (ABSL_PREDICT_FALSE(!src.Pull(sizeof(uint16_t)))) return absl::nullopt;
  const uint16_t data = ReadBigEndian16(src.cursor());
  src.move_cursor(sizeof(uint16_t));
  return data;
}

inline absl::optional<uint32_t> ReadBigEndian32(Reader& src) {
  if (ABSL_PREDICT_FALSE(!src.Pull(sizeof(uint32_t)))) return absl::nullopt;
  const uint32_t data = ReadBigEndian32(src.cursor());
  src.move_cursor(sizeof(uint32_t));
  return data;
}

inline absl::optional<uint64_t> ReadBigEndian64(Reader& src) {
  if (ABSL_PREDICT_FALSE(!src.Pull(sizeof(uint64_t)))) return absl::nullopt;
  const uint64_t data = ReadBigEndian64(src.cursor());
  src.move_cursor(sizeof(uint64_t));
  return data;
}

inline bool ReadLittleEndian16s(Reader& src, absl::Span<uint16_t> dest) {
  if (internal::IsLittleEndian()) {
    return src.Read(dest.size() * sizeof(uint16_t),
                    reinterpret_cast<char*>(dest.data()));
  } else {
    for (uint16_t& dest_value : dest) {
      const absl::optional<uint16_t> src_value = ReadLittleEndian16(src);
      if (ABSL_PREDICT_FALSE(src_value == absl::nullopt)) return false;
      dest_value = *src_value;
    }
    return true;
  }
}

inline bool ReadLittleEndian32s(Reader& src, absl::Span<uint32_t> dest) {
  if (internal::IsLittleEndian()) {
    return src.Read(dest.size() * sizeof(uint32_t),
                    reinterpret_cast<char*>(dest.data()));
  } else {
    for (uint32_t& dest_value : dest) {
      const absl::optional<uint32_t> src_value = ReadLittleEndian32(src);
      if (ABSL_PREDICT_FALSE(src_value == absl::nullopt)) return false;
      dest_value = *src_value;
    }
    return true;
  }
}

inline bool ReadLittleEndian64s(Reader& src, absl::Span<uint64_t> dest) {
  if (internal::IsLittleEndian()) {
    return src.Read(dest.size() * sizeof(uint64_t),
                    reinterpret_cast<char*>(dest.data()));
  } else {
    for (uint64_t& dest_value : dest) {
      const absl::optional<uint64_t> src_value = ReadLittleEndian64(src);
      if (ABSL_PREDICT_FALSE(src_value == absl::nullopt)) return false;
      dest_value = *src_value;
    }
    return true;
  }
}

inline bool ReadBigEndian16s(Reader& src, absl::Span<uint16_t> dest) {
  if (internal::IsBigEndian()) {
    return src.Read(dest.size() * sizeof(uint16_t),
                    reinterpret_cast<char*>(dest.data()));
  } else {
    for (uint16_t& dest_value : dest) {
      const absl::optional<uint16_t> src_value = ReadBigEndian16(src);
      if (ABSL_PREDICT_FALSE(src_value == absl::nullopt)) return false;
      dest_value = *src_value;
    }
    return true;
  }
}

inline bool ReadBigEndian32s(Reader& src, absl::Span<uint32_t> dest) {
  if (internal::IsBigEndian()) {
    return src.Read(dest.size() * sizeof(uint32_t),
                    reinterpret_cast<char*>(dest.data()));
  } else {
    for (uint32_t& dest_value : dest) {
      const absl::optional<uint32_t> src_value = ReadBigEndian32(src);
      if (ABSL_PREDICT_FALSE(src_value == absl::nullopt)) return false;
      dest_value = *src_value;
    }
    return true;
  }
}

inline bool ReadBigEndian64s(Reader& src, absl::Span<uint64_t> dest) {
  if (internal::IsBigEndian()) {
    return src.Read(dest.size() * sizeof(uint64_t),
                    reinterpret_cast<char*>(dest.data()));
  } else {
    for (uint64_t& dest_value : dest) {
      const absl::optional<uint64_t> src_value = ReadBigEndian64(src);
      if (ABSL_PREDICT_FALSE(src_value == absl::nullopt)) return false;
      dest_value = *src_value;
    }
    return true;
  }
}

namespace internal {

// If these functions are manually inlined into their callers, clang generates
// poor code (with byte shifting even for native endianness).

inline uint16_t DecodeLittleEndian16(uint16_t encoded) {
  const unsigned char* const ptr =
      reinterpret_cast<const unsigned char*>(&encoded);
  return uint16_t{ptr[0]} | (uint16_t{ptr[1]} << 8);
}

inline uint32_t DecodeLittleEndian32(uint32_t encoded) {
  const unsigned char* const ptr =
      reinterpret_cast<const unsigned char*>(&encoded);
  return uint32_t{ptr[0]} | (uint32_t{ptr[1]} << 8) |
         (uint32_t{ptr[2]} << (2 * 8)) | (uint32_t{ptr[3]} << (3 * 8));
}

inline uint64_t DecodeLittleEndian64(uint64_t encoded) {
  const unsigned char* const ptr =
      reinterpret_cast<const unsigned char*>(&encoded);
  return uint64_t{ptr[0]} | (uint64_t{ptr[1]} << 8) |
         (uint64_t{ptr[2]} << (2 * 8)) | (uint64_t{ptr[3]} << (3 * 8)) |
         (uint64_t{ptr[4]} << (4 * 8)) | (uint64_t{ptr[5]} << (5 * 8)) |
         (uint64_t{ptr[6]} << (6 * 8)) | (uint64_t{ptr[7]} << (7 * 8));
}

inline uint16_t DecodeBigEndian16(uint16_t encoded) {
  const unsigned char* const ptr =
      reinterpret_cast<const unsigned char*>(&encoded);
  return (uint16_t{ptr[0]} << 8) | uint16_t{ptr[1]};
}

inline uint32_t DecodeBigEndian32(uint32_t encoded) {
  const unsigned char* const ptr =
      reinterpret_cast<const unsigned char*>(&encoded);
  return (uint32_t{ptr[0]} << (3 * 8)) | (uint32_t{ptr[1]} << (2 * 8)) |
         (uint32_t{ptr[2]} << 8) | uint32_t{ptr[3]};
}

inline uint64_t DecodeBigEndian64(uint64_t encoded) {
  const unsigned char* const ptr =
      reinterpret_cast<const unsigned char*>(&encoded);
  return (uint64_t{ptr[0]} << (7 * 8)) | (uint64_t{ptr[1]} << (6 * 8)) |
         (uint64_t{ptr[2]} << (5 * 8)) | (uint64_t{ptr[3]} << (4 * 8)) |
         (uint64_t{ptr[4]} << (3 * 8)) | (uint64_t{ptr[5]} << (2 * 8)) |
         (uint64_t{ptr[6]} << 8) | uint64_t{ptr[7]};
}

}  // namespace internal

inline uint16_t ReadLittleEndian16(const char* src) {
  uint16_t encoded;
  std::memcpy(&encoded, src, sizeof(uint16_t));
  return internal::DecodeLittleEndian16(encoded);
}

inline uint32_t ReadLittleEndian32(const char* src) {
  uint32_t encoded;
  std::memcpy(&encoded, src, sizeof(uint32_t));
  return internal::DecodeLittleEndian32(encoded);
}

inline uint64_t ReadLittleEndian64(const char* src) {
  uint64_t encoded;
  std::memcpy(&encoded, src, sizeof(uint64_t));
  return internal::DecodeLittleEndian64(encoded);
}

inline uint16_t ReadBigEndian16(const char* src) {
  uint16_t encoded;
  std::memcpy(&encoded, src, sizeof(uint16_t));
  return internal::DecodeBigEndian16(encoded);
}

inline uint32_t ReadBigEndian32(const char* src) {
  uint32_t encoded;
  std::memcpy(&encoded, src, sizeof(uint32_t));
  return internal::DecodeBigEndian32(encoded);
}

inline uint64_t ReadBigEndian64(const char* src) {
  uint64_t encoded;
  std::memcpy(&encoded, src, sizeof(uint64_t));
  return internal::DecodeBigEndian64(encoded);
}

inline void ReadLittleEndian16s(const char* src, absl::Span<uint16_t> dest) {
  if (internal::IsLittleEndian()) {
    if (ABSL_PREDICT_TRUE(
            // `std::memcpy(nullptr, _, 0)` and `std::memcpy(_, nullptr, 0)` are
            // undefined.
            !dest.empty())) {
      std::memcpy(dest.data(), src, dest.size() * sizeof(uint16_t));
    }
  } else {
    for (uint16_t& value : dest) {
      value = ReadLittleEndian16(src);
      src += sizeof(uint16_t);
    }
  }
}

inline void ReadLittleEndian32s(const char* src, absl::Span<uint32_t> dest) {
  if (internal::IsLittleEndian()) {
    if (ABSL_PREDICT_TRUE(
            // `std::memcpy(nullptr, _, 0)` and `std::memcpy(_, nullptr, 0)` are
            // undefined.
            !dest.empty())) {
      std::memcpy(dest.data(), src, dest.size() * sizeof(uint32_t));
    }
  } else {
    for (uint32_t& value : dest) {
      value = ReadLittleEndian32(src);
      src += sizeof(uint32_t);
    }
  }
}

inline void ReadLittleEndian64s(const char* src, absl::Span<uint64_t> dest) {
  if (internal::IsLittleEndian()) {
    if (ABSL_PREDICT_TRUE(
            // `std::memcpy(nullptr, _, 0)` and `std::memcpy(_, nullptr, 0)` are
            // undefined.
            !dest.empty())) {
      std::memcpy(dest.data(), src, dest.size() * sizeof(uint64_t));
    }
  } else {
    for (uint64_t& value : dest) {
      value = ReadLittleEndian64(src);
      src += sizeof(uint64_t);
    }
  }
}

inline void ReadBigEndian16s(const char* src, absl::Span<uint16_t> dest) {
  if (internal::IsBigEndian()) {
    if (ABSL_PREDICT_TRUE(
            // `std::memcpy(nullptr, _, 0)` and `std::memcpy(_, nullptr, 0)` are
            // undefined.
            !dest.empty())) {
      std::memcpy(dest.data(), src, dest.size() * sizeof(uint16_t));
    }
  } else {
    for (uint16_t& value : dest) {
      value = ReadBigEndian16(src);
      src += sizeof(uint16_t);
    }
  }
}

inline void ReadBigEndian32s(const char* src, absl::Span<uint32_t> dest) {
  if (internal::IsBigEndian()) {
    if (ABSL_PREDICT_TRUE(
            // `std::memcpy(nullptr, _, 0)` and `std::memcpy(_, nullptr, 0)` are
            // undefined.
            !dest.empty())) {
      std::memcpy(dest.data(), src, dest.size() * sizeof(uint32_t));
    }
  } else {
    for (uint32_t& value : dest) {
      value = ReadBigEndian32(src);
      src += sizeof(uint32_t);
    }
  }
}

inline void ReadBigEndian64s(const char* src, absl::Span<uint64_t> dest) {
  if (internal::IsBigEndian()) {
    if (ABSL_PREDICT_TRUE(
            // `std::memcpy(nullptr, _, 0)` and `std::memcpy(_, nullptr, 0)` are
            // undefined.
            !dest.empty())) {
      std::memcpy(dest.data(), src, dest.size() * sizeof(uint64_t));
    }
  } else {
    for (uint64_t& value : dest) {
      value = ReadBigEndian64(src);
      src += sizeof(uint64_t);
    }
  }
}

}  // namespace riegeli

#endif  // RIEGELI_ENDIAN_ENDIAN_READING_H_
