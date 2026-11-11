// colfit のテスト（画面なし）。失敗が1つでもあれば 0 以外で終わる
#include "../src/colfit.h"
#include <cstdio>
static int g_fail = 0, g_n = 0;
#define CHECK(x) do { g_n++; if (!(x)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static int Sum(const ColFitResult& r) { int s = 0; for (int i = 0; i < LC_COUNT; i++) s += r.w[i]; return s; }

int main() {
    ColState cs;   // 初期：# 40, Title 240, Artist 150, Length 56
    // C2
    ColFitResult r = ColFit(cs, 486);
    CHECK(!r.overflow); CHECK(Sum(r) == 486);
    CHECK(r.w[LC_NO] == 40 && r.w[LC_LEN] == 56);
    CHECK(r.w[LC_TITLE] == 240 && r.w[LC_ARTIST] == 150);
    for (int a : { 1200, 300, 487, 333, 777 }) {
        r = ColFit(cs, a);
        CHECK(!r.overflow); CHECK(Sum(r) == a);
        CHECK(r.w[LC_NO] == 40 && r.w[LC_LEN] == 56);
        CHECK(r.w[LC_TITLE] >= 80 && r.w[LC_ARTIST] >= 24);
        int R = a - 96;   // 比（Artist は切り捨て、残りは Title）
        if (a >= 300) CHECK(r.w[LC_ARTIST] == (int)((long long)R * 150 / 390));
    }
    r = ColFit(cs, 1200); CHECK(r.w[LC_TITLE] + r.w[LC_ARTIST] == 1104);
    // 出していない列は 0
    CHECK(r.w[LC_ALBUM] == 0 && r.w[LC_YEAR] == 0);
    // C3: 比で分けると Artist が最小を下回る
    ColState c3; c3.w[LC_TITLE] = 1000; c3.w[LC_ARTIST] = 10; c3.on[LC_ALBUM] = true; c3.w[LC_ALBUM] = 500;
    r = ColFit(c3, 696);   // N=96, R=600, M=80+24+24=128
    CHECK(!r.overflow); CHECK(Sum(r) == 696);
    CHECK(r.w[LC_ARTIST] == 24);                       // 最小に固定
    CHECK(r.w[LC_TITLE] + r.w[LC_ALBUM] == 600 - 24);  // 残りを分ける
    CHECK(r.w[LC_TITLE] == 576 * 1000 / 1500 + (576 - 576 * 1000 / 1500 - 576 * 500 / 1500));   // Title: 比 2:1 の 384（端数は Title）
    CHECK(r.w[LC_ALBUM] == 576 * 500 / 1500);
    // 連鎖：固定のあとに別の列も最小を下回る
    ColState c3b; c3b.w[LC_TITLE] = 100; c3b.w[LC_ARTIST] = 100; c3b.on[LC_ALBUM] = true; c3b.w[LC_ALBUM] = 100;
    r = ColFit(c3b, 96 + 80 + 24 + 24 + 10);   // R=138, M=128：どれも最小に近い
    CHECK(!r.overflow); CHECK(Sum(r) == 96 + 138);
    CHECK(r.w[LC_TITLE] >= 80 && r.w[LC_ARTIST] >= 24 && r.w[LC_ALBUM] >= 24);
    // C4: はみ出す
    ColState c4; c4.on[LC_TRACK] = c4.on[LC_FORMAT] = c4.on[LC_BITRATE] = c4.on[LC_RATE] = true;
    // 固定 40+56+56+64(Format)+80+80=376、文字 Title80+Artist24=104 → 480
    r = ColFit(c4, 479); CHECK(r.overflow);
    CHECK(r.w[LC_TITLE] == 80 && r.w[LC_ARTIST] == 24 && r.w[LC_FORMAT] == 64);
    CHECK(r.w[LC_NO] == 40 && r.w[LC_TRACK] == 56 && r.w[LC_LEN] == 56 && r.w[LC_BITRATE] == 80 && r.w[LC_RATE] == 80);
    CHECK(Sum(r) == 480);
    r = ColFit(c4, 480); CHECK(!r.overflow); CHECK(Sum(r) == 480);   // ちょうど収まる
    // Format と Modified は固定：狭くても広くても w のまま
    ColState f; f.on[LC_FORMAT] = f.on[LC_MODIFIED] = true;
    for (int av : { 500, 700, 1600 }) {
        r = ColFit(f, av); CHECK(!r.overflow); CHECK(Sum(r) == av);
        CHECK(r.w[LC_FORMAT] == 64 && r.w[LC_MODIFIED] == 120);
    }
    r = ColFit(c4, 100); CHECK(r.overflow); CHECK(r.w[LC_TITLE] == 80);
    // C5: ドラッグと覚え直し
    ColState d; ColFitResult s = ColFit(d, 600);   // Title 240+差, Artist
    int t0 = s.w[LC_TITLE], a0 = s.w[LC_ARTIST];
    ColFitResult dr = ColFitDrag(s, LC_TITLE, LC_ARTIST, t0 - 100);
    CHECK(dr.w[LC_TITLE] == t0 - 100 && dr.w[LC_ARTIST] == a0 + 100); CHECK(Sum(dr) == 600);
    CHECK(dr.w[LC_NO] == 40 && dr.w[LC_LEN] == 56);
    dr = ColFitDrag(s, LC_TITLE, LC_ARTIST, 5);          // 左へ行き過ぎ：Title は最小 80
    CHECK(dr.w[LC_TITLE] == 80 && dr.w[LC_ARTIST] == t0 + a0 - 80);
    dr = ColFitDrag(s, LC_TITLE, LC_ARTIST, 9999);       // 右へ行き過ぎ：Artist は最小 24
    CHECK(dr.w[LC_ARTIST] == 24 && dr.w[LC_TITLE] == t0 + a0 - 24);
    dr = ColFitDrag(s, LC_NO, LC_TITLE, 100);            // 数字の列と文字の列
    CHECK(dr.w[LC_NO] == 100 && dr.w[LC_TITLE] == t0 - 60 && Sum(dr) == 600);
    dr = ColFitDrag(s, LC_ARTIST, LC_LEN, a0 + 20);     // Length は数字の列：56 → 36
    CHECK(dr.w[LC_LEN] == 56 - 20 && dr.w[LC_ARTIST] == a0 + 20);
    dr = ColFitDrag(s, LC_TITLE, LC_ARTIST, t0 - 100);
    ColFitCommit(d, dr);
    ColFitResult again = ColFit(d, 600);
    bool same = true; for (int i = 0; i < LC_COUNT; i++) if (again.w[i] != dr.w[i]) same = false;
    CHECK(same); CHECK(!again.overflow);
    // 広げると同じ比
    ColFitResult wide = ColFit(d, 1600);
    CHECK(Sum(wide) == 1600);
    long long lhs = (long long)wide.w[LC_TITLE] * dr.w[LC_ARTIST], rhs = (long long)wide.w[LC_ARTIST] * dr.w[LC_TITLE];
    CHECK(lhs - rhs < 2 * (dr.w[LC_TITLE] + dr.w[LC_ARTIST]) && rhs - lhs < 2 * (dr.w[LC_TITLE] + dr.w[LC_ARTIST]));
    // 文字の列が3つで、離したあとも跳ねない
    ColState e; e.on[LC_ALBUM] = e.on[LC_GENRE] = true;
    ColFitResult es = ColFit(e, 800), ed = ColFitDrag(es, LC_ARTIST, LC_ALBUM, es.w[LC_ARTIST] + 37);
    ColFitCommit(e, ed); ColFitResult ea = ColFit(e, 800);
    same = true; for (int i = 0; i < LC_COUNT; i++) if (ea.w[i] != ed.w[i]) same = false;
    CHECK(same);
    printf("%d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
