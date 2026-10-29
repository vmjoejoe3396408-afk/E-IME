# E-IME 執行檔暫存目錄

此目錄由 CMake 的 `Runtime` 元件產生，用於驗證產品與開發工具的分離；不是已簽署或完整驗收的正式安裝包。

需要 Windows 10／11 x64 與 PowerShell 7。先保留並解壓完整目錄，再執行：

```powershell
pwsh -File .\scripts\install.ps1 -SourceDll .\eime_tip.dll
```

也可使用根目錄的「安裝或重新註冊 E-IME.cmd」。安裝器需要時會要求管理員權限；安裝後重新開啟宿主，以 Win+Space 選擇 E-IME。詞庫仍指向此目錄的 `data/`，請保留它。

此目錄不含自訂詞、學習日誌、快取、測試或分析工具。停用與解除註冊分別執行 `scripts/disable.ps1`、`scripts/uninstall.ps1`。

完整原始碼、第三方資料來源及建置方式見 [E-IME GitHub](https://github.com/vmjoejoe3396408-afk/E-IME)。對外分發前仍須提供與 DLL 相符的原始碼，完成宿主驗收及編譯器執行庫分發聲明檢查；請保留 LICENSE、LICENSES 與 THIRD_PARTY_NOTICES.md。
