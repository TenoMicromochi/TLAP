TLAP — Tenokun's Light Audio Player
超軽量の Windows 用音楽プレーヤー / A very light music player for Windows

----------------------------------------
[English]
----------------------------------------
TLAP plays the songs in the folder you are looking at. No library, no playlists, no shuffle.
Open a song, and the songs of its folder line up and play from that one.

How to run
- Installer: run TLAPSetup-<version>.exe (per-user, no administrator rights needed).
  Uninstall from Windows Settings > Apps > Installed apps.
- Portable: unzip TLAP-<version>-portable.zip anywhere and run TLAP\TLAP.exe.
  Settings are saved in TLAP.ini next to the exe. Keep the visualizers folder next to it.
- To open songs with TLAP: right-click a song > Open with > Choose another app > TLAP.
  (Installer only. TLAP never takes over your default app.)
- Or start TLAP and drop a song or a folder on the window, or press Ctrl+O.
- TLAP runs only one window. Opening another song hands it to the running TLAP.
- A ZIP full of songs can be dropped on TLAP and played as it is.

Keys and mouse
 Space                     Play / pause
 Left / Right              Previous / next song (Left within 3 s of the start goes back one song; later it restarts the song)
 Shift+Left / Right        Seek -5 s / +5 s
 Up / Down                 Volume +5% / -5%
 M                         Mute
 R                         Switch repeat: Repeat one / Repeat folder / Stop at folder end / Stop after track
 E                         Equalizer panel
 V                         Visualizer on / off
 I                         Info panel (select text and Ctrl+C to copy)
 C                         Cover on / off
 Ctrl+M                    Mini window <-> normal window
 Ctrl+T                    Always on top
 Ctrl+O / Ctrl+Shift+O     Open a song / open a folder
 Ctrl+,                    Settings
 Enter                     Play the selected song (in the list or the explorer)
 Media keys                Play/pause, previous, next, stop (work even when TLAP is behind other windows)
 Wheel                     Seek bar: +-5 s / volume slider: +-5% / list: scroll
 Seek bar                  Click or drag to jump

Song list: click a column header to sort (click again to reverse). The play order follows the sort.
Right-click the header to choose columns (Title cannot be hidden) or Reset Columns. Drag the edge of a header to change its width.
Left column: the cover on top and a small folder explorer below. Double-click a folder to go in, a song to play it
(Backspace goes up). Drag the thin dividers to change the width of the left column and the info panel.

Mini window: Ctrl+M or double-click. Drag anywhere to move it. A visualizer can be shown under it. Right-click for its menu.

Equalizer: 10 bands, +-12 dB, presets Flat / Bass / Treble / Vocal / Loudness. The preamp is automatic (no clipping).

Visualizer: drawn by an HLSL pixel shader. The button toggles it on/off; right-click the button for the list of shaders and Reload.
To add your own, put a .hlsl file in the visualizers folder next to TLAP.exe (see visualizers\README.txt).
A saved shader is reloaded within a second. The bundled shaders come back when you install again,
so save your edited version under another name.

Settings (Ctrl+,): language (English / Japanese / Simplified / Traditional Chinese), show visualizer in mini window,
open last folder on startup, always open a chosen folder on startup.
Volume, mute, repeat, EQ, visualizer, columns and window positions are saved automatically.

Formats: MP3, M4A (AAC / ALAC), AAC, FLAC, WAV, WMA, OGG (Vorbis / Opus), OGA, OPUS. Songs play gaplessly.
Requires: Windows 10 / 11 (x64).

Note
- TLAP is not code-signed, so Windows SmartScreen may warn you.
  Click "More info" > "Run anyway".
- Use at your own risk.
- MIT License. Free to use. Third-party licenses: THIRD_PARTY_NOTICES.txt.

----------------------------------------
[日本語]
----------------------------------------
TLAP は、今見ているフォルダの曲を鳴らします。ライブラリ・プレイリスト・シャッフルはありません。
曲を開くと、そのフォルダの曲が一覧に並び、その曲から鳴ります。

動かし方
- インストーラー：TLAPSetup-<version>.exe を実行します（ユーザー単位。管理者権限は不要）。
  アンインストールは Windows の設定「アプリ > インストールされているアプリ」から。
- ポータブル版：TLAP-<version>-portable.zip を好きな場所に展開し、TLAP\TLAP.exe を実行します。
  設定は exe の隣の TLAP.ini に保存されます。visualizers フォルダは exe の隣に置いたままにしてください。
- 曲を TLAP で開く：曲を右クリック > プログラムから開く > 別のプログラムを選択 > TLAP。
  （インストーラー版のみ。既定のアプリは奪いません。）
- TLAP を起動して、窓に曲やフォルダをドラッグ&ドロップするか、Ctrl+O でも開けます。
- TLAP は1つだけ起動します。ほかの曲を開くと、起動中の TLAP に渡されます。
- 曲の入った ZIP は、そのまま TLAP に投げても聴けます。

操作
 Space                     再生・一時停止
 ← →                      前の曲 / 次の曲（← は曲の頭から 3 秒以内なら前の曲へ、それより後なら曲の頭へ）
 Shift+← →               5秒戻る / 進む
 ↑ ↓                      音量 +5% / -5%
 M                          ミュート
 R                          リピートの切り替え：Repeat one / Repeat folder / Stop at folder end / Stop after track
 E                          EQ パネル
 V                          スペアナのオン／オフ
 I                          情報パネル（文字を選んで Ctrl+C でコピーできます）
 C                          ジャケットの表示／非表示
 Ctrl+M                     ミニ窓 ↔ ふつうの窓
 Ctrl+T                     常に手前に表示
 Ctrl+O / Ctrl+Shift+O      曲を開く / フォルダを開く
 Ctrl+,                     設定
 Enter                      選んだ曲を再生（一覧・エクスプローラ）
 メディアキー               再生/一時停止・前・次・停止（TLAP が後ろにあっても効きます）
 ホイール                   シークバー：±5秒 / 音量スライダー：±5% / 一覧：スクロール
 シークバー                 クリック・ドラッグで移動

曲の一覧：見出しをクリックで並べ替え（もう一度で逆順）。再生の順番もその並びになります。
見出しを右クリックで表示する列を選べます（Title は外せません）。Reset Columns で元に戻ります。見出しの境目のドラッグで幅を変えられます。
左の列：上にジャケット、下に小さなフォルダのエクスプローラ。フォルダのダブルクリックで中へ、曲のダブルクリックで再生
（Backspace で上へ）。細い仕切りのドラッグで、左の列と情報パネルの幅を変えられます。

ミニ窓：Ctrl+M かダブルクリック。どこをドラッグしても動かせます。下にスペアナを出せます。右クリックでメニュー。

EQ：10バンド、±12dB、プリセット Flat / Bass / Treble / Vocal / Loudness。プリアンプは自動です（音割れしません）。

スペアナ：HLSL のピクセルシェーダーで描きます。ボタンでオン／オフ、ボタンの右クリックでシェーダーの一覧と Reload。
自分のシェーダーを足すには、TLAP.exe の隣の visualizers フォルダに .hlsl を置きます（visualizers\README.txt 参照）。
保存すると 1 秒以内に読み直されます。同梱のシェーダーは上書きインストールで元に戻るので、
書き換えるときは別名で保存してください。

設定（Ctrl+,）：言語（英語 / 日本語 / 簡体字 / 繁体字）、ミニ窓の下のスペアナ、
起動時に前回のフォルダを開く、起動時に決めたフォルダを必ず開く。
音量・ミュート・リピート・EQ・スペアナ・列・窓の位置は自動で保存されます。

対応形式：MP3, M4A（AAC / ALAC）, AAC, FLAC, WAV, WMA, OGG（Vorbis / Opus）, OGA, OPUS。曲の継ぎ目に無音は入りません（ギャップレス）。
動作環境：Windows 10 / 11（x64）

注意
- コード署名がないため、Windows の SmartScreen が警告を出すことがあります。
  「詳細情報」→「実行」を選んでください。
- 自己責任でお使いください。
- MIT ライセンスです。自由に使ってください。外部ライブラリのライセンス：THIRD_PARTY_NOTICES.txt

----------------------------------------
[简体中文]
----------------------------------------
TLAP 只播放你当前所在文件夹里的歌曲。没有媒体库、播放列表和随机播放。
打开一首歌，它所在文件夹的歌曲会排成列表，并从这一首开始播放。

运行方法
- 安装程序：运行 TLAPSetup-<version>.exe（仅为当前用户安装，无需管理员权限）。
  卸载请在 Windows 设置 > 应用 > 已安装的应用 中进行。
- 便携版：把 TLAP-<version>-portable.zip 解压到任意位置，运行 TLAP\TLAP.exe。
  设置保存在 exe 旁边的 TLAP.ini 中。请让 visualizers 文件夹保持在 exe 旁边。
- 用 TLAP 打开歌曲：右键单击歌曲 > 打开方式 > 选择其他应用 > TLAP。
  （仅限安装程序版。TLAP 不会占用你的默认应用。）
- 也可以先启动 TLAP，然后把歌曲或文件夹拖到窗口上，或按 Ctrl+O。
- TLAP 只会运行一个窗口。再打开别的歌曲时，会交给正在运行的 TLAP。
- 装有歌曲的 ZIP 可以直接拖到 TLAP 上播放。

操作
 空格键                     播放 / 暂停
 ← →                      上一首 / 下一首（← 在歌曲开头 3 秒内回到上一首，之后则回到本曲开头）
 Shift+← →               后退 / 前进 5 秒
 ↑ ↓                      音量 +5% / -5%
 M                          静音
 R                          切换重复：Repeat one / Repeat folder / Stop at folder end / Stop after track
 E                          均衡器面板
 V                          频谱可视化 开 / 关
 I                          信息面板（选中文字后可用 Ctrl+C 复制）
 C                          封面 显示 / 隐藏
 Ctrl+M                     迷你窗口 ↔ 普通窗口
 Ctrl+T                     窗口置顶
 Ctrl+O / Ctrl+Shift+O      打开歌曲 / 打开文件夹
 Ctrl+,                     设置
 Enter                      播放选中的歌曲（列表或资源管理器中）
 媒体键                     播放/暂停、上一首、下一首、停止（TLAP 在其他窗口后面时也有效）
 滚轮                       进度条：±5 秒 / 音量滑块：±5% / 列表：滚动
 进度条                     单击或拖动跳转

歌曲列表：单击列标题排序（再次单击为倒序）。播放顺序也会随排序改变。
右键单击列标题可选择显示的列（Title 不能隐藏），Reset Columns 可恢复默认。拖动列标题的边缘可调整宽度。
左侧栏：上方是封面，下方是小型文件夹浏览器。双击文件夹进入，双击歌曲播放（Backspace 返回上一级）。
拖动细分隔条可调整左侧栏和信息面板的宽度。

迷你窗口：Ctrl+M 或双击。在任意位置拖动即可移动，下方可显示频谱。右键单击打开菜单。

均衡器：10 个频段，±12 dB，预设 Flat / Bass / Treble / Vocal / Loudness。前置放大自动调整（不会削波）。

频谱可视化：由 HLSL 像素着色器绘制。按钮用于开 / 关，右键单击按钮可看到着色器列表和 Reload。
想添加自己的着色器，请把 .hlsl 文件放进 TLAP.exe 旁边的 visualizers 文件夹（参见 visualizers\README.txt）。
保存后 1 秒内会自动重新载入。自带的着色器在覆盖安装时会恢复原样，
因此修改时请另存为其他名字。

设置（Ctrl+,）：语言（英语 / 日语 / 简体中文 / 繁体中文）、迷你窗口下方显示频谱、
启动时打开上次的文件夹、启动时总是打开指定文件夹。
音量、静音、重复、均衡器、频谱、列和窗口位置会自动保存。

支持格式：MP3, M4A（AAC / ALAC）, AAC, FLAC, WAV, WMA, OGG（Vorbis / Opus）, OGA, OPUS。歌曲之间无缝衔接（无间隙播放）。
运行环境：Windows 10 / 11（x64）

注意
- TLAP 没有代码签名，Windows SmartScreen 可能会发出警告。
  请选择“更多信息”→“仍要运行”。
- 请自行承担使用风险。
- MIT 许可证。可自由使用。第三方库的许可证：THIRD_PARTY_NOTICES.txt

----------------------------------------
[繁體中文]
----------------------------------------
TLAP 只播放你目前所在資料夾裡的歌曲。沒有媒體庫、播放清單和隨機播放。
開啟一首歌，它所在資料夾的歌曲會排成清單，並從這一首開始播放。

執行方法
- 安裝程式：執行 TLAPSetup-<version>.exe（僅為目前使用者安裝，不需要系統管理員權限）。
  解除安裝請至 Windows 設定 > 應用程式 > 已安裝的應用程式。
- 可攜版：把 TLAP-<version>-portable.zip 解壓縮到任意位置，執行 TLAP\TLAP.exe。
  設定儲存在 exe 旁邊的 TLAP.ini。請讓 visualizers 資料夾保持在 exe 旁邊。
- 用 TLAP 開啟歌曲：在歌曲上按右鍵 > 開啟檔案 > 選擇其他應用程式 > TLAP。
  （僅限安裝程式版。TLAP 不會搶走你的預設應用程式。）
- 也可以先啟動 TLAP，再把歌曲或資料夾拖到視窗上，或按 Ctrl+O。
- TLAP 只會執行一個視窗。再開啟其他歌曲時，會交給正在執行的 TLAP。
- 裝有歌曲的 ZIP 可以直接拖到 TLAP 上播放。

操作
 空白鍵                     播放 / 暫停
 ← →                      上一首 / 下一首（← 在歌曲開頭 3 秒內回到上一首，之後則回到本曲開頭）
 Shift+← →               倒退 / 前進 5 秒
 ↑ ↓                      音量 +5% / -5%
 M                          靜音
 R                          切換重複：Repeat one / Repeat folder / Stop at folder end / Stop after track
 E                          等化器面板
 V                          頻譜視覺化 開 / 關
 I                          資訊面板（選取文字後可用 Ctrl+C 複製）
 C                          封面 顯示 / 隱藏
 Ctrl+M                     迷你視窗 ↔ 一般視窗
 Ctrl+T                     視窗置頂
 Ctrl+O / Ctrl+Shift+O      開啟歌曲 / 開啟資料夾
 Ctrl+,                     設定
 Enter                      播放選取的歌曲（清單或檔案總管中）
 媒體鍵                     播放/暫停、上一首、下一首、停止（TLAP 在其他視窗後面時也有效）
 滾輪                       進度列：±5 秒 / 音量滑桿：±5% / 清單：捲動
 進度列                     按一下或拖曳跳轉

歌曲清單：按一下欄位標題排序（再按一次為倒序）。播放順序也會隨排序改變。
在欄位標題上按右鍵可選擇顯示的欄位（Title 不能隱藏），Reset Columns 可恢復預設。拖曳標題邊緣可調整寬度。
左側欄：上方是封面，下方是小型資料夾瀏覽器。按兩下資料夾進入，按兩下歌曲播放（Backspace 回上一層）。
拖曳細分隔條可調整左側欄與資訊面板的寬度。

迷你視窗：Ctrl+M 或按兩下。在任意位置拖曳即可移動，下方可顯示頻譜。按右鍵開啟選單。

等化器：10 個頻段，±12 dB，預設 Flat / Bass / Treble / Vocal / Loudness。前級放大自動調整（不會削波）。

頻譜視覺化：由 HLSL 像素著色器繪製。按鈕用於開 / 關，在按鈕上按右鍵可看到著色器清單與 Reload。
想加入自己的著色器，請把 .hlsl 檔放進 TLAP.exe 旁邊的 visualizers 資料夾（請見 visualizers\README.txt）。
儲存後 1 秒內會自動重新載入。內建的著色器在覆蓋安裝時會恢復原樣，
因此修改時請另存成其他名稱。

設定（Ctrl+,）：語言（英文 / 日文 / 簡體中文 / 繁體中文）、迷你視窗下方顯示頻譜、
啟動時開啟上次的資料夾、啟動時一律開啟指定資料夾。
音量、靜音、重複、等化器、頻譜、欄位與視窗位置會自動儲存。

支援格式：MP3, M4A（AAC / ALAC）, AAC, FLAC, WAV, WMA, OGG（Vorbis / Opus）, OGA, OPUS。歌曲之間無縫銜接（無間隙播放）。
執行環境：Windows 10 / 11（x64）

注意
- TLAP 沒有程式碼簽章，Windows SmartScreen 可能會顯示警告。
  請選擇「其他資訊」→「仍要執行」。
- 使用風險請自行承擔。
- MIT 授權。可自由使用。第三方函式庫的授權：THIRD_PARTY_NOTICES.txt
