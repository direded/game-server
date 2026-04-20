#include "net/framing.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

using game::net::decode;
using game::net::encode;
using game::net::Frame;
using game::net::packet_id;
using game::net::PacketId;

// ─── packet_id (FNV-1a 32-bit) ─────────────────────────────────────────────

TEST(Framing, PacketIdIsConstexpr) {
    // If this compiles the id is usable in switch/case. The exact value is
    // pinned below so an accidental hash-algorithm change breaks here.
    constexpr PacketId id = packet_id("auth.Login");
    static_assert(id != 0u);
}

TEST(Framing, PacketIdFnv1aKnownValues) {
    // Reference values computed by a separate FNV-1a implementation. These
    // pin the algorithm — both client and server must agree on these hashes
    // or packets will not dispatch.
    EXPECT_EQ(packet_id(""),             0x811C9DC5u);  // FNV-1a offset basis
    EXPECT_EQ(packet_id("a"),            0xE40C292Cu);
    EXPECT_EQ(packet_id("foobar"),       0xBF9CF968u);
}

TEST(Framing, PacketIdSameInputSameHash) {
    EXPECT_EQ(packet_id("auth.Login"), packet_id("auth.Login"));
}

TEST(Framing, PacketIdDifferentInputsDifferentHashes) {
    EXPECT_NE(packet_id("auth.Login"), packet_id("auth.Register"));
    EXPECT_NE(packet_id("auth.Login"), packet_id("auth.login"));  // case-sensitive
}

// ─── encode / decode roundtrip ─────────────────────────────────────────────

TEST(Framing, EncodeWritesLittleEndianHeader) {
    const PacketId id = 0x01020304u;
    auto buf = encode(id, nullptr, 0);
    ASSERT_EQ(buf.size(), 4u);
    EXPECT_EQ(buf[0], 0x04u);
    EXPECT_EQ(buf[1], 0x03u);
    EXPECT_EQ(buf[2], 0x02u);
    EXPECT_EQ(buf[3], 0x01u);
}

TEST(Framing, EncodeAppendsPayloadAfterHeader) {
    const PacketId id = packet_id("x");
    const std::vector<uint8_t> payload{0xDE, 0xAD, 0xBE, 0xEF};
    auto buf = encode(id, payload.data(), payload.size());
    ASSERT_EQ(buf.size(), 4u + payload.size());
    EXPECT_EQ(0, std::memcmp(buf.data() + 4, payload.data(), payload.size()));
}

TEST(Framing, RoundtripPreservesIdAndPayload) {
    const PacketId id = packet_id("auth.AuthOk");
    const std::vector<uint8_t> payload{1, 2, 3, 4, 5, 6, 7, 8};

    auto buf = encode(id, payload.data(), payload.size());
    auto frame_opt = decode(buf.data(), buf.size());
    ASSERT_TRUE(frame_opt.has_value());

    const Frame& f = *frame_opt;
    EXPECT_EQ(f.packet_id, id);
    EXPECT_EQ(f.payload_len, payload.size());
    EXPECT_EQ(0, std::memcmp(f.payload, payload.data(), payload.size()));
}

TEST(Framing, RoundtripEmptyPayload) {
    const PacketId id = packet_id("ping.Ping");
    auto buf = encode(id, nullptr, 0);
    auto frame_opt = decode(buf.data(), buf.size());
    ASSERT_TRUE(frame_opt.has_value());
    EXPECT_EQ(frame_opt->packet_id, id);
    EXPECT_EQ(frame_opt->payload_len, 0u);
}

// ─── decode() edge cases ───────────────────────────────────────────────────

TEST(Framing, DecodeNullReturnsNullopt) {
    auto frame_opt = decode(nullptr, 0);
    EXPECT_FALSE(frame_opt.has_value());
}

TEST(Framing, DecodeTooShortReturnsNullopt) {
    const uint8_t buf[] = {0x01, 0x02, 0x03};  // 3 bytes — needs at least 4
    auto frame_opt = decode(buf, sizeof(buf));
    EXPECT_FALSE(frame_opt.has_value());
}

TEST(Framing, DecodeExactHeaderLengthIsValid) {
    const uint8_t buf[] = {0x0A, 0x0B, 0x0C, 0x0D};
    auto frame_opt = decode(buf, sizeof(buf));
    ASSERT_TRUE(frame_opt.has_value());
    EXPECT_EQ(frame_opt->packet_id, 0x0D0C0B0Au);
    EXPECT_EQ(frame_opt->payload_len, 0u);
}

TEST(Framing, DecodeLargePayloadHandledCorrectly) {
    const PacketId id = packet_id("large");
    std::vector<uint8_t> payload(4096);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(i & 0xFFu);
    }
    auto buf = encode(id, payload.data(), payload.size());
    auto frame_opt = decode(buf.data(), buf.size());
    ASSERT_TRUE(frame_opt.has_value());
    EXPECT_EQ(frame_opt->payload_len, payload.size());
    EXPECT_EQ(0, std::memcmp(frame_opt->payload, payload.data(), payload.size()));
}
