// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/hpack/hpack_huffman_table.h"

#include <stdint.h>

#include <bitset>
#include <limits>
#include <string>

#include "base/logging.h"
#include "net/spdy/hpack/hpack_constants.h"
#include "net/spdy/hpack/hpack_input_stream.h"
#include "net/spdy/hpack/hpack_output_stream.h"
#include "net/spdy/spdy_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::StringPiece;
using std::string;
using testing::ElementsAreArray;
using testing::Pointwise;

namespace net {

namespace test {

typedef HpackHuffmanTable::DecodeEntry DecodeEntry;
typedef HpackHuffmanTable::DecodeTable DecodeTable;

class HpackHuffmanTablePeer {
 public:
  explicit HpackHuffmanTablePeer(const HpackHuffmanTable& table)
      : table_(table) {}

  const std::vector<uint32_t>& code_by_id() const { return table_.code_by_id_; }
  const std::vector<uint8_t>& length_by_id() const {
    return table_.length_by_id_;
  }
  const std::vector<DecodeTable>& decode_tables() const {
    return table_.decode_tables_;
  }
  char pad_bits() const {
    // Cast to match signed-ness of bits8().
    return static_cast<char>(table_.pad_bits_);
  }
  uint16_t failed_symbol_id() const { return table_.failed_symbol_id_; }
  std::vector<DecodeEntry> decode_entries(const DecodeTable& decode_table) {
    std::vector<DecodeEntry>::const_iterator begin =
        table_.decode_entries_.begin() + decode_table.entries_offset;
    return std::vector<DecodeEntry>(begin, begin + decode_table.size());
  }

 private:
  const HpackHuffmanTable& table_;
};

namespace {

class HpackHuffmanTableTest : public ::testing::Test {
 protected:
  HpackHuffmanTableTest() : table_(), peer_(table_) {}

  string EncodeString(StringPiece input) {
    string result;
    HpackOutputStream output_stream;
    table_.EncodeString(input, &output_stream);

    output_stream.TakeString(&result);
    // Verify EncodedSize() agrees with EncodeString().
    EXPECT_EQ(result.size(), table_.EncodedSize(input));
    return result;
  }

  HpackHuffmanTable table_;
  HpackHuffmanTablePeer peer_;
};

MATCHER(DecodeEntryEq, "") {
  const DecodeEntry& lhs = std::tr1::get<0>(arg);
  const DecodeEntry& rhs = std::tr1::get<1>(arg);
  return lhs.next_table_index == rhs.next_table_index &&
         lhs.length == rhs.length && lhs.symbol_id == rhs.symbol_id;
}

uint32_t bits32(const string& bitstring) {
  return std::bitset<32>(bitstring).to_ulong();
}
char bits8(const string& bitstring) {
  return static_cast<char>(std::bitset<8>(bitstring).to_ulong());
}

TEST_F(HpackHuffmanTableTest, InitializeHpackCode) {
  std::vector<HpackHuffmanSymbol> code = HpackHuffmanCode();
  EXPECT_TRUE(table_.Initialize(&code[0], code.size()));
  EXPECT_TRUE(table_.IsInitialized());
  EXPECT_EQ(peer_.pad_bits(), bits8("11111111"));  // First 8 bits of EOS.
}

TEST_F(HpackHuffmanTableTest, InitializeEdgeCases) {
  {
    // Verify eight symbols can be encoded with 3 bits per symbol.
    HpackHuffmanSymbol code[] = {
        {bits32("00000000000000000000000000000000"), 3, 0},
        {bits32("00100000000000000000000000000000"), 3, 1},
        {bits32("01000000000000000000000000000000"), 3, 2},
        {bits32("01100000000000000000000000000000"), 3, 3},
        {bits32("10000000000000000000000000000000"), 3, 4},
        {bits32("10100000000000000000000000000000"), 3, 5},
        {bits32("11000000000000000000000000000000"), 3, 6},
        {bits32("11100000000000000000000000000000"), 8, 7}};
    HpackHuffmanTable table;
    EXPECT_TRUE(table.Initialize(code, arraysize(code)));
  }
  {
    // But using 2 bits with one symbol overflows the code.
    HpackHuffmanSymbol code[] = {
        {bits32("01000000000000000000000000000000"), 3, 0},
        {bits32("01100000000000000000000000000000"), 3, 1},
        {bits32("00000000000000000000000000000000"), 2, 2},
        {bits32("10000000000000000000000000000000"), 3, 3},
        {bits32("10100000000000000000000000000000"), 3, 4},
        {bits32("11000000000000000000000000000000"), 3, 5},
        {bits32("11100000000000000000000000000000"), 3, 6},
        {bits32("00000000000000000000000000000000"), 8, 7}};  // Overflow.
    HpackHuffmanTable table;
    EXPECT_FALSE(table.Initialize(code, arraysize(code)));
    EXPECT_EQ(7, HpackHuffmanTablePeer(table).failed_symbol_id());
  }
  {
    // Verify four symbols can be encoded with incremental bits per symbol.
    HpackHuffmanSymbol code[] = {
        {bits32("00000000000000000000000000000000"), 1, 0},
        {bits32("10000000000000000000000000000000"), 2, 1},
        {bits32("11000000000000000000000000000000"), 3, 2},
        {bits32("11100000000000000000000000000000"), 8, 3}};
    HpackHuffmanTable table;
    EXPECT_TRUE(table.Initialize(code, arraysize(code)));
  }
  {
    // But repeating a length overflows the code.
    HpackHuffmanSymbol code[] = {
        {bits32("00000000000000000000000000000000"), 1, 0},
        {bits32("10000000000000000000000000000000"), 2, 1},
        {bits32("11000000000000000000000000000000"), 2, 2},
        {bits32("00000000000000000000000000000000"), 8, 3}};  // Overflow.
    HpackHuffmanTable table;
    EXPECT_FALSE(table.Initialize(code, arraysize(code)));
    EXPECT_EQ(3, HpackHuffmanTablePeer(table).failed_symbol_id());
  }
  {
    // Symbol IDs must be assigned sequentially with no gaps.
    HpackHuffmanSymbol code[] = {
        {bits32("00000000000000000000000000000000"), 1, 0},
        {bits32("10000000000000000000000000000000"), 2, 1},
        {bits32("11000000000000000000000000000000"), 3, 1},  // Repeat.
        {bits32("11100000000000000000000000000000"), 8, 3}};
    HpackHuffmanTable table;
    EXPECT_FALSE(table.Initialize(code, arraysize(code)));
    EXPECT_EQ(2, HpackHuffmanTablePeer(table).failed_symbol_id());
  }
  {
    // Canonical codes must begin with zero.
    HpackHuffmanSymbol code[] = {
        {bits32("10000000000000000000000000000000"), 4, 0},
        {bits32("10010000000000000000000000000000"), 4, 1},
        {bits32("10100000000000000000000000000000"), 4, 2},
        {bits32("10110000000000000000000000000000"), 8, 3}};
    HpackHuffmanTable table;
    EXPECT_FALSE(table.Initialize(code, arraysize(code)));
    EXPECT_EQ(0, HpackHuffmanTablePeer(table).failed_symbol_id());
  }
  {
    // Codes must match the expected canonical sequence.
    HpackHuffmanSymbol code[] = {
        {bits32("00000000000000000000000000000000"), 2, 0},
        {bits32("01000000000000000000000000000000"), 2, 1},
        {bits32("11000000000000000000000000000000"), 2, 2},  // Not canonical.
        {bits32("10000000000000000000000000000000"), 8, 3}};
    HpackHuffmanTable table;
    EXPECT_FALSE(table.Initialize(code, arraysize(code)));
    EXPECT_EQ(2, HpackHuffmanTablePeer(table).failed_symbol_id());
  }
  {
    // At least one code must have a length of 8 bits (to ensure pad-ability).
    HpackHuffmanSymbol code[] = {
        {bits32("00000000000000000000000000000000"), 1, 0},
        {bits32("10000000000000000000000000000000"), 2, 1},
        {bits32("11000000000000000000000000000000"), 3, 2},
        {bits32("11100000000000000000000000000000"), 7, 3}};
    HpackHuffmanTable table;
    EXPECT_FALSE(table.Initialize(code, arraysize(code)));
  }
}

TEST_F(HpackHuffmanTableTest, ValidateInternalsWithSmallCode) {
  HpackHuffmanSymbol code[] = {
      {bits32("01100000000000000000000000000000"), 4, 0},  // 3rd.
      {bits32("01110000000000000000000000000000"), 4, 1},  // 4th.
      {bits32("00000000000000000000000000000000"), 2, 2},  // 1st assigned code.
      {bits32("01000000000000000000000000000000"), 3, 3},  // 2nd.
      {bits32("10000000000000000000000000000000"), 5, 4},  // 5th.
      {bits32("10001000000000000000000000000000"), 5, 5},  // 6th.
      {bits32("10011000000000000000000000000000"), 8, 6},  // 8th.
      {bits32("10010000000000000000000000000000"), 5, 7}};  // 7th.
  EXPECT_TRUE(table_.Initialize(code, arraysize(code)));
  ASSERT_EQ(arraysize(code), peer_.code_by_id().size());
  ASSERT_EQ(arraysize(code), peer_.length_by_id().size());
  for (size_t i = 0; i < arraysize(code); ++i) {
    EXPECT_EQ(code[i].code, peer_.code_by_id()[i]);
    EXPECT_EQ(code[i].length, peer_.length_by_id()[i]);
  }

  EXPECT_EQ(1u, peer_.decode_tables().size());
  {
    std::vector<DecodeEntry> expected;
    expected.resize(128, DecodeEntry(0, 2, 2));  // Fills 128.
    expected.resize(192, DecodeEntry(0, 3, 3));  // Fills 64.
    expected.resize(224, DecodeEntry(0, 4, 0));  // Fills 32.
    expected.resize(256, DecodeEntry(0, 4, 1));  // Fills 32.
    expected.resize(272, DecodeEntry(0, 5, 4));  // Fills 16.
    expected.resize(288, DecodeEntry(0, 5, 5));  // Fills 16.
    expected.resize(304, DecodeEntry(0, 5, 7));  // Fills 16.
    expected.resize(306, DecodeEntry(0, 8, 6));  // Fills 2.
    expected.resize(512, DecodeEntry());         // Remainder is empty.

    EXPECT_THAT(peer_.decode_entries(peer_.decode_tables()[0]),
                Pointwise(DecodeEntryEq(), expected));
  }
  EXPECT_EQ(bits8("10011000"), peer_.pad_bits());

  char input_storage[] = {2, 3, 2, 7, 4};
  StringPiece input(input_storage, arraysize(input_storage));
  // By symbol: (2) 00 (3) 010 (2) 00 (7) 10010 (4) 10000 (6 as pad) 1001100.
  char expect_storage[] = {bits8("00010001"), bits8("00101000"),
                           bits8("01001100")};
  StringPiece expect(expect_storage, arraysize(expect_storage));

  string buffer_in = EncodeString(input);
  EXPECT_EQ(expect, buffer_in);

  string buffer_out;
  HpackInputStream input_stream(std::numeric_limits<uint32_t>::max(),
                                buffer_in);
  EXPECT_TRUE(table_.DecodeString(&input_stream, input.size(), &buffer_out));
  EXPECT_EQ(buffer_out, input);
}

TEST_F(HpackHuffmanTableTest, ValidateMultiLevelDecodeTables) {
  HpackHuffmanSymbol code[] = {
      {bits32("00000000000000000000000000000000"), 6, 0},
      {bits32("00000100000000000000000000000000"), 6, 1},
      {bits32("00001000000000000000000000000000"), 11, 2},
      {bits32("00001000001000000000000000000000"), 11, 3},
      {bits32("00001000010000000000000000000000"), 12, 4},
  };
  EXPECT_TRUE(table_.Initialize(code, arraysize(code)));

  EXPECT_EQ(2u, peer_.decode_tables().size());
  {
    std::vector<DecodeEntry> expected;
    expected.resize(8, DecodeEntry(0, 6, 0));    // Fills 8.
    expected.resize(16, DecodeEntry(0, 6, 1));   // Fills 8.
    expected.resize(17, DecodeEntry(1, 12, 0));  // Pointer. Fills 1.
    expected.resize(512, DecodeEntry());         // Remainder is empty.

    const DecodeTable& decode_table = peer_.decode_tables()[0];
    EXPECT_EQ(decode_table.prefix_length, 0);
    EXPECT_EQ(decode_table.indexed_length, 9);
    EXPECT_THAT(peer_.decode_entries(decode_table),
                Pointwise(DecodeEntryEq(), expected));
  }
  {
    std::vector<DecodeEntry> expected;
    expected.resize(2, DecodeEntry(1, 11, 2));  // Fills 2.
    expected.resize(4, DecodeEntry(1, 11, 3));  // Fills 2.
    expected.resize(5, DecodeEntry(1, 12, 4));  // Fills 1.
    expected.resize(8, DecodeEntry());          // Remainder is empty.

    const DecodeTable& decode_table = peer_.decode_tables()[1];
    EXPECT_EQ(decode_table.prefix_length, 9);
    EXPECT_EQ(decode_table.indexed_length, 3);
    EXPECT_THAT(peer_.decode_entries(decode_table),
                Pointwise(DecodeEntryEq(), expected));
  }
  EXPECT_EQ(bits8("00001000"), peer_.pad_bits());
}

TEST_F(HpackHuffmanTableTest, DecodeWithBadInput) {
  HpackHuffmanSymbol code[] = {
      {bits32("01100000000000000000000000000000"), 4, 0},
      {bits32("01110000000000000000000000000000"), 4, 1},
      {bits32("00000000000000000000000000000000"), 2, 2},
      {bits32("01000000000000000000000000000000"), 3, 3},
      {bits32("10000000000000000000000000000000"), 5, 4},
      {bits32("10001000000000000000000000000000"), 5, 5},
      {bits32("10011000000000000000000000000000"), 6, 6},
      {bits32("10010000000000000000000000000000"), 5, 7},
      {bits32("10011100000000000000000000000000"), 16, 8}};
  EXPECT_TRUE(table_.Initialize(code, arraysize(code)));

  string buffer;
  const size_t capacity = 4;
  {
    // This example works: (2) 00 (3) 010 (2) 00 (6) 100110 (pad) 100.
    char input_storage[] = {bits8("00010001"), bits8("00110100")};
    StringPiece input(input_storage, arraysize(input_storage));

    HpackInputStream input_stream(std::numeric_limits<uint32_t>::max(), input);
    EXPECT_TRUE(table_.DecodeString(&input_stream, capacity, &buffer));
    EXPECT_EQ(buffer, "\x02\x03\x02\x06");
  }
  {
    // Expect to fail on an invalid code prefix.
    // (2) 00 (3) 010 (2) 00 (too-large) 101000 (pad) 100.
    char input_storage[] = {bits8("00010001"), bits8("01000111")};
    StringPiece input(input_storage, arraysize(input_storage));

    HpackInputStream input_stream(std::numeric_limits<uint32_t>::max(), input);
    EXPECT_FALSE(table_.DecodeString(&input_stream, capacity, &buffer));
    EXPECT_EQ(buffer, "\x02\x03\x02");
  }
  {
    // Repeat the shortest 0b00 code to overflow |buffer|. Expect to fail.
    std::vector<char> input_storage(1 + capacity / 4, '\0');
    StringPiece input(&input_storage[0], input_storage.size());

    HpackInputStream input_stream(std::numeric_limits<uint32_t>::max(), input);
    EXPECT_FALSE(table_.DecodeString(&input_stream, capacity, &buffer));

    std::vector<char> expected(capacity, '\x02');
    EXPECT_THAT(buffer, ElementsAreArray(expected));
    EXPECT_EQ(capacity, buffer.size());
  }
  {
    // Expect to fail if more than a byte of unconsumed input remains.
    // (6) 100110 (8 truncated) 1001110000
    char input_storage[] = {bits8("10011010"), bits8("01110000")};
    StringPiece input(input_storage, arraysize(input_storage));

    HpackInputStream input_stream(std::numeric_limits<uint32_t>::max(), input);
    EXPECT_FALSE(table_.DecodeString(&input_stream, capacity, &buffer));
    EXPECT_EQ(buffer, "\x06");
  }
}

TEST_F(HpackHuffmanTableTest, SpecRequestExamples) {
  std::vector<HpackHuffmanSymbol> code = HpackHuffmanCode();
  EXPECT_TRUE(table_.Initialize(&code[0], code.size()));

  string buffer;
  string test_table[] = {
      a2b_hex("f1e3c2e5f23a6ba0ab90f4ff"),
      "www.example.com",
      a2b_hex("a8eb10649cbf"),
      "no-cache",
      a2b_hex("25a849e95ba97d7f"),
      "custom-key",
      a2b_hex("25a849e95bb8e8b4bf"),
      "custom-value",
  };
  // Round-trip each test example.
  for (size_t i = 0; i != arraysize(test_table); i += 2) {
    const string& encodedFixture(test_table[i]);
    const string& decodedFixture(test_table[i + 1]);
    HpackInputStream input_stream(std::numeric_limits<uint32_t>::max(),
                                  encodedFixture);

    EXPECT_TRUE(
        table_.DecodeString(&input_stream, decodedFixture.size(), &buffer));
    EXPECT_EQ(decodedFixture, buffer);
    buffer = EncodeString(decodedFixture);
    EXPECT_EQ(encodedFixture, buffer);
  }
}

TEST_F(HpackHuffmanTableTest, SpecResponseExamples) {
  std::vector<HpackHuffmanSymbol> code = HpackHuffmanCode();
  EXPECT_TRUE(table_.Initialize(&code[0], code.size()));

  string buffer;
  string test_table[] = {
      a2b_hex("6402"), "302", a2b_hex("aec3771a4b"), "private",
      a2b_hex("d07abe941054d444a8200595040b8166"
              "e082a62d1bff"),
      "Mon, 21 Oct 2013 20:13:21 GMT",
      a2b_hex("9d29ad171863c78f0b97c8e9ae82ae43"
              "d3"),
      "https://www.example.com", a2b_hex("94e7821dd7f2e6c7b335dfdfcd5b3960"
                                         "d5af27087f3672c1ab270fb5291f9587"
                                         "316065c003ed4ee5b1063d5007"),
      "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1",
  };
  // Round-trip each test example.
  for (size_t i = 0; i != arraysize(test_table); i += 2) {
    const string& encodedFixture(test_table[i]);
    const string& decodedFixture(test_table[i + 1]);
    HpackInputStream input_stream(std::numeric_limits<uint32_t>::max(),
                                  encodedFixture);

    EXPECT_TRUE(
        table_.DecodeString(&input_stream, decodedFixture.size(), &buffer));
    EXPECT_EQ(decodedFixture, buffer);
    buffer = EncodeString(decodedFixture);
    EXPECT_EQ(encodedFixture, buffer);
  }
}

TEST_F(HpackHuffmanTableTest, RoundTripIndvidualSymbols) {
  std::vector<HpackHuffmanSymbol> code = HpackHuffmanCode();
  EXPECT_TRUE(table_.Initialize(&code[0], code.size()));

  for (size_t i = 0; i != 256; i++) {
    char c = static_cast<char>(i);
    char storage[3] = {c, c, c};
    StringPiece input(storage, arraysize(storage));

    string buffer_in = EncodeString(input);
    string buffer_out;

    HpackInputStream input_stream(std::numeric_limits<uint32_t>::max(),
                                  buffer_in);
    EXPECT_TRUE(table_.DecodeString(&input_stream, input.size(), &buffer_out));
    EXPECT_EQ(input, buffer_out);
  }
}

TEST_F(HpackHuffmanTableTest, RoundTripSymbolSequence) {
  std::vector<HpackHuffmanSymbol> code = HpackHuffmanCode();
  EXPECT_TRUE(table_.Initialize(&code[0], code.size()));

  char storage[512];
  for (size_t i = 0; i != 256; i++) {
    storage[i] = static_cast<char>(i);
    storage[511 - i] = static_cast<char>(i);
  }
  StringPiece input(storage, arraysize(storage));

  string buffer_in = EncodeString(input);
  string buffer_out;

  HpackInputStream input_stream(std::numeric_limits<uint32_t>::max(),
                                buffer_in);
  EXPECT_TRUE(table_.DecodeString(&input_stream, input.size(), &buffer_out));
  EXPECT_EQ(input, buffer_out);
}

TEST_F(HpackHuffmanTableTest, EncodedSizeAgreesWithEncodeString) {
  std::vector<HpackHuffmanSymbol> code = HpackHuffmanCode();
  EXPECT_TRUE(table_.Initialize(&code[0], code.size()));

  string test_table[] = {
      "",
      "Mon, 21 Oct 2013 20:13:21 GMT",
      "https://www.example.com",
      "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1",
      string(1, '\0'),
      string("foo\0bar", 7),
      string(256, '\0'),
  };
  for (size_t i = 0; i != 256; ++i) {
    // Expand last |test_table| entry to cover all codes.
    test_table[arraysize(test_table) - 1][i] = static_cast<char>(i);
  }

  HpackOutputStream output_stream;
  string encoding;
  for (size_t i = 0; i != arraysize(test_table); ++i) {
    table_.EncodeString(test_table[i], &output_stream);
    output_stream.TakeString(&encoding);
    EXPECT_EQ(encoding.size(), table_.EncodedSize(test_table[i]));
  }
}

}  // namespace

}  // namespace test

}  // namespace net