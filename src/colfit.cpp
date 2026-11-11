#include "colfit.h"
#include <algorithm>

ColFitResult ColFit(const ColState& cs, int avail) {
    ColFitResult r;
    int N = 0, M = 0;
    int tx[LC_COUNT], nt = 0;
    for (int c = 0; c < LC_COUNT; c++) {
        if (!cs.on[c]) continue;
        if (ColIsFixed(c)) { r.w[c] = cs.w[c]; N += cs.w[c]; }
        else { tx[nt++] = c; M += ColMinW(c); }
    }
    if (N + M > avail) {   // はみ出す：文字の列はすべて最小
        r.overflow = true;
        for (int i = 0; i < nt; i++) r.w[tx[i]] = ColMinW(tx[i]);
        return r;
    }
    long long rem = avail - N;       // 文字の列に分ける幅
    bool fixed[LC_COUNT] = {};
    for (;;) {
        long long sumW = 0;
        for (int i = 0; i < nt; i++) if (!fixed[tx[i]]) sumW += std::max(1, cs.w[tx[i]]);
        if (sumW == 0) break;
        bool any = false;
        for (int i = 0; i < nt; i++) {
            int c = tx[i]; if (fixed[c]) continue;
            if (rem * std::max(1, cs.w[c]) < (long long)ColMinW(c) * sumW) { fixed[c] = true; r.w[c] = ColMinW(c); any = true; }
        }
        if (any) {
            rem = avail - N;
            for (int i = 0; i < nt; i++) if (fixed[tx[i]]) rem -= r.w[tx[i]];
            continue;
        }
        for (int i = 0; i < nt; i++) { int c = tx[i]; if (!fixed[c]) r.w[c] = (int)(rem * std::max(1, cs.w[c]) / sumW); }
        break;
    }
    int sum = 0; for (int c = 0; c < LC_COUNT; c++) sum += r.w[c];
    r.w[LC_TITLE] += avail - sum;    // 端数は Title
    return r;
}

ColFitResult ColFitDrag(const ColFitResult& start, int c, int n, int wantW) {
    ColFitResult r = start;
    int total = start.w[c] + start.w[n], lo = ColMinW(c), hi = total - ColMinW(n);
    if (lo > hi) return r;
    int nw = std::max(lo, std::min(wantW, hi));
    r.w[c] = nw; r.w[n] = total - nw;
    return r;
}

void ColFitCommit(ColState& cs, const ColFitResult& r) {
    for (int c = 0; c < LC_COUNT; c++) if (cs.on[c]) cs.w[c] = r.w[c];
}
