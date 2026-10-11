<div align="center">

# ModInstaller

**建立、分享與更新 Minecraft 模組包** — 搜尋 Modrinth / CurseForge、自動補齊依賴、一鍵輸出 zip 或超輕量的 `.modpak`

![Qt6](https://img.shields.io/badge/Qt-6-41cd52?logo=qt&logoColor=white)
![Platform](https://img.shields.io/badge/platform-Windows-0078d6)
![License](https://img.shields.io/badge/license-GPL--3.0-blue)

![主畫面](docs/screenshots/01-home.png)

</div>

> 以下截圖使用示意資料。

## 為什麼用 ModInstaller？
- **不用啟動器**：只想整理模組清單，不需要建立遊戲實例
- **自動補齊依賴**：搜索邏輯移植自 PrismLauncher，連 Fabric API ↔ Quilted Fabric API 都會自動轉換
- **`.modpak` 輕量分享**：只存模組清單與 sha256，檔案只有幾 KB，收到的人再「請求模組包檔案」就能還原成 zip
- **雙平台來源**：Modrinth 與 CurseForge 一起搜尋
- **繁體中文 / English**

## 使用流程

### 1. 選擇載入器與版本
Fabric / Quilt / Forge / NeoForge 與遊戲版本，版本資料直接從官方來源下載，不需要 Python。

![選擇載入器與版本](docs/screenshots/02-loader.png)

### 2. 搜尋並選取模組
左側是搜尋結果，右側是詳細資訊與版本選擇，底部是已選取清單。

![搜尋並選取模組](docs/screenshots/03-browser.png)

### 3. 確認依賴
自動搜索到的必要依賴會列出並標示「被誰需要」，可自行取消勾選。

![確認依賴](docs/screenshots/04-dependencies.png)

### 4. 輸出
選擇 **zip**（下載全部模組並壓縮）或 **modpak**（只存資訊），名稱與儲存位置都由你決定。

![輸出模組包](docs/screenshots/05-export.png)

## 功能
| 入口 | 說明 |
|---|---|
| 更新模組 | 選擇 `.modpak`，向各模組登記的託管平台請求更新，有更新就提示並覆寫（覆寫前備份為 `.bak`） |
| 建立模組包 | 選載入器 + 遊戲版本 + 載入器版本 → 搜尋模組（Modrinth / CurseForge）→ 自動搜索依賴 → 確認 → 輸出 zip 或 modpak |
| 請求模組包檔案 | 提供 `.modpak`，依 `mod.info` 下載、驗證 sha256，打包成 `<檔名>.zip` |
| 設定 | CurseForge API Key、語言切換（繁體中文 / English） |
| GitHub Repo | 開啟本專案頁面 |
| 更新 pak | 開啟既有 `.modpak`，新增 / 移除模組（流程同「建立模組包」），再另存或覆寫 |

所有輸出檔案都會讓使用者輸入名稱與儲存位置。

## .modpak 格式
`.modpak` 是 zip，內含兩個 JSON 檔：
- `version.info`：名稱、Minecraft 版本、載入器與版本、每個模組的託管平台
- `mod.info`：每個模組的下載地址、版本號、版本 ID、檔名、sha256

zip 輸出格式：所有模組放在 `mods/` 資料夾內。

## 版本資料
不需要 Python。程式用 Qt 網路直接下載（與原 `version.py` 相同的來源）並解析到
`%LOCALAPPDATA%/ModInstaller/ModInstaller/version/`：Mojang、Fabric、Quilt、Forge（maven + promotions）、NeoForge、OptiFine。

## 建置（Windows / MSVC 2022 / Qt 6）
```bat
vcpkg install quazip:x64-windows
cmake -S . -B build -G "Ninja" -DCMAKE_PREFIX_PATH=<Qt6 路徑> ^
      -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
windeployqt build\ModInstaller.exe
```
需要 Qt 6.4 以上（Widgets、Network、Svg、LinguistTools）。

## 開發用環境變數
`MODINSTALLER_MODRINTH_API` / `MODINSTALLER_CURSEFORGE_API` 可把 API 指向模擬伺服器（測試用）。

## 授權
GPL-3.0，見 `LICENSE`。
