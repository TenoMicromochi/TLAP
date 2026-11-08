#pragma once
#include "common.h"
#include <climits>

// 設定（exe の隣の TLAP.ini）の読み書き
namespace settings {

enum Repeat { R_STOP, R_ALL, R_ONE, R_TRACK };   // Stop at folder end（旧 Stop at end）/ Repeat folder / Repeat one / Stop after track。回す順は ONE→ALL→STOP→TRACK（main の NextRepeat）

struct Data {
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 640, h = 480;   // 窓の位置と大きさ（通常時）
    bool maximized = false;
    int volume = 30;                  // 0..100（5刻み）。初回は 30
    bool muted = false;
    int repeat = R_STOP;
    bool mini = false;                // 終了時にミニ窓だったか（次もミニ窓で起動する）
    int miniX = INT_MIN, miniY = INT_MIN;   // ミニ窓の位置（ふつうの窓とは別に覚える）
    bool topNormal = false, topMini = false;   // Always on Top（ふつうの窓とミニ窓で別々。初期値はどちらもオフ）
    bool eqOn = false;                // EQ：オン・オフ / プリセット（0..5、5 = Custom）/ 各バンドの dB
    int eqPreset = 0;
    int eqGain[10] = {};
    std::wstring visFile = L"";       // スペアナ：選んだシェーダーのファイル名（空 = None。初期値は None）
    bool infoOn = false;              // 情報パネル（左の欄）を出しているか / 幅（論理px、仕切り4pxを含む）
    int infoW = 240;
    bool miniVis = true;              // ミニ窓の下にスペアナを出す（Settings。初期値オン）
    bool openLast = true;             // 起動時に前回の場所を開く（Settings。初期値オン。第5b段）
    std::wstring lastTrack;           // 終了時に鳴っていた（選んでいた）曲のフルパス
    std::wstring visLast;             // 最後に使ったシェーダー（V・ボタンでオンに戻すとき用）
    bool colOn[15] = { true, false, true, true, false, false, false, false, true, false, false, false, false, false, false };   // 一覧の列（ui.h の ListCol の順）：# Title Artist Length が初期
    int colW[15] = { 40, 56, 240, 150, 150, 150, 48, 100, 56, 64, 80, 80, 72, 180, 120 };
    int sortCol = 0; bool sortDesc = false;
    int lang = 0;                     // 表示言語（lang::Lang。ini の Language= は en / ja / zh-CN / zh-TW）
    bool coverOn = true;             // ジャケットの欄を出す（第5d段。ini の Cover=1）
    bool defOn = false;              // 引数なしの起動で必ず defFolder を開く（第5d段。ini の DefaultFolderOn）
    std::wstring defFolder;          // そのフォルダ（ini の DefaultFolder）
    int coverW = 200;                // 左の列（ジャケット＋エクスプローラ）の幅（論理px、右端の仕切り4pxを含む。第4d段）
    int visHeight = 120;             // スペアナの欄の高さ（論理px）
};

// 読む。無い項目は既定値。窓の位置は、どのモニターにも掛からないときは既定に戻す
void Load(Data& d);
void Save(const Data& d);

}  // namespace settings
