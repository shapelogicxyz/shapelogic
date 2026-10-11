// SPDX-License-Identifier: GPL-3.0-or-later
// blockpack_old: pack and unpack files with the original's blockpack
// (src/lib/blockpack.cpp), for `oracle.py blockpack-check`, which packs
// with one program and unpacks with the other. The counterpart is
// tests/tools/blockpack.cpp (bt_blockpack); keep the two in step.
//
//   blockpack_old pack   <file> <packed> <keys 0|1> [chunkBytes]
//   blockpack_old unpack <packed> <file>

#include "lib/blockpack.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

int main(int argc, char** argv) {
  if (argc >= 5 && std::strcmp(argv[1], "pack") == 0) {
    std::ofstream out(argv[3], std::ios::binary | std::ios::trunc);
    const size_t chunk = argc > 5 ? (size_t)std::strtoull(argv[5], nullptr, 10) : 0;
    const uint64_t n = blockpack::pack(argv[2], out, std::atoi(argv[4]) != 0, 4, chunk);
    return n == 0 ? 1 : 0;
  }
  if (argc == 4 && std::strcmp(argv[1], "unpack") == 0) {
    std::error_code ec;
    const uint64_t size = std::filesystem::file_size(argv[2], ec);
    std::ifstream in(argv[2], std::ios::binary);
    return !ec && blockpack::unpack(in, size, argv[3], 4) ? 0 : 1;
  }
  std::fputs("usage: blockpack_old pack <file> <packed> <keys> [chunk] | unpack <packed> <file>\n", stderr);
  return 2;
}
