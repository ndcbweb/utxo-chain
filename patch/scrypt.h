// Copyright (c) 2026 Nadcab Labs
// Distributed under the MIT software license.

#ifndef BITCOIN_CRYPTO_SCRYPT_H
#define BITCOIN_CRYPTO_SCRYPT_H

/** scrypt(N=1024, r=1, p=1) of an 80-byte block header, as used by Litecoin-style chains. */
void scrypt_1024_1_1_256(const unsigned char* input, unsigned char* output);

#endif // BITCOIN_CRYPTO_SCRYPT_H
