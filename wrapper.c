#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <emscripten.h>
#include "cubiomes/generator.h"

// 疑似乱数生成
static inline float hash(int n) {
    n = (n << 13) ^ n;
    return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
}

// 3Dパーリンノイズ
static inline float noise3D(float x, float y, float z, int seed) {
    int X = (int)floorf(x);
    int Y = (int)floorf(y);
    int Z = (int)floorf(z);

    float fx = x - (float)X;
    float fy = y - (float)Y;
    float fz = z - (float)Z;

    float u = fx * fx * (3.0f - 2.0f * fx);
    float v = fy * fy * (3.0f - 2.0f * fy);
    float w = fz * fz * (3.0f - 2.0f * fz);

    int n = X + Y * 57 + Z * 113 + seed * 1337;

    float n000 = hash(n);
    float n100 = hash(n + 1);
    float n010 = hash(n + 57);
    float n110 = hash(n + 58);
    float n001 = hash(n + 113);
    float n101 = hash(n + 114);
    float n011 = hash(n + 170);
    float n111 = hash(n + 171);

    float x1 = n000 + u * (n100 - n000);
    float x2 = n010 + u * (n110 - n010);
    float y1 = x1 + v * (x2 - x1);

    float x3 = n001 + u * (n101 - n001);
    float x4 = n011 + u * (n111 - n011);
    float y2 = x3 + v * (x4 - x3);

    return y1 + w * (y2 - y1);
}

// 3Dベースノイズ（振幅 -1.0 〜 +1.0）
static inline float get_base_3d_noise(float x, float y, float z, int seed) {
    float freqX = 1.0f / 64.0f;
    float freqY = 1.0f / 128.0f;
    float freqZ = 1.0f / 64.0f;

    float val = 0.0f;
    float amp = 1.0f;
    for (int i = 0; i < 3; i++) {
        val += noise3D(x * freqX, y * freqY, z * freqZ, seed + i * 101) * amp;
        freqX *= 2.0f;
        freqY *= 2.0f;
        freqZ *= 2.0f;
        amp *= 0.5f;
    }
    return val * 0.55f;
}

// 【重要修正】ネザーの真の物理密度判定
static float calculate_final_density(float x, float y, float z, int seed) {
    // 1. ノイズ値 (-1.0 〜 +1.0)
    float noise = get_base_3d_noise(x, y, z, seed);

    // 2. 中央部（Y=32〜100）はデフォルトでマイナス（広大な大空洞！）
    float bias = -0.32f;

    // 3. 底面と天井の硬化
    if (y < 32.0f) {
        // Y=32から下へ向かって急速に硬くする（床と溶岩底）
        bias += (32.0f - y) * 0.09f;
    } else if (y > 105.0f) {
        // Y=105から上へ向かって急速に硬くする（天井岩盤）
        bias += (y - 105.0f) * 0.09f;
    }

    // ノイズがバイアスを打ち消してプラスになった場所だけが「岩」になる！
    return noise + bias;
}

// バッファサイズ
#define MAX_BLOCKS 10000
static uint8_t g_block_buffer[MAX_BLOCKS * 4];

EMSCRIPTEN_KEEPALIVE
uint8_t* get_block_buffer() {
    return g_block_buffer;
}

// 3Dスキャン関数
EMSCRIPTEN_KEEPALIVE
int scan_nether_3d(int seed, int minX, int maxX, int minZ, int maxZ, int stepH, int stepY) {
    int block_count = 0;

    for (int x = minX; x <= maxX; x += stepH) {
        for (int z = minZ; z <= maxZ; z += stepH) {
            for (int y = 16; y <= 118; y += stepY) {
                float d = calculate_final_density((float)x, (float)y, (float)z, seed);

                // 固体ブロックのみ保存
                if (d > 0.0f) {
                    if (block_count < MAX_BLOCKS) {
                        int idx = block_count * 4;
                        g_block_buffer[idx + 0] = (uint8_t)(x - minX);
                        g_block_buffer[idx + 1] = (uint8_t)y;
                        g_block_buffer[idx + 2] = (uint8_t)(z - minZ);
                        g_block_buffer[idx + 3] = 1;
                        block_count++;
                    }
                }
            }
        }
    }

    return block_count;
}

// バイオーム取得
EMSCRIPTEN_KEEPALIVE
int get_nether_biome(int seed, int x, int y, int z) {
    Generator g;
    setupGenerator(&g, MC_1_20, 0);
    applySeed(&g, DIM_NETHER, (int64_t)seed);
    return getBiomeAt(&g, 4, x, y, z);
}
