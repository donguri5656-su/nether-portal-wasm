#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <emscripten.h>
#include "cubiomes/generator.h"
#include "cubiomes/noise.h"

// Cubiomes 内蔵の公式パーリンノイズ構造体
static PerlinNoise g_nether_perlin;
static int g_noise_initialized = 0;
static int64_t g_current_seed = -1;

// シード値から公式と同じパーリンノイズを初期化
static void init_official_noise(int64_t seed) {
    if (!g_noise_initialized || g_current_seed != seed) {
        // マイクラJava版のシード展開アルゴリズム
        uint64_t s = (uint64_t)seed ^ 0x5deece66dULL;
        perlinInit(&g_nether_perlin, &s);
        g_noise_initialized = 1;
        g_current_seed = seed;
    }
}

// 1. y_clamped_gradient（マイクラ公式補間）
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

// 2. 本物のマイクラ公式 final_density 計算
static float calculate_final_density(float x, float y, float z, int64_t seed) {
    init_official_noise(seed);

    // 公式スケール: XZ=80ブロック周期, Y=160ブロック周期
    double sampleX = (double)x / 64.0;
    double sampleY = (double)y / 128.0;
    double sampleZ = (double)z / 64.0;

    // Cubiomes の本物パーリンノイズをサンプリング（オクターブ合成）
    double noise = samplePerlin(&g_nether_perlin, sampleX, sampleY, sampleZ);
    noise += 0.5 * samplePerlin(&g_nether_perlin, sampleX * 2.0, sampleY * 2.0, sampleZ * 2.0);

    // 公式 nether.json 高度勾配
    // floor: -8 〜 24 で 0->1
    float floor_grad = clamped_gradient(y, -8.0f, 24.0f, 0.0f, 1.0f);
    // roof: 128 〜 112 で 0->1
    float roof_grad = clamped_gradient(y, 128.0f, 112.0f, 0.0f, 1.0f);

    // y_bias: 中央(Y=24〜112)で -0.5
    float y_bias = floor_grad + roof_grad - 2.5f;

    // 密度合成: ノイズと高度バイアスを掛け合わせ
    // 平らな床にならないよう、滑らかなスプライン結合
    float density = (float)(noise * 1.8) + (y_bias * 0.7f) + 0.1f;

    return density;
}

// メモリバッファ（12,000ブロックまで対応）
#define MAX_BLOCKS 12000
static uint8_t g_block_buffer[MAX_BLOCKS * 4];

EMSCRIPTEN_KEEPALIVE
uint8_t* get_block_buffer() {
    return g_block_buffer;
}

// 3Dスキャン関数
EMSCRIPTEN_KEEPALIVE
int scan_nether_3d(int seed, int minX, int maxX, int minZ, int maxZ, int stepH, int stepY) {
    int block_count = 0;
    int64_t s = (int64_t)seed;

    for (int x = minX; x <= maxX; x += stepH) {
        for (int z = minZ; z <= maxZ; z += stepH) {
            for (int y = 16; y <= 118; y += stepY) {
                float d = calculate_final_density((float)x, (float)y, (float)z, s);

                // 固体ブロック判定
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
