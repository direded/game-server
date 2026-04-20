#include "protocol/generated/ping_generated.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace {

// Helper: build a Ping and return the finished buffer as a pair (ptr, size).
// The FlatBufferBuilder must outlive the returned pointer — callers pass one in.
const uint8_t* build_ping(flatbuffers::FlatBufferBuilder& builder, uint32_t nonce) {
    auto offset = ping::CreatePing(builder, nonce);
    builder.Finish(offset);
    return builder.GetBufferPointer();
}

const uint8_t* build_pong(flatbuffers::FlatBufferBuilder& builder,
                          uint32_t nonce,
                          uint64_t server_time_ms) {
    auto offset = ping::CreatePong(builder, nonce, server_time_ms);
    builder.Finish(offset);
    return builder.GetBufferPointer();
}

} // namespace

// ─── Ping ────────────────────────────────────────────────────────────────────

TEST(PingProtocol, RoundtripPreservesNonce) {
    flatbuffers::FlatBufferBuilder builder;
    auto* decoded = ping::GetPing(build_ping(builder, 42u));
    EXPECT_EQ(decoded->nonce(), 42u);
}

TEST(PingProtocol, RoundtripZeroNonce) {
    flatbuffers::FlatBufferBuilder builder;
    auto* decoded = ping::GetPing(build_ping(builder, 0u));
    EXPECT_EQ(decoded->nonce(), 0u);
}

TEST(PingProtocol, RoundtripMaxNonce) {
    flatbuffers::FlatBufferBuilder builder;
    const uint32_t max_nonce = std::numeric_limits<uint32_t>::max();
    auto* decoded = ping::GetPing(build_ping(builder, max_nonce));
    EXPECT_EQ(decoded->nonce(), max_nonce);
}

TEST(PingProtocol, SerializedBufferIsNonEmpty) {
    flatbuffers::FlatBufferBuilder builder;
    (void)build_ping(builder, 7u);
    EXPECT_GT(builder.GetSize(), 0u);
}

TEST(PingProtocol, VerifierAcceptsValidBuffer) {
    flatbuffers::FlatBufferBuilder builder;
    (void)build_ping(builder, 123u);

    flatbuffers::Verifier verifier(builder.GetBufferPointer(), builder.GetSize());
    EXPECT_TRUE(verifier.VerifyBuffer<ping::Ping>(/*identifier=*/nullptr));
}

TEST(PingProtocol, DistinctNoncesProduceDistinctFields) {
    flatbuffers::FlatBufferBuilder b1, b2;
    auto* p1 = ping::GetPing(build_ping(b1, 100u));
    auto* p2 = ping::GetPing(build_ping(b2, 200u));
    EXPECT_NE(p1->nonce(), p2->nonce());
}

// ─── Pong ────────────────────────────────────────────────────────────────────

TEST(PongProtocol, RoundtripPreservesAllFields) {
    flatbuffers::FlatBufferBuilder builder;
    auto* decoded = ping::GetPong(build_pong(builder, 42u, 1'700'000'000'000ull));
    EXPECT_EQ(decoded->nonce(), 42u);
    EXPECT_EQ(decoded->server_time_ms(), 1'700'000'000'000ull);
}

TEST(PongProtocol, RoundtripZeroValues) {
    flatbuffers::FlatBufferBuilder builder;
    auto* decoded = ping::GetPong(build_pong(builder, 0u, 0ull));
    EXPECT_EQ(decoded->nonce(), 0u);
    EXPECT_EQ(decoded->server_time_ms(), 0ull);
}

TEST(PongProtocol, RoundtripMaxValues) {
    flatbuffers::FlatBufferBuilder builder;
    const uint32_t max_nonce = std::numeric_limits<uint32_t>::max();
    const uint64_t max_time  = std::numeric_limits<uint64_t>::max();
    auto* decoded = ping::GetPong(build_pong(builder, max_nonce, max_time));
    EXPECT_EQ(decoded->nonce(), max_nonce);
    EXPECT_EQ(decoded->server_time_ms(), max_time);
}

TEST(PongProtocol, VerifierAcceptsValidBuffer) {
    flatbuffers::FlatBufferBuilder builder;
    (void)build_pong(builder, 9u, 12345ull);

    flatbuffers::Verifier verifier(builder.GetBufferPointer(), builder.GetSize());
    EXPECT_TRUE(verifier.VerifyBuffer<ping::Pong>(/*identifier=*/nullptr));
}

TEST(PongProtocol, CorrelatesWithPingNonce) {
    // In the wire protocol a Pong echoes the Ping's nonce so the client can
    // match the reply to its outstanding request. This test asserts nothing
    // about network behavior — it just confirms the field plumbing preserves
    // the correlation value through encode/decode on both sides.
    flatbuffers::FlatBufferBuilder ping_buf, pong_buf;
    auto* sent = ping::GetPing(build_ping(ping_buf, 0xDEADBEEFu));
    auto* recv = ping::GetPong(build_pong(pong_buf, sent->nonce(), 999ull));
    EXPECT_EQ(recv->nonce(), sent->nonce());
}
