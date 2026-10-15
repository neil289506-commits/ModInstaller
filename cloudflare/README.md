# 裝置代碼註冊服務（Cloudflare Workers + D1）

ModInstaller 每台電腦第一次開啟時會產生一組 12 碼英數代碼，並向這個服務註冊以確保**全球不重複**。
註冊時只會儲存代碼與建立時間，不收集任何其他資料。

## 部署（約 3 分鐘）
```bash
cd cloudflare
npx wrangler login
npx wrangler d1 create modinstaller-codes          # 把輸出的 database_id 填進 wrangler.toml
npx wrangler d1 execute modinstaller-codes --remote --file=schema.sql
npx wrangler deploy
```
部署完成後會得到網址，例如 `https://modinstaller-codes.<你的帳號>.workers.dev`，
到 ModInstaller 的「設定」→「Cloudflare 註冊服務網址」貼上即可。

## API
| 方法 | 路徑 | 說明 |
|---|---|---|
| POST | `/api/register` `{"code":"…"}` | 201 成功 / 409 已存在（客戶端會自動換新代碼重試）/ 400 格式錯誤 |
| GET | `/api/codes/:code` | 200 存在 / 404 不存在（傳送前用來確認對方代碼） |

## 建議
在 Cloudflare 後台為 `/api/register` 加一條 **Rate limiting rule**（例如同一 IP 每分鐘 10 次），避免被大量註冊。
