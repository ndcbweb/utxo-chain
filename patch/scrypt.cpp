// Copyright (c) 2009 Colin Percival, 2011 ArtForz, 2026 Nadcab Labs
// Distributed under the MIT software license.

#include <crypto/scrypt.h>

#include <crypto/common.h>
#include <crypto/hmac_sha256.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

void PBKDF2_SHA256_1(const unsigned char* pass, size_t passlen, const unsigned char* salt, size_t saltlen, unsigned char* out, size_t dklen)
{
    for (size_t block = 0; block * 32 < dklen; ++block) {
        unsigned char index[4];
        WriteBE32(index, uint32_t(block + 1));
        unsigned char u[CHMAC_SHA256::OUTPUT_SIZE];
        CHMAC_SHA256(pass, passlen).Write(salt, saltlen).Write(index, 4).Finalize(u);
        std::memcpy(out + block * 32, u, std::min<size_t>(32, dklen - block * 32));
    }
}

inline uint32_t Rotl(uint32_t a, int b) { return (a << b) | (a >> (32 - b)); }

void XorSalsa8(uint32_t b[16], const uint32_t bx[16])
{
    uint32_t x[16];
    for (int i = 0; i < 16; ++i) x[i] = (b[i] ^= bx[i]);
    for (int i = 0; i < 8; i += 2) {
        x[4] ^= Rotl(x[0] + x[12], 7);   x[8] ^= Rotl(x[4] + x[0], 9);
        x[12] ^= Rotl(x[8] + x[4], 13);  x[0] ^= Rotl(x[12] + x[8], 18);
        x[9] ^= Rotl(x[5] + x[1], 7);    x[13] ^= Rotl(x[9] + x[5], 9);
        x[1] ^= Rotl(x[13] + x[9], 13);  x[5] ^= Rotl(x[1] + x[13], 18);
        x[14] ^= Rotl(x[10] + x[6], 7);  x[2] ^= Rotl(x[14] + x[10], 9);
        x[6] ^= Rotl(x[2] + x[14], 13);  x[10] ^= Rotl(x[6] + x[2], 18);
        x[3] ^= Rotl(x[15] + x[11], 7);  x[7] ^= Rotl(x[3] + x[15], 9);
        x[11] ^= Rotl(x[7] + x[3], 13);  x[15] ^= Rotl(x[11] + x[7], 18);
        x[1] ^= Rotl(x[0] + x[3], 7);    x[2] ^= Rotl(x[1] + x[0], 9);
        x[3] ^= Rotl(x[2] + x[1], 13);   x[0] ^= Rotl(x[3] + x[2], 18);
        x[6] ^= Rotl(x[5] + x[4], 7);    x[7] ^= Rotl(x[6] + x[5], 9);
        x[4] ^= Rotl(x[7] + x[6], 13);   x[5] ^= Rotl(x[4] + x[7], 18);
        x[11] ^= Rotl(x[10] + x[9], 7);  x[8] ^= Rotl(x[11] + x[10], 9);
        x[9] ^= Rotl(x[8] + x[11], 13);  x[10] ^= Rotl(x[9] + x[8], 18);
        x[12] ^= Rotl(x[15] + x[14], 7); x[13] ^= Rotl(x[12] + x[15], 9);
        x[14] ^= Rotl(x[13] + x[12], 13); x[15] ^= Rotl(x[14] + x[13], 18);
    }
    for (int i = 0; i < 16; ++i) b[i] += x[i];
}

} // namespace

void scrypt_1024_1_1_256(const unsigned char* input, unsigned char* output)
{
    thread_local std::vector<uint32_t> v(1024 * 32);
    unsigned char b[128];
    uint32_t x[32];

    PBKDF2_SHA256_1(input, 80, input, 80, b, 128);
    for (int k = 0; k < 32; ++k) x[k] = ReadLE32(&b[4 * k]);
    for (int i = 0; i < 1024; ++i) {
        std::memcpy(&v[i * 32], x, 128);
        XorSalsa8(&x[0], &x[16]);
        XorSalsa8(&x[16], &x[0]);
    }
    for (int i = 0; i < 1024; ++i) {
        const uint32_t j = 32 * (x[16] & 1023);
        for (int k = 0; k < 32; ++k) x[k] ^= v[j + k];
        XorSalsa8(&x[0], &x[16]);
        XorSalsa8(&x[16], &x[0]);
    }
    for (int k = 0; k < 32; ++k) WriteLE32(&b[4 * k], x[k]);
    PBKDF2_SHA256_1(input, 80, b, 128, output, 32);
}
