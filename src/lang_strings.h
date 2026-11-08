// 画面の文字列の表（これが唯一の置き場）。X(ID, English, 日本語, 简体中文, 繁體中文)
// 第5段で 4言語にした（TLVV の lang と同じ作り）。言語を足すときは、各行に列を足し、lang.h の Lang と lang.cpp の kLangs を1つ増やす。
// %s は文字列、%d は数。ポップアップメニューの & と \t 以降のキーは呼び出し側で足す
// 訳さないもの：情報パネルの行の名前（INF_*、コピーして使うため）、曲のタグ、シェーダーの name、シェーダーのコンパイルエラーの文
#define TLAP_STRINGS(X) \
    /* メニューバー（括弧つきの言語は、括弧の中の英字に下線。英字は FPVH で共通） */ \
    X(M_FILE, L"File", L"ファイル(F)", L"文件(F)", L"檔案(F)") \
    X(M_PLAYBACK, L"Playback", L"再生(P)", L"播放(P)", L"播放(P)") \
    X(M_VIEW, L"View", L"表示(V)", L"查看(V)", L"檢視(V)") \
    X(M_HELP, L"Help", L"ヘルプ(H)", L"帮助(H)", L"說明(H)") \
    /* ポップアップメニュー */ \
    X(P_OPEN, L"&Open...", L"開く(&O)...", L"打开(&O)...", L"開啟(&O)...") \
    X(P_OPENFOLDER, L"Open &Folder...", L"フォルダーを開く(&F)...", L"打开文件夹(&F)...", L"開啟資料夾(&F)...") \
    X(P_SHOWEXP, L"Show in &Explorer", L"エクスプローラーで表示(&E)", L"在资源管理器中显示(&E)", L"在檔案總管中顯示(&E)") \
    X(P_SETTINGS, L"&Settings...", L"設定(&S)...", L"设置(&S)...", L"設定(&S)...") \
    X(P_EXIT, L"E&xit", L"終了(&X)", L"退出(&X)", L"結束(&X)") \
    X(P_PLAY, L"Play / &Pause", L"再生 / 一時停止(&P)", L"播放 / 暂停(&P)", L"播放 / 暫停(&P)") \
    X(P_PREV, L"P&revious", L"前の曲(&B)", L"上一首(&B)", L"上一首(&B)") \
    X(P_NEXT, L"&Next", L"次の曲(&N)", L"下一首(&N)", L"下一首(&N)") \
    X(P_REPEAT, L"&Repeat", L"リピート(&R)", L"循环(&R)", L"循環(&R)") \
    X(P_R_STOP, L"&Stop at folder end", L"フォルダーの最後で停止(&S)", L"文件夹末尾停止(&S)", L"資料夾結尾停止(&S)") \
    X(P_R_TRACK, L"Stop after &track", L"1曲で停止(&T)", L"单曲播完停止(&T)", L"單曲播完停止(&T)") \
    X(P_R_ALL, L"Repeat &folder", L"フォルダーをリピート(&F)", L"文件夹循环(&F)", L"資料夾循環(&F)") \
    X(P_R_ONE, L"Repeat &one", L"1曲リピート(&O)", L"单曲循环(&O)", L"單曲循環(&O)") \
    X(P_MUTE, L"&Mute", L"ミュート(&M)", L"静音(&M)", L"靜音(&M)") \
    X(P_MINI, L"&Mini Window", L"ミニ窓(&M)", L"迷你窗口(&M)", L"迷你視窗(&M)") \
    X(P_NORMAL, L"&Normal Window", L"通常の窓(&N)", L"普通窗口(&N)", L"一般視窗(&N)") \
    X(P_TOP, L"Always on &Top", L"常に手前に表示(&T)", L"窗口置顶(&T)", L"視窗置頂(&T)") \
    X(P_EQ, L"&Equalizer", L"イコライザー(&E)", L"均衡器(&E)", L"等化器(&E)") \
    X(P_COVER, L"&Cover", L"ジャケット(&C)", L"封面(&C)", L"封面(&C)") \
    X(P_INFO, L"&Info", L"情報(&I)", L"信息(&I)", L"資訊(&I)") \
    X(P_VISUALIZER, L"&Visualizer", L"ビジュアライザー(&V)", L"可视化效果(&V)", L"視覺效果(&V)") \
    X(P_SHOWVIS, L"&Show Visualizer", L"ビジュアライザーを表示(&S)", L"显示可视化效果(&S)", L"顯示視覺效果(&S)") \
    X(P_V_NONE, L"None", L"なし", L"无", L"無") \
    X(P_V_RELOAD, L"Reload", L"再読み込み", L"重新加载", L"重新載入") \
    X(P_V_NOFILES, L"(no .hlsl files)", L"(.hlsl ファイルなし)", L"(没有 .hlsl 文件)", L"(沒有 .hlsl 檔案)") \
    X(P_KEYS, L"&Keys...", L"キー操作(&K)...", L"快捷键(&K)...", L"快速鍵(&K)...") \
    X(P_ABOUT, L"&About", L"バージョン情報(&A)", L"关于(&A)", L"關於(&A)") \
    /* ツールチップ */ \
    X(TIP_OPEN, L"Open (Ctrl+O)", L"開く（Ctrl+O）", L"打开（Ctrl+O）", L"開啟（Ctrl+O）") \
    X(TIP_PREV, L"Previous (Left)", L"前の曲（←）", L"上一首（←）", L"上一首（←）") \
    X(TIP_PLAY, L"Play / Pause (Space)", L"再生 / 一時停止（Space）", L"播放 / 暂停（空格键）", L"播放 / 暫停（空白鍵）") \
    X(TIP_NEXT, L"Next (Right)", L"次の曲（→）", L"下一首（→）", L"下一首（→）") \
    X(TIP_REPEAT, L"Repeat: Repeat one / Repeat folder / Stop at folder end / Stop after track (R)", \
        L"リピート：1曲 / フォルダー / フォルダーの最後で停止 / 1曲で停止（R）", \
        L"循环：单曲循环 / 文件夹循环 / 文件夹末尾停止 / 单曲播完停止（R）", \
        L"循環：單曲循環 / 資料夾循環 / 資料夾結尾停止 / 單曲播完停止（R）") \
    X(TIP_MUTE, L"Mute (M)", L"ミュート（M）", L"静音（M）", L"靜音（M）") \
    X(TIP_COVER, L"Cover (C)", L"ジャケット（C）", L"封面（C）", L"封面（C）") \
    X(TIP_INFO, L"Info (I)", L"情報（I）", L"信息（I）", L"資訊（I）") \
    X(TIP_EQ, L"Equalizer (E)", L"イコライザー（E）", L"均衡器（E）", L"等化器（E）") \
    X(TIP_VIS, L"Visualizer: %s (V: on / off, right-click: list)", L"ビジュアライザー: %s（V: オン / オフ、右クリック: 一覧）", L"可视化效果: %s（V: 开 / 关，右键: 列表）", L"視覺效果: %s（V: 開 / 關，右鍵: 清單）") \
    /* 情報パネルの行の名前は英語のまま（TLIV と同じ。コピーして使うため） */ \
    X(INFO_WINDOW, L"Info", L"Info", L"Info", L"Info") \
    X(INF_TITLE, L"Title", L"Title", L"Title", L"Title") X(INF_ARTIST, L"Artist", L"Artist", L"Artist", L"Artist") \
    X(INF_ALBUM, L"Album", L"Album", L"Album", L"Album") X(INF_ALBUMARTIST, L"Album artist", L"Album artist", L"Album artist", L"Album artist") \
    X(INF_TRACK, L"Track", L"Track", L"Track", L"Track") X(INF_DISC, L"Disc", L"Disc", L"Disc", L"Disc") \
    X(INF_YEAR, L"Year", L"Year", L"Year", L"Year") X(INF_FORMAT, L"Format", L"Format", L"Format", L"Format") \
    X(INF_RATE, L"Sample rate", L"Sample rate", L"Sample rate", L"Sample rate") \
    X(INF_BITS, L"Bits", L"Bits", L"Bits", L"Bits") X(INF_CHANNELS, L"Channels", L"Channels", L"Channels", L"Channels") \
    X(INF_BITRATE, L"Bitrate", L"Bitrate", L"Bitrate", L"Bitrate") X(INF_LENGTH, L"Length", L"Length", L"Length", L"Length") \
    X(INF_FILE, L"File", L"File", L"File", L"File") X(INF_FOLDER, L"Folder", L"Folder", L"Folder", L"Folder") \
    /* 一覧・ステータス・エクスプローラ・ダイアログ */ \
    X(COL_NO, L"#", L"#", L"#", L"#") \
    X(COL_TITLE, L"Title", L"曲名", L"标题", L"標題") \
    X(COL_LEN, L"Length", L"長さ", L"时长", L"長度") \
    X(COL_TRACK, L"Track", L"トラック", L"音轨", L"音軌") \
    X(COL_ARTIST, L"Artist", L"アーティスト", L"艺术家", L"演出者") \
    X(COL_ALBUM, L"Album", L"アルバム", L"专辑", L"專輯") \
    X(COL_ALBUMARTIST, L"Album Artist", L"アルバムアーティスト", L"专辑艺术家", L"專輯演出者") \
    X(COL_YEAR, L"Year", L"年", L"年份", L"年份") \
    X(COL_GENRE, L"Genre", L"ジャンル", L"流派", L"類型") \
    X(COL_FORMAT, L"Format", L"形式", L"格式", L"格式") \
    X(COL_BITRATE, L"Bitrate", L"ビットレート", L"比特率", L"位元率") \
    X(COL_RATE, L"Sample Rate", L"サンプルレート", L"采样率", L"取樣率") \
    X(COL_SIZE, L"Size", L"サイズ", L"大小", L"大小") \
    X(COL_FILE, L"File Name", L"ファイル名", L"文件名", L"檔案名稱") \
    X(COL_MODIFIED, L"Modified", L"更新日時", L"修改日期", L"修改日期") \
    X(COL_RESET, L"Reset Columns", L"列を初期状態に戻す", L"重置列", L"重設欄位") \
    X(ST_CANT, L"Can't play this file", L"このファイルは再生できません", L"无法播放此文件", L"無法播放此檔案") \
    X(ST_NOAUDIO, L"No audio files", L"音楽ファイルがありません", L"没有音频文件", L"沒有音訊檔案")     X(ST_BADZIP, L"Can't open this ZIP", L"この ZIP は開けません", L"无法打开此 ZIP", L"無法開啟此 ZIP")     X(FILTER_AUDIO, L"Audio files", L"音楽ファイル", L"音频文件", L"音訊檔案") \
    X(FILTER_ALL, L"All files", L"すべてのファイル", L"所有文件", L"所有檔案") \
    X(EXP_DRIVES, L"Drives", L"ドライブ", L"驱动器", L"磁碟機") \
    X(EXP_CANT, L"(cannot open)", L"(開けません)", L"(无法打开)", L"(無法開啟)") \
    /* EQ パネル */ \
    X(EQ_TITLE, L"Equalizer", L"イコライザー", L"均衡器", L"等化器") \
    X(EQ_ON, L"On", L"オン", L"开", L"開") \
    X(EQ_P_FLAT, L"Flat", L"フラット", L"平坦", L"平坦") \
    X(EQ_P_BASS, L"Bass", L"低音強調", L"低音", L"低音") \
    X(EQ_P_TREBLE, L"Treble", L"高音強調", L"高音", L"高音") \
    X(EQ_P_VOCAL, L"Vocal", L"ボーカル", L"人声", L"人聲") \
    X(EQ_P_LOUD, L"Loudness", L"ラウドネス", L"响度", L"響度") \
    X(EQ_P_CUSTOM, L"Custom", L"カスタム", L"自定义", L"自訂") \
    /* スペアナの知らせ（シェーダーのコンパイルエラーの文は訳さない） */ \
    X(VIS_GPU, L"GPU timeout", L"GPU がタイムアウトしました", L"GPU 超时", L"GPU 逾時") \
    X(VIS_BIG, L"file too big (256KB limit)", L"ファイルが大きすぎます（256KB まで）", L"文件过大（上限 256KB）", L"檔案過大（上限 256KB）") \
    X(VIS_READ, L"can't read the file", L"ファイルを読めません", L"无法读取文件", L"無法讀取檔案") \
    X(VIS_PS, L"can't create the pixel shader", L"ピクセルシェーダーを作れません", L"无法创建像素着色器", L"無法建立像素著色器") \
    X(VIS_D3D, L"can't start Direct3D 11", L"Direct3D 11 を開始できません", L"无法启动 Direct3D 11", L"無法啟動 Direct3D 11") \
    X(VIS_NODLL, L"d3dcompiler_47.dll not found", L"d3dcompiler_47.dll が見つかりません", L"找不到 d3dcompiler_47.dll", L"找不到 d3dcompiler_47.dll") \
    X(VIS_INC, L"#include is not allowed", L"#include は使えません", L"不能使用 #include", L"不可使用 #include") \
    /* Settings */ \
    X(SET_TITLE, L"Settings", L"設定", L"设置", L"設定") \
    X(SET_LANGUAGE, L"Language:", L"言語:", L"语言:", L"語言:") \
    X(SET_MINIVIS, L"Show visualizer in mini window", L"ミニ窓の下にビジュアライザーを表示", L"在迷你窗口下方显示可视化效果", L"在迷你視窗下方顯示視覺效果") \
    X(SET_OPENLAST, L"Open last folder on startup", L"起動時に前回のフォルダーを開く", L"启动时打开上次的文件夹", L"啟動時開啟上次的資料夾") \
    X(SET_DEFON, L"Always open this folder on startup", L"起動時に必ずこのフォルダーを開く", L"启动时始终打开此文件夹", L"啟動時一律開啟此資料夾") \
    X(SET_BROWSE, L"Browse...", L"参照...", L"浏览...", L"瀏覽...") \
    X(BTN_OK, L"OK", L"OK", L"确定", L"確定") \
    X(BTN_CANCEL, L"Cancel", L"キャンセル", L"取消", L"取消") \
    /* Keys / About */ \
    X(KEYS_TITLE, L"Keys", L"キー操作", L"快捷键", L"快速鍵") \
    X(ABOUT_TITLE, L"About TLAP", L"TLAP のバージョン情報", L"关于 TLAP", L"關於 TLAP") \
    X(ABOUT_BODY, \
        L"TLAP %s\nTenokun's Light Audio Player\n\nby Micromochi_Teno\nMIT License (see LICENSE and THIRD_PARTY_NOTICES.txt)", \
        L"TLAP %s\nTenokun's Light Audio Player\n\nby Micromochi_Teno\nMIT License（LICENSE と THIRD_PARTY_NOTICES.txt を参照）", \
        L"TLAP %s\nTenokun's Light Audio Player\n\nby Micromochi_Teno\nMIT 许可证（见 LICENSE 和 THIRD_PARTY_NOTICES.txt）", \
        L"TLAP %s\nTenokun's Light Audio Player\n\nby Micromochi_Teno\nMIT 授權（見 LICENSE 與 THIRD_PARTY_NOTICES.txt）") \
    X(KEYS_BODY, \
        L"Space\tPlay / Pause\n" \
        L"Left / Right\tPrevious / Next track\n" \
        L"Shift + Left / Right\t5 seconds back / forward\n" \
        L"Up / Down\tVolume +5% / -5%\n" \
        L"M\tMute\n" \
        L"R\tChange repeat mode\n" \
        L"E\tEqualizer panel\n" \
        L"V\tVisualizer on / off\n" \
        L"I\tInfo panel\n" \
        L"C\tCover on / off\n" \
        L"Ctrl + M\tMini window <-> normal window\n" \
        L"Ctrl + T\tAlways on top\n" \
        L"Ctrl + O\tOpen a song\n" \
        L"Ctrl + Shift + O\tOpen a folder\n" \
        L"Ctrl + ,\tSettings\n" \
        L"Enter\tPlay the selected song\n\n" \
        L"Wheel on the seek bar: +/-5 seconds\n" \
        L"Wheel on the volume slider: +/-5%\n" \
        L"Media keys: play/pause, previous, next, stop", \
        L"Space\t再生 / 一時停止\n" \
        L"← / →\t前の曲 / 次の曲\n" \
        L"Shift + ← / →\t5秒戻る / 進む\n" \
        L"↑ / ↓\t音量 +5% / -5%\n" \
        L"M\tミュート\n" \
        L"R\tリピートの切り替え\n" \
        L"E\tイコライザー\n" \
        L"V\tビジュアライザーのオン / オフ\n" \
        L"I\t情報パネル\n" \
        L"C\tジャケットの表示 / 非表示\n" \
        L"Ctrl + M\tミニ窓 <-> 通常の窓\n" \
        L"Ctrl + T\t常に手前に表示\n" \
        L"Ctrl + O\t曲を開く\n" \
        L"Ctrl + Shift + O\tフォルダーを開く\n" \
        L"Ctrl + ,\t設定\n" \
        L"Enter\t選んだ曲を再生\n\n" \
        L"シークバーの上でホイール: ±5秒\n" \
        L"音量スライダーの上でホイール: ±5%\n" \
        L"メディアキー: 再生/一時停止、前、次、停止", \
        L"空格键\t播放 / 暂停\n" \
        L"← / →\t上一首 / 下一首\n" \
        L"Shift + ← / →\t后退 / 前进 5 秒\n" \
        L"↑ / ↓\t音量 +5% / -5%\n" \
        L"M\t静音\n" \
        L"R\t切换循环模式\n" \
        L"E\t均衡器\n" \
        L"V\t开 / 关可视化效果\n" \
        L"I\t信息面板\n" \
        L"C\t显示 / 隐藏封面\n" \
        L"Ctrl + M\t迷你窗口 <-> 普通窗口\n" \
        L"Ctrl + T\t窗口置顶\n" \
        L"Ctrl + O\t打开歌曲\n" \
        L"Ctrl + Shift + O\t打开文件夹\n" \
        L"Ctrl + ,\t设置\n" \
        L"Enter\t播放选中的歌曲\n\n" \
        L"在进度条上滚动滚轮: ±5 秒\n" \
        L"在音量滑块上滚动滚轮: ±5%\n" \
        L"媒体键: 播放/暂停、上一首、下一首、停止", \
        L"空白鍵\t播放 / 暫停\n" \
        L"← / →\t上一首 / 下一首\n" \
        L"Shift + ← / →\t倒退 / 前進 5 秒\n" \
        L"↑ / ↓\t音量 +5% / -5%\n" \
        L"M\t靜音\n" \
        L"R\t切換循環模式\n" \
        L"E\t等化器\n" \
        L"V\t開 / 關視覺效果\n" \
        L"I\t資訊面板\n" \
        L"C\t顯示 / 隱藏封面\n" \
        L"Ctrl + M\t迷你視窗 <-> 一般視窗\n" \
        L"Ctrl + T\t視窗置頂\n" \
        L"Ctrl + O\t開啟歌曲\n" \
        L"Ctrl + Shift + O\t開啟資料夾\n" \
        L"Ctrl + ,\t設定\n" \
        L"Enter\t播放選取的歌曲\n\n" \
        L"在進度列上滾動滾輪: ±5 秒\n" \
        L"在音量滑桿上滾動滾輪: ±5%\n" \
        L"媒體鍵: 播放/暫停、上一首、下一首、停止")
