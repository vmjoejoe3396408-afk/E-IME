# 安裝、更新與另一台裝置測試

適用：主線 `master`（已納入功能分支）。本指南用乾淨 checkout 驗證建置與使用；合併前差異見 [文件索引](README.md)。目前提供原始碼，沒有可直接安裝的二進位套件。

## 準備環境

| 必要項目 | 用途 |
| --- | --- |
| Windows 10／11 x64 | 目前的系統整合目標；不含 32 位元應用模組 |
| Git | 下載公開原始碼；一般 clone 不需登入 GitHub |
| PowerShell 7（`pwsh`） | 執行以下命令與部署腳本 |
| CMake 3.20+ | 配置、建置及 CTest |
| Visual Studio 2022 C++ 桌面工具組，或 MinGW-w64 GCC 13+ 與 Ninja | 二選一的編譯工具鏈；MinGW 的 g++ 與 Ninja 須在 PATH |

Python 3.10+ 僅在重新產生或核對第三方資料時需要，一般建置不用安裝。

## 取得正確分支

在 PowerShell 7 執行：

```powershell
git clone --branch master https://github.com/vmjoejoe3396408-afk/E-IME.git
cd E-IME
git branch --show-current
git rev-parse --short HEAD
```

記下分支與 commit；預設分支為 `master`，已包含四欄選字等新增功能。

## 編譯與自動測試

MSVC：

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

MinGW：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build -C Release --output-on-failure
```

每個步驟成功後再繼續。兩種編譯器使用不同建置目錄；已有目錄若曾配置為另一種工具鏈，請換一個新的建置目錄。

預期 CTest **5／5 組通過**，其中核心套件含 **15 個模組**。測試每次使用新的資料副本；失敗時 CTest 會顯示證據目錄。一般建置使用已提交的詞庫，不需重新下載。

## 安裝與啟用

先儲存工作並關閉載入舊 E-IME 的應用程式。依採用的建置方式，執行其中一個命令：

```powershell
# MSVC
pwsh -File .\scripts\install.ps1 -SourceDll .\build-msvc\Release\eime_tip.dll

# MinGW
pwsh -File .\scripts\install.ps1 -SourceDll .\build\eime_tip.dll
```

允許安裝器要求的管理員權限。安裝器會嘗試把 TIP 加入既有繁體中文語言項目；這是盡力處理，並非安裝成功的唯一判斷。若出現語言清單警告，或 `Win+Space` 沒有 E-IME，請在 Windows 語言與鍵盤設定確認繁體中文及 E-IME 鍵盤項目，再登出／登入後重試。

開啟新的記事本、切到 E-IME，中文模式下輸入 `su3cl3` 再按 Enter，確認得到「你好」。**只看到輸入法名稱或 DLL 註冊成功，不能代表詞庫及實際組字正常。**

安裝器會輸出安裝路徑與 DLL 的 SHA-256。DLL 複製至本機版本目錄，但詞庫仍指向 checkout 的 `data/`，請保留該目錄。

也可雙擊專案根目錄的 `安裝或重新註冊 E-IME.cmd`，由它呼叫相同的安裝器；需要 PowerShell 7，並會在必要時要求管理員權限。它只安裝或重新註冊既有 DLL，不會下載或編譯更新。若使用 `build-msvc/` 或其他自訂建置位置，請優先使用上方明確指定 `-SourceDll` 的命令，以免選到其他建置輸出。

## 更新已存在的 checkout

先確認自己的修改已儲存或提交，再切換主線並更新：

```powershell
git status --short --branch
git switch master
git pull --ff-only
```

重新執行原工具鏈的建置、CTest 與指定 DLL 安裝命令，最後重新開啟宿主。Git 更新不會替已開啟的程式換 DLL。

已有機器從舊日誌版本升級時，先關閉舊版宿主並備份 `data/sync/`；必要時登出再登入。新版可讀舊日誌，但不要讓舊版持續共寫，也不要手動更名或複製 `.delta`。

## 實機檢查清單

操作細節見 [使用指南](USAGE.md)。請用實際按鍵輸入，不要只貼上預期中文字。

| 案例 | 操作與預期 |
| --- | --- |
| 基本輸入 | `su3cl3`＋Enter 得到「你好」 |
| 提早聲調 | `24l` 嘗試組成「到」或「倒」；`h4ji3` 應為「次我」，避免錯吸收為「促」 |
| 重新選字 | 選候選後仍有組字底線，再按 ↓ 可重選；最後 Enter 提交整句 |
| 四欄候選 | 輸入 `u`＋Space，再按 ↓／→；確認展開、切欄、欄內上下移動、數字選目前欄及 PgUp／PgDn |
| 標點 | 分別測 Ctrl+,、Shift+,、反引號後接逗號，以及 Ctrl+;／Ctrl+Shift+; |
| 句中插入 | 未提交「你好」時按 ←，再 Ctrl+,，應為「你，好」，游標緊隨逗號 |
| 修飾鍵 | 按住左右 Ctrl 分別連打逗號，再放開；確認之後的一般注音鍵沒有被當成 Ctrl 組合 |
| 小鍵盤／英數 | 在組字中及游標中間插入數字、等號；底線保留，Enter 提交順序正確 |
| 繁簡 | Ctrl+Shift+F 切換；右 Ctrl＋右 Shift＋S／T 指定模式，以「體／体」確認輸出並觀察短暫提示 |
| 多宿主／重啟 | 分別在兩個記事本、日常瀏覽器及編輯器試打，關閉重開後仍可使用 |
| 畫面 | 候選字 24px、列高 38px、直角窗框、實線底線；檢查多欄在螢幕邊界與不同縮放下是否被裁切 |

這些是待執行的實機案例，不代表每個宿主都已通過。學習順位可能因個人紀錄而不同；以功能和提交結果為主記錄。

## 常見問題

| 現象 | 檢查方式 |
| --- | --- |
| 找不到編譯器或 Ninja | 確認已安裝工具並加入 PATH；MSVC 路線不需要 Ninja |
| 切換分支後仍是舊 UI | 重新建置，安裝時明確指定該建置目錄的 DLL，關閉已載入舊版的宿主再開 |
| 只有注音符號、無法組字 | 先檢查資料目錄及 `core_dict.txt`，並確認安裝輸出的版本路徑；目前版本可能以空詞庫降級初始化，COM 成功不代表資料載入成功 |
| 版本已屬於另一個資料目錄 | 同一 DLL 雜湊已綁定不同 checkout；回原安裝資料夾處理，不要直接覆寫 `data-root.txt` 保護 |
| 沒有 `[繁]`／`[簡]` | 這是目前設計，改以切換提示及實際輸出判斷模式 |
| 部分按鍵沒有反應但也沒響聲 | 目前版本預設靜音；記錄實際按鍵及前一音節，區分拒絕輸入與漏鍵 |

## 停用、移除與回報

暫停後續宿主載入：

```powershell
pwsh -File .\scripts\disable.ps1
```

完整反註冊：

```powershell
pwsh -File .\scripts\uninstall.ps1
```

已載入 DLL 的程式仍需關閉。停用後若要重新啟用，執行對應的安裝命令。解除註冊不會自動清除版本目錄、詞庫或個人日誌。

回報請包含 Windows 版本、分支、commit、編譯器、宿主程式、實際按鍵、預期／實際結果。崩潰時附事件檢視器的錯誤模組及例外代碼；更多證據格式見 [測試索引](../testbench/README.md)。

GitHub 傳送的是原始碼；個人學習日誌不經 Git 同步。跨機漫遊需另行設定私人雲端資料夾，與本次換機建置測試分開驗證。
