#pragma once
#include "core/PackJob.h"
#include <QWidget>

namespace MI {

// 各功能的操作流程（對應主畫面按鈕）
namespace Flows {

void createModpack(QWidget *parent);   // 2. 建立模組包
void requestModpack(QWidget *parent);  // 3. 請求模組包檔案
void updateMods(QWidget *parent);      // 1. 更新模組
void editPak(QWidget *parent);         // 6. 更新 pak

// 顯示進度並執行打包工作；成功回傳 true（失敗時自行顯示錯誤）
bool runPackJob(QWidget *parent, PackJob::Mode mode, const PackTarget &target, const QString &name, const QString &output,
                const QList<JobMod> &mods, const QString &title);

// 輸出完成後的提示（含開啟資料夾）
void showDone(QWidget *parent, const QString &title, const QString &path);

} // namespace Flows
} // namespace MI
