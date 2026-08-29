#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "lib/lookup3.h"
#include "lib/superfox/qpc/nhash2.h"
#include "lib/wsprd/nhash.h"

uint32_t nhash_(const void *key, int *length, uint32_t *initval);

static const uint32_t expected_hashes[] = {
  UINT32_C(0xdeadc12a), UINT32_C(0x1b94435d), UINT32_C(0xd2deb219),
  UINT32_C(0x6155e046), UINT32_C(0xf9db3ff3), UINT32_C(0x999875df),
  UINT32_C(0xcf4a698e), UINT32_C(0x11d73cee), UINT32_C(0x06349a85),
  UINT32_C(0x434bac56), UINT32_C(0x11ead3ab), UINT32_C(0x4c191765),
  UINT32_C(0xf4195417), UINT32_C(0xe8d7137c), UINT32_C(0x2e8d5a88),
  UINT32_C(0xb0414d93), UINT32_C(0x07c55374), UINT32_C(0x110c468c),
  UINT32_C(0x4afa5bbc), UINT32_C(0x4944bccc), UINT32_C(0x4cbb7a1b),
  UINT32_C(0x18516846), UINT32_C(0x3b97ae23), UINT32_C(0x44867164),
  UINT32_C(0x3002d5f9), UINT32_C(0x595cdc5d), UINT32_C(0xec15a036),
  UINT32_C(0xc0d1e3b0), UINT32_C(0x7f471a40), UINT32_C(0x04391f70),
  UINT32_C(0xbceebda2), UINT32_C(0xa5721abb), UINT32_C(0x365f9634),
  UINT32_C(0x086509a4), UINT32_C(0xc87ed5da), UINT32_C(0xd16fd5fd),
  UINT32_C(0xa7ca90d1), UINT32_C(0xfe888aa0), UINT32_C(0xab82ce67),
  UINT32_C(0xbd2683b1), UINT32_C(0x5ce3f5eb), UINT32_C(0xe526881b),
  UINT32_C(0x4f2ad0bc), UINT32_C(0x083f24ee), UINT32_C(0xee9ebcad),
  UINT32_C(0x7b890b41), UINT32_C(0x5f600c7a), UINT32_C(0xf9307a08),
  UINT32_C(0x9025c6d7), UINT32_C(0x8bf9cd5c), UINT32_C(0x5e7fc68f),
  UINT32_C(0x980ec2aa), UINT32_C(0xcfef4469), UINT32_C(0x51df097b),
  UINT32_C(0xf716bb46), UINT32_C(0x5ccbf75a), UINT32_C(0x2b345185),
  UINT32_C(0x6a53fdc1), UINT32_C(0x4b3f9153), UINT32_C(0x0c5cb07a),
  UINT32_C(0xcdad2a44), UINT32_C(0x434b9089), UINT32_C(0x08a090f3),
  UINT32_C(0x504ccef9), UINT32_C(0x8183308b)
};

static int failures;

static void expect_hash(const char *api, size_t offset, size_t length,
                        uint32_t actual, uint32_t expected)
{
  if (actual != expected) {
    fprintf(stderr,
            "%s offset %zu length %zu: expected %08x, got %08x\n",
            api, offset, length, expected, actual);
    ++failures;
  }
}

static unsigned char test_byte(size_t index, size_t length)
{
  return (unsigned char)(index * 37U + length * 11U);
}

int main(void)
{
  const uint32_t seed = UINT32_C(571);

  for (size_t offset = 0; offset < 4; ++offset) {
    for (size_t length = 0; length < sizeof expected_hashes / sizeof *expected_hashes;
         ++length) {
      size_t allocation_size = offset + length;
      if (allocation_size == 0) allocation_size = 1;
      unsigned char *allocation = malloc(allocation_size);
      if (allocation == NULL) {
        fputs("allocation failed\n", stderr);
        return 2;
      }

      unsigned char *key = allocation + offset;
      for (size_t i = 0; i < length; ++i) key[i] = test_byte(i, length);

      uint32_t expected = expected_hashes[length];
      expect_hash("wsjt_hashlittle", offset, length,
                  wsjt_hashlittle(key, length, seed), expected);
      expect_hash("nhash2", offset, length,
                  nhash2(key, length, seed), expected);

      uint32_t wspr_expected = length == 0 ? expected : expected & UINT32_C(0x7fff);
      expect_hash("nhash", offset, length,
                  nhash(key, length, seed), wspr_expected);

      int legacy_length = (int)length;
      uint32_t legacy_seed = seed;
      expect_hash("nhash_", offset, length,
                  nhash_(key, &legacy_length, &legacy_seed), expected);
      free(allocation);
    }
  }

  unsigned char superfox_symbols[47];
  for (size_t i = 0; i < sizeof superfox_symbols; ++i) {
    superfox_symbols[i] = (unsigned char)i;
  }
  uint32_t superfox_crc = nhash2(superfox_symbols, sizeof superfox_symbols,
                                 seed) & UINT32_C(0x1fffff);
  expect_hash("SuperFox CRC21", 0, sizeof superfox_symbols,
              superfox_crc, UINT32_C(0x14d1ab));

  unsigned char abc[] = {'a', 'b', 'c'};
  expect_hash("seed 146 empty", 0, 0,
              wsjt_hashlittle(abc, 0, UINT32_C(146)), UINT32_C(0xdeadbf81));
  expect_hash("seed 146 abc", 0, sizeof abc,
              wsjt_hashlittle(abc, sizeof abc, UINT32_C(146)),
              UINT32_C(0x3f227c14));
  expect_hash("WSPR seed 146 abc", 0, sizeof abc,
              nhash(abc, sizeof abc, UINT32_C(146)), UINT32_C(0x7c14));

  if (failures != 0) return 1;
  puts("lookup3 tests passed: 260 aligned and unaligned vectors");
  return 0;
}
