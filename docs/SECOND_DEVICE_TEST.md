# 在另一台 Windows 裝置測試私人預覽版

這次先驗證從乾淨 GitHub checkout 建置及使用 E-IME。適用於 Windows 10／11 x64；請用擁有此私人倉庫存取權的 GitHub 帳號登入 Git。

## 下載與建置

需要 Git、PowerShell 7、CMake 3.20+，以及 MinGW-w64 GCC 13+ 與 Ninja，或 Visual Studio 2022 的「使用 C++ 的桌面開發」工具組。編譯工具須能從終端機找到；詳細環境說明見 [README](../README.md#31-前置環境要求)。

在 PowerShell 7 執行：

```powershell
git clone https://github.com/vmjoejoe3396408-afk/E-IME.git
cd E-IME
git rev-parse --short HEAD
```

首次存取私人倉庫時，Git Credential Manager 可能會請你在瀏覽器登入 GitHub。請記下上面輸出的 commit，方便比對測試版本。

MinGW 路線：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build -C Release --output-on-failure
```

或選擇 MSVC 路線：

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

預期 CTest 為 **5 / 5 組通過**。一般建置使用倉庫內詞庫，不需要另行下載或產生資料。若建置失敗，保留完整錯誤及編譯器版本。

## 安裝與試打

先儲存工作並關閉載入舊 E-IME 的應用程式。MinGW 建置完成後執行：

```powershell
pwsh -File .\scripts\install.ps1
```

若採 MSVC，改為：

```powershell
pwsh -File .\scripts\install.ps1 -SourceDll .\build-msvc\Release\eime_tip.dll
```

允許安裝器要求的管理員權限，再開新的記事本，以 `Win+Space` 選取「E-IME (改進型注音)」。若清單沒有更新，登出再登入後重試。請保留 checkout 目錄；安裝器的詞庫路徑指向其中的 `data/`。

依序試打，記錄預期與實際結果：

| 案例 | 操作與預期 |
| --- | --- |
| 基本注音 | 中文模式輸入 `su3cl3`，按 Enter，應得到「你好」 |
| 重新選字 | 輸入一個字或整詞，按 ↓ 選候選後再次按 ↓，應能重新選擇；最後 Enter 才提交組字 |
| 標點 | 輸入「你好」，按 Ctrl+,，逗號應留在組字區；按住 Ctrl 連按逗號也應逐個新增 |
| 句中編輯 | 未提交「你好」時按 ←，在中間插入 Ctrl+,，應呈現「你，好」，游標緊隨逗號 |
| 小鍵盤 | 組字中使用數字小鍵盤，確認數字插入游標位置，組字及底線沒有提前結束 |
| 中英與繁簡 | 單按 Shift 切換中英；Ctrl+Shift+F 切換輸出字形，試打「台積電」／「台积电」 |
| 多個程式 | 同時開啟兩個記事本及平常使用的編輯器，各自試打及選字，確認不卡住、不丟字 |
| 重啟 | 關閉並重新開啟記事本，確認輸入法仍可載入 |

換機測試時也請看候選窗位置、底線及配色是否正常。自動化測試使用 mock 宿主，這些桌面觀察能補足自動化覆蓋。

## 回報與移除

回報時提供 Windows 版本、commit、編譯器、出問題的程式、實際按鍵、預期結果及實際結果；若涉及崩潰，也提供事件檢視器的錯誤模組與例外代碼。

需要暫時停用後續載入時執行 `pwsh -File .\scripts\disable.ps1`，並關閉已載入 DLL 的程式。完整反註冊：

```powershell
pwsh -File .\scripts\uninstall.ps1
```

GitHub 用來傳送原始碼與修正，`data/sync/*.delta` 不會經由 Git 同步到另一台裝置。若之後要測個人學習漫遊，再另行設定私人雲端同步資料夾。
