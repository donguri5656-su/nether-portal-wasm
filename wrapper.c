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

// --- 【公式 nether.json の完全再現】 ---

// 1. y_clamped_gradient の計算
static inline float clamped_gradient(float y, float from_y, float to_y, float from_val, float to_val) {
    if (from_y < to_y) {
        if (y <= from_y) return from_val;
        if (y >= to_y) return to_val;
        return from_val + (to_val - from_val) * ((y - from_y) / (to_y - from_y));
    } else {
        if (y >= from_y) return from_val;
        if (y <= to_y) return to_val;
        return from_val + (to_val - from_val) * ((from_y - y) / (from_y - to_y));
    }
}

// 2. base_3d_noise の計算（公式スケール: XZ=80, Y=160）
static inline float get_base_3d_noise(float x, float y, float z, int seed) {
    float freqX = 1.0f / 80.0f;  // 公式 xz_factor = 80
    float freqY = 1.0f / 160.0f; // 公式 y_factor = 160
    float freqZ = 1.0f / 80.0f;

    float val = 0.0f;
    float amp = 1.0f;
    for (int i = 0; i < 4; i++) {
        val += noise3D(x * freqX, y * freqY, z * freqZ, seed + i * 101) * amp;
        freqX *= 2.0f;
        freqY *= 2.0f;
        freqZ *= 2.0f;
        amp *= 0.5f;
    }
    return val;
}

// 3. final_density（公式数式ツリーの完全評価）
static float calculate_final_density(float x, float y, float z, int seed) {
    // floor_gradient: from_y: -8, to_y: 24, from_val: 0, to_val: 1
    float floor_grad = clamped_gradient(y, -8.0f, 24.0f, 0.0f, 1.0f);

    // roof_gradient: from_y: 128, to_y: 112, from_val: 0, to_val: 1
    float roof_grad = clamped_gradient(y, 128.0f, 112.0f, 0.0f, 1.0f);

    // argument1: floor_grad + roof_grad - 2.5
    float y_bias = floor_grad + roof_grad - 2.5f;

    // base_3d_noise
    float base_noise = get_base_3d_noise(x, y, z, seed);

    // blend_density: 2.5 + (y_bias * base_noise)
    float density = 2.5f + (y_bias * (base_noise + 1.0f));

    return density;
}

// メモリバッファ
#define MAX_BLOCKS 5000
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
                // 公式 final_density を計算！
                float d = calculate_final_density((float)x, (float)y, (float)z, seed);

                // 密度がプラスなら固体（ネザーラック）
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
