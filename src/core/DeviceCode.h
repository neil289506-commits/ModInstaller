// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QString>

namespace MI {

// 每台電腦第一次開啟時產生的 12 碼英數代碼（用於模組包傳輸）。
// 代碼是隨機產生的，不包含任何硬體資訊；會同時存放在使用者層級的兩個位置（登錄檔與檔案），
// 任何一處遺失都會由另一處還原，所以重新啟動後代碼不會改變。
class DeviceCode
{
public:
    static constexpr int Length = 12;

    struct Record {
        QString code;
        bool registered = false; // 是否已向雲端註冊（確認不重複）
    };

    // 讀取已儲存的代碼；沒有則回傳空 code
    static Record load();
    // 寫入兩處儲存位置
    static void save(const Record &r);
    // 產生新的隨機代碼（大寫英文 + 數字，去掉容易混淆的 0 O 1 I）
    static QString generate();
    static bool isValid(const QString &code);
    // 轉大寫並移除空白與連字號
    static QString normalize(const QString &input);
    // 檔案儲存位置（顯示給使用者看）
    static QString filePath();
};

} // namespace MI
