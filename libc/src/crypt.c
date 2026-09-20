// This is SHA-256 code ported from my TLS 1.3 project
// see https://github.com/RealFoxerity/tls13/tree/master/crypto/sha2.c

// https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf
#include <endian.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define SHA256_HASH_BYTES  (256/8)
#define SHA256_MESSAGE_SCHED_LEN 64
#define SHA256_WORK_VARS 8 // a - h
#define SHA256_MESSAGE_BLOCK (512)

static const uint32_t sha256_iv[SHA256_WORK_VARS] = {
    0x6a09e667,
    0xbb67ae85,
    0x3c6ef372,
    0xa54ff53a,
    0x510e527f,
    0x9b05688c,
    0x1f83d9ab,
    0x5be0cd19
};

static const uint32_t sha256_consts[] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

struct sha2_ctx {
    int temp_buf_end;
    size_t computed_bytes; // for the pad
    uint8_t temp_buf[SHA256_MESSAGE_BLOCK/8];
    uint32_t work_vars[SHA256_WORK_VARS];
} typedef sha2_ctx_t;

static inline uint64_t sha2_rotl_64(int n, uint64_t x) { // potentially replaceable with __builtin_stdc_rotate_left
    return (x<<n) | (x>>((sizeof(uint64_t)*8)-n));
}

static inline uint64_t sha2_rotr_64(int n, uint64_t x) { // potentially replaceable with __builtin_stdc_rotate_right
    return (x>>n) | (x<<((sizeof(uint64_t)*8)-n));
}

static inline uint32_t sha2_rotl_32(int n, uint32_t x) {
    return (x<<n) | (x>>((sizeof(uint32_t)*8)-n));
}

static inline uint32_t sha2_rotr_32(int n, uint32_t x) {
    return (x>>n) | (x<<((sizeof(uint32_t)*8)-n));
}

#define sha2_shr(n, x) ((x)>>(n)) // to keep naming convention


static inline uint32_t sha256_Ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ ((~x) & z);
}
static inline uint32_t sha256_Maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

static inline uint32_t sha256_sigma0(uint32_t x) { // is it sigma notation or not???????
    return sha2_rotr_32(2, x) ^ sha2_rotr_32(13, x) ^ sha2_rotr_32(22, x);
}
static inline uint32_t sha256_sigma1(uint32_t x) {
    return sha2_rotr_32(6, x) ^ sha2_rotr_32(11, x) ^ sha2_rotr_32(25, x);
}

static inline uint32_t sha256_lsigma0(uint32_t x) {
    return sha2_rotr_32(7, x) ^ sha2_rotr_32(18, x) ^ sha2_shr(3, x);
}
static inline uint32_t sha256_lsigma1(uint32_t x) {
    return sha2_rotr_32(17, x) ^ sha2_rotr_32(19, x) ^ sha2_shr(10, x);
}

static void sha256_update_internal(sha2_ctx_t * ctx, const unsigned char * input, size_t input_len);
static void sha2_pad_block(sha2_ctx_t * ctx, int block_size_bits) {
    assert(ctx);
    assert(block_size_bits == SHA256_MESSAGE_BLOCK);
    assert(ctx->temp_buf_end < block_size_bits / 8); // this is why we need temp_buf_end as a first element

    size_t pad_size = 0;
    uint8_t * pad_data = NULL;

    pad_size = ctx->computed_bytes + 1 + sizeof(uint64_t); // +1 for added bit and since we always operate on bytes, it is safe to assume it will take up a whole byte, see page 13(18)
    pad_size = pad_size + (block_size_bits / 8 - (pad_size%(block_size_bits/8)));
    pad_size -= ctx->computed_bytes;
    assert(pad_size >= 1 + sizeof(uint64_t));

    pad_data = calloc(pad_size, 1);
    pad_data[0] = 0x80;

    *(uint64_t *)&pad_data[pad_size - sizeof(uint64_t)] = htobe64(ctx->computed_bytes*8);

    sha256_update_internal(ctx, pad_data, pad_size);

    free(pad_data);
}

static void sha256_init_internal(sha2_ctx_t * ctx, const uint32_t * iv) {
    memset(ctx, 0, sizeof(sha2_ctx_t));
    memcpy(ctx->work_vars, iv, sizeof(uint32_t)*SHA256_WORK_VARS);
}
static void sha256_update_internal(sha2_ctx_t * ctx, const unsigned char * input, size_t input_len) { // work on a single block at a time so that we don't have to store all of the data at once
    ctx->computed_bytes += input_len;
    if (ctx->temp_buf_end + input_len < SHA256_MESSAGE_BLOCK/8) {
        memcpy(&ctx->temp_buf[ctx->temp_buf_end], input, input_len);
        ctx->temp_buf_end += input_len;
        return;
    }
    uint32_t a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0, h = 0, temp1 = 0, temp2 = 0;
    uint32_t message_schedule[SHA256_MESSAGE_SCHED_LEN] = {0};

    memcpy(&ctx->temp_buf[ctx->temp_buf_end], input, SHA256_MESSAGE_BLOCK/8  - ctx->temp_buf_end);
    input_len -= SHA256_MESSAGE_BLOCK/8  - ctx->temp_buf_end;
    input += SHA256_MESSAGE_BLOCK/8  - ctx->temp_buf_end;
    size_t chunks = 1 + input_len/(SHA256_MESSAGE_BLOCK/8);

    for (size_t i = 0; i < chunks; i++) {
        memset(message_schedule, 0, sizeof(message_schedule));
        for (int t = 0; t < SHA256_MESSAGE_SCHED_LEN; t++) {
            if (t <= 15) {
                message_schedule[t] = be32toh(((uint32_t*)ctx->temp_buf)[t]);
            } else {
                message_schedule[t] = sha256_lsigma1(message_schedule[t-2]) + message_schedule[t-7] + sha256_lsigma0(message_schedule[t-15]) + message_schedule[t-16];
            }
        }
        a = ctx->work_vars[0];
        b = ctx->work_vars[1];
        c = ctx->work_vars[2];
        d = ctx->work_vars[3];
        e = ctx->work_vars[4];
        f = ctx->work_vars[5];
        g = ctx->work_vars[6];
        h = ctx->work_vars[7];

        for (int t = 0; t < SHA256_MESSAGE_SCHED_LEN; t++) {
            temp1 = h + sha256_sigma1(e) + sha256_Ch(e, f, g) + sha256_consts[t] + message_schedule[t];
            temp2 = sha256_sigma0(a) + sha256_Maj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        ctx->work_vars[0] += a;
        ctx->work_vars[1] += b;
        ctx->work_vars[2] += c;
        ctx->work_vars[3] += d;
        ctx->work_vars[4] += e;
        ctx->work_vars[5] += f;
        ctx->work_vars[6] += g;
        ctx->work_vars[7] += h;

        if (i != chunks - 1) {
            memcpy(ctx->temp_buf, &input[i*(SHA256_MESSAGE_BLOCK/8)], SHA256_MESSAGE_BLOCK/8);
        } else {
            memcpy(ctx->temp_buf, &input[i*(SHA256_MESSAGE_BLOCK/8)], input_len % (SHA256_MESSAGE_BLOCK / 8));
            ctx->temp_buf_end = input_len % (SHA256_MESSAGE_BLOCK / 8);
        }
    }
}

static void sha256_finalize_internal(sha2_ctx_t * ctx, unsigned char * hash_out, int hash_vars) {
    sha2_ctx_t temp_ctx = *ctx;

    sha2_pad_block(&temp_ctx, SHA256_MESSAGE_BLOCK);

    for (int t = 0; t < SHA256_WORK_VARS; t++) {
        temp_ctx.work_vars[t] = be32toh(temp_ctx.work_vars[t]);
    }

    memcpy(hash_out, temp_ctx.work_vars, sizeof(uint32_t)*hash_vars);
}

static void sha256_init    (sha2_ctx_t * ctx) {sha256_init_internal(ctx, sha256_iv);}
static void sha256_update  (sha2_ctx_t * ctx, const unsigned char * input, size_t input_len) {sha256_update_internal(ctx, input, input_len);}
static void sha256_finalize(sha2_ctx_t * ctx, unsigned char * hash_out) {sha256_finalize_internal(ctx, hash_out, SHA256_WORK_VARS);}

#define CRYPT_MAX_SALT 32

// hash_bytes*2 because our format
// $salt$hash\0
static char pwhash[1 + CRYPT_MAX_SALT + 1 + SHA256_HASH_BYTES*2 + 1];
char *crypt(const char *key, const char *salt) {
    if (!key) {
        ___set_errno(-EFAULT);
        return NULL;
    }
    size_t salt_len = 0;
    if (salt && (
        (salt_len = strlen(salt)) > CRYPT_MAX_SALT ||
        strchr(salt, '$')
    )) {
        ___set_errno(-EINVAL);
        return NULL;
    }

    pwhash[0] = '$';
    if (salt)
        strcpy(pwhash + 1, salt);
    pwhash[salt_len + 1] = '$';

    unsigned char hash[SHA256_HASH_BYTES];
    sha2_ctx_t context;

    sha256_init(&context);
    if (salt)
        sha256_update(&context, (unsigned char*)salt, salt_len);
    sha256_update(&context, (unsigned char*)key, strlen(key));
    sha256_finalize(&context, hash);

    for (int i = 0; i < SHA256_HASH_BYTES*2; i++) {
        unsigned char nibble = ((hash[i/2]>>(i % 2 ? 0 : 4)) & 0xF);
        pwhash[1 + salt_len + 1 + i] = nibble > 9 ? 'a' + nibble - 10 : nibble + '0';
    }
    pwhash[1 + salt_len + 1 + SHA256_HASH_BYTES*2] = '\0';
    return pwhash;
}