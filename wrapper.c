#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <emscripten.h>
#include "cubiomes/generator.h"
#include "cubiomes/noise.h"

// --- Minecraft Java公式 BlendedNoise (計40層オクターブ) ---

#define TOTAL_OCTAVES 40

static PerlinNoise g_perlin_pool[TOTAL_OCTAVES];
static OctaveNoise g_octmin;   // 16層下限ノイズ
static OctaveNoise g_octmax;   // 16層上限ノイズ
static OctaveNoise g_octmain;  // 8層補間ノイズ

static int g_noise_initialized = 0;
static int64_t g_last_seed = -1;

// シード値から公式BlendedNoise（40層）を初期化
static void init_official_blended_noise(int64_t seed) {
    if (g_noise_initialized && g_last_seed == seed) return;

    uint64_t s = (uint64_t)seed;

    // Cubiomes公式のオクターブ初期化
    // octmin: 16オクターブ (omin: -15, len: 16)
    octaveInit(&g_octmin,  &s, g_perlin_pool + 0,  -15, 16);
    // octmax: 16オクターブ (omin: -15, len: 16)
    octaveInit(&g_octmax,  &s, g_perlin_pool + 16, -15, 16);
    // octmain: 8オクターブ (omin: -7, len: 8)
    octaveInit(&g_octmain, &s, g_perlin_pool + 32, -7,  8);

    g_noise_initialized = 1;
    g_last_seed = seed;
}

// 公式 y_clamped_gradient
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

// 公式線形補間
static inline double lerp(double a, double b, double t) {
    return a + t * (b - a);
}

// 公式 final_density 完全評価
static float calculate_official_nether_density(float x, float y, float z, int64_t seed) {
    init_official_blended_noise(seed);

    // 公式 nether/base_3d_noise スケール
    // xz_factor = 80.0, y_factor = 160.0
    double scaleX = 1.0 / 80.0;
    double scaleY = 2.0 / 160.0;
    double scaleZ = 1.0 / 80.0;

    // 補間用メインノイズ（Smearスケール 1/8）
    double mainX = (double)x * (scaleX / 8.0);
    double mainY = (double)y * (scaleY / 8.0);
    double mainZ = (double)z * (scaleZ / 8.0);

    double mainVal = sampleOctave(&g_octmain, mainX, mainY, mainZ);
    double alpha = (mainVal * 0.1 + 1.0) * 0.5;
    if (alpha < 0.0) alpha = 0.0;
    if (alpha > 1.0) alpha = 1.0;

    // 境界ノイズサンプリング
    double boundX = (double)x * scaleX;
    double boundY = (double)y * scaleY;
    double boundZ = (double)z * scaleZ;

    double lowerVal = sampleOctave(&g_octmin, boundX, boundY, boundZ);
    double upperVal = sampleOctave(&g_octmax, boundX, boundY, boundZ);

    double base_noise = lerp(lowerVal, upperVal, alpha) / 128.0;

    // 公式高度勾配 G(Y)
    float floor_grad = clamped_gradient(y, -8.0f, 24.0f, 0.0f, 1.0f);
    float roof_grad  = clamped_gradient(y, 128.0f, 112.0f, 0.0f, 1.0f);
    float g_y = floor_grad + roof_grad - 2.5f;

    // final_density = 2.5 + G(Y) * base_3d_noise
    float density = 2.5f + (g_y * (float)base_noise);

    return density;
}

// メモリバッファ（12,000ブロック）
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
                float d = calculate_official_nether_density((float)x, (float)y, (float)z, s);

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
