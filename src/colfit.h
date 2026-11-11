#pragma once
// 曲の一覧の列の表示の幅（第8段）。画面と切り離した計算だけ
#include "ui.h"

struct ColFitResult {
    int w[LC_COUNT] = {};      // 表示の幅（論理px。出していない列は 0）
    bool overflow = false;     // 文字の列を最小にしても avail に入らない（横スクロールバーを出す）
};
// avail: 一覧に使える幅（縦のバーを引いたもの）。数字の列は w のまま、文字の列は w を重みとして残りを分ける
ColFitResult ColFit(const ColState& cs, int avail);
// 境目のドラッグ：列 c と右隣 n で幅をやり取りして、c の幅を wantW に近づける（どちらも ColMinW で止まる）。start はドラッグを始めたときの表示の幅
ColFitResult ColFitDrag(const ColFitResult& start, int c, int n, int wantW);
// 離したとき：今見えている幅を w に覚え直す（出している列だけ）
void ColFitCommit(ColState& cs, const ColFitResult& r);
