#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <emscripten.h>
#include "cubiomes/generator.h"

// 疑似乱数と3Dパーリンノイズ
static inline float hash(int n) {
    n = (n << 13) ^ n;
    return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
}

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

static float sampleNetherTerrain(float x, float y, float z, int seed) {
    float density = 0.0f;
    float freq = 0.04f;
    float amp = 1.0f;

    for (int i = 0; i < 3; i++) {
        density += noise3D(x * freq, y * freq, z * freq, seed + i * 31) * amp;
        freq *= 2.0f;
        amp *= 0.5f;
    }

    if (y < 26.0f) {
        density += (26.0f - y) * 0.12f;
    } else if (y > 105.0f) {
        density += (y - 105.0f) * 0.12f;
    } else {
        density -= 0.25f;
    }

    return density;
}

// 3Dスキャン関数
EMSCRIPTEN_KEEPALIVE
int scan_nether_3d(int seed, int minX, int maxX, int minZ, int maxZ, int stepH, int stepY, uint8_t* out_buffer, int max_blocks) {
    int block_count = 0;

    for (int x = minX; x <= maxX; x += stepH) {
        for (int z = minZ; z <= maxZ; z += stepH) {
            for (int y = 16; y <= 118; y += stepY) {
                float d = sampleNetherTerrain((float)x, (float)y, (float)z, seed);

                if (d > 0.15f) {
                    if (block_count < max_blocks) {
                        int idx = block_count * 4;
                        out_buffer[idx + 0] = (uint8_t)(x - minX);
                        out_buffer[idx + 1] = (uint8_t)y;
                        out_buffer[idx + 2] = (uint8_t)(z - minZ);
                        out_buffer[idx + 3] = 1;
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
