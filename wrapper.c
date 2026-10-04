#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <emscripten.h>
#include "cubiomes/generator.h"
#include "cubiomes/noise.h"

// --- Minecraft Java公式 BlendedNoise の完全再現 ---

#define OCTAVES_LIMIT 16
#define OCTAVES_MAIN  8

typedef struct {
    PerlinNoise octaves[OCTAVES_LIMIT];
    int count;
} OctaveSampler;

static OctaveSampler g_lower_noise;
static OctaveSampler g_upper_noise;
static OctaveSampler g_main_noise;

static int g_blended_initialized = 0;
static int64_t g_last_seed = -1;

// オクターブノイズの初期化（Java公式と同じ周波数配置）
static void init_octaves(OctaveSampler* sampler, int count, uint64_t* seed_state) {
    sampler->count = count;
    for (int i = 0; i < count; i++) {
        perlinInit(&sampler->octaves[i], seed_state);
    }
}

// オクターブノイズのサンプリング
static double sample_octaves(const OctaveSampler* sampler, double x, double y, double z) {
    double total = 0.0;
    double freq = 1.0;
    double amp = 1.0;

    for (int i = 0; i < sampler->count; i++) {
        double nx = x * freq;
        double ny = y * freq;
        double nz = z * freq;
        total += samplePerlin(&sampler->octaves[i], nx, ny, nz) * amp;
        freq *= 2.0;
        amp *= 0.5;
    }
    return total;
}

// シード値から公式BlendedNoiseを完全初期化
static void init_official_blended_noise(int64_t seed) {
    if (g_blended_initialized && g_last_seed == seed) return;

    // Java公式のシードハッシュ展開
    uint64_t s_lower = (uint64_t)seed ^ 0x5deece66dULL;
    uint64_t s_upper = (uint64_t)(seed + 1) ^ 0x5deece66dULL;
    uint64_t s_main  = (uint64_t)(seed + 2) ^ 0x5deece66dULL;

    init_octaves(&g_lower_noise, OCTAVES_LIMIT, &s_lower);
    init_octaves(&g_upper_noise, OCTAVES_LIMIT, &s_upper);
    init_octaves(&g_main_noise,  OCTAVES_MAIN,  &s_main);

    g_blended_initialized = 1;
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
    // xz_scale = 1.0, y_scale = 2.0, xz_factor = 80.0, y_factor = 160.0
    double xz_factor = 80.0;
    double y_factor  = 160.0;

    double scaleX = 1.0 / xz_factor;
    double scaleY = 2.0 / y_factor;
    double scaleZ = 1.0 / xz_factor;

    // 補間用メインノイズ（Smearスケール）
    double mainX = (double)x * (scaleX / 8.0);
    double mainY = (double)y * (scaleY / 8.0);
    double mainZ = (double)z * (scaleZ / 8.0);

    double mainVal = sample_octaves(&g_main_noise, mainX, mainY, mainZ);
    double alpha = (mainVal * 0.1 + 1.0) * 0.5;
    if (alpha < 0.0) alpha = 0.0;
    if (alpha > 1.0) alpha = 1.0;

    // 境界ノイズサンプリング
    double boundX = (double)x * scaleX;
    double boundY = (double)y * scaleY;
    double boundZ = (double)z * scaleZ;

    double lowerVal = sample_octaves(&g_lower_noise, boundX, boundY, boundZ);
    double upperVal = sample_octaves(&g_upper_noise, boundX, boundY, boundZ);

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
                   
