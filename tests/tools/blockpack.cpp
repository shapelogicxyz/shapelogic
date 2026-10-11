// SPDX-License-Identifier: GPL-3.0-or-later
// bt_blockpack: pack and unpack files with the new core's blockpack, for
// `oracle.py blockpack-check`. The counterpart of
// tests/oracle/blockpack_old.cpp (the same arguments); keep the two in step.
//
//   bt_blockpack pack   <file> <packed> <keys 0|1> [chunkBytes]
//   bt_blockpack unpack <packed> <file>

#include "bt/io/blockpack.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

int main(int argc, char** argv) {
  if (argc >= 5 && std::strcmp(argv[1], "pack") == 0) {
    std::ofstream out(argv[3], std::ios::binary | std::ios::trunc);
    const auto chunk =
        argc > 5 ? static_cast<std::size_t>(std::strtoull(argv[5], nullptr, 10)) : std::size_t{0};
    const std::uint64_t n = bt::io::blockpack::pack(argv[2], out, std::atoi(argv[4]) != 0, 4, chunk);
    return n == 0 ? 1 : 0;
  }
  if (argc == 4 && std::strcmp(argv[1], "unpack") == 0) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(argv[2], ec);
    std::ifstream in(argv[2], std::ios::binary);
    return !ec && bt::io::blockpack::unpack(in, size, argv[3], 4) ? 0 : 1;
  }
  std::fputs("usage: bt_blockpack pack <file> <packed> <keys> [chunk] | unpack <packed> <file>\n",
             stderr);
  return 2;
}
