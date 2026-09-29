#ifndef DE_HELPERS_SHA256_HPP_
#define DE_HELPERS_SHA256_HPP_

/**
 * @file sha256.hpp
 * @brief Self-contained SHA-256 (FIPS 180-2), header-only.
 *
 * Deliberately NOT OpenSSL-based: de_rpi_gpio, droneengage_camera and
 * droneengage_precision_landing do not link OpenSSL, and the capability
 * advert hash (Phase 3, "ch" field) must work in every module.
 * Header-only so modules pick it up without CMake/build changes.
 *
 * Verified against the FIPS 180-2 vectors in test/test_cap_schema.cpp
 * ("", "abc", the 448-bit message).
 */

#include <cstdint>
#include <cstddef>
#include <string>

namespace de
{
namespace helpers
{

namespace sha256_detail
{

struct Sha256Ctx
{
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  block[64];
    uint32_t block_len;
};

inline uint32_t rotr(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32 - n));
}

inline void transform(Sha256Ctx& ctx, const uint8_t* block)
{
    static const uint32_t K[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };

    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
    {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24)
             | (static_cast<uint32_t>(block[i * 4 + 1]) << 16)
             | (static_cast<uint32_t>(block[i * 4 + 2]) << 8)
             | (static_cast<uint32_t>(block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i)
    {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ctx.state[0], b = ctx.state[1], c = ctx.state[2], d = ctx.state[3];
    uint32_t e = ctx.state[4], f = ctx.state[5], g = ctx.state[6], h = ctx.state[7];

    for (int i = 0; i < 64; ++i)
    {
        const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + S1 + ch + K[i] + w[i];
        const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx.state[0] += a; ctx.state[1] += b; ctx.state[2] += c; ctx.state[3] += d;
    ctx.state[4] += e; ctx.state[5] += f; ctx.state[6] += g; ctx.state[7] += h;
}

inline void init(Sha256Ctx& ctx)
{
    ctx.state[0] = 0x6a09e667u; ctx.state[1] = 0xbb67ae85u;
    ctx.state[2] = 0x3c6ef372u; ctx.state[3] = 0xa54ff53au;
    ctx.state[4] = 0x510e527fu; ctx.state[5] = 0x9b05688cu;
    ctx.state[6] = 0x1f83d9abu; ctx.state[7] = 0x5be0cd19u;
    ctx.bitlen = 0;
    ctx.block_len = 0;
}

inline void update(Sha256Ctx& ctx, const uint8_t* data, std::size_t len)
{
    for (std::size_t i = 0; i < len; ++i)
    {
        ctx.block[ctx.block_len++] = data[i];
        if (ctx.block_len == 64)
        {
            transform(ctx, ctx.block);
            ctx.bitlen += 512;
            ctx.block_len = 0;
        }
    }
}

inline void final(Sha256Ctx& ctx, uint8_t out[32])
{
    const uint64_t total_bits = ctx.bitlen + ctx.block_len * 8;

    ctx.block[ctx.block_len++] = 0x80;
    if (ctx.block_len > 56)
    {
        while (ctx.block_len < 64) ctx.block[ctx.block_len++] = 0;
        transform(ctx, ctx.block);
        ctx.block_len = 0;
    }
    while (ctx.block_len < 56) ctx.block[ctx.block_len++] = 0;

    for (int i = 7; i >= 0; --i)
        ctx.block[ctx.block_len++] = static_cast<uint8_t>(total_bits >> (i * 8));
    transform(ctx, ctx.block);

    for (int i = 0; i < 8; ++i)
    {
        out[i * 4]     = static_cast<uint8_t>(ctx.state[i] >> 24);
        out[i * 4 + 1] = static_cast<uint8_t>(ctx.state[i] >> 16);
        out[i * 4 + 2] = static_cast<uint8_t>(ctx.state[i] >> 8);
        out[i * 4 + 3] = static_cast<uint8_t>(ctx.state[i]);
    }
}

} // namespace sha256_detail

/**
 * @brief SHA-256 over raw bytes, returned as 64 lowercase hex chars.
 */
inline std::string sha256_hex(const void* data, std::size_t len)
{
    static const char* HEX = "0123456789abcdef";

    sha256_detail::Sha256Ctx ctx;
    sha256_detail::init(ctx);
    sha256_detail::update(ctx, static_cast<const uint8_t*>(data), len);

    uint8_t digest[32];
    sha256_detail::final(ctx, digest);

    std::string out;
    out.reserve(64);
    for (int i = 0; i < 32; ++i)
    {
        out.push_back(HEX[digest[i] >> 4]);
        out.push_back(HEX[digest[i] & 0x0f]);
    }
    return out;
}

inline std::string sha256_hex(const std::string& input)
{
    return sha256_hex(input.data(), input.size());
}

} // namespace helpers
} // namespace de

#endif
