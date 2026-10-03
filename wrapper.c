#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <emscripten.h>
#include "cubiomes/generator.h"

// JavaScriptから呼び出せるように公開する関数
EMSCRIPTEN_KEEPALIVE
int get_nether_biome(int64_t seed, int x, int y, int z) {
    // マイクラ1.20用のジェネレータ初期化
    Generator g;
    setupGenerator(&g, MC_1_20, 0);
    applySeed(&g, DIM_NETHER, seed);

    // 指定された座標のバイオームIDを取得して返す
    int biomeID = getBiomeAt(&g, 4, x, y, z);
    return biomeID;
}

// 動作確認用テスト関数
EMSCRIPTEN_KEEPALIVE
int test_ping(int value) {
    return value * 2;
}
