CREATE TABLE IF NOT EXISTS codes (
  code       TEXT PRIMARY KEY,   -- 12 碼英數代碼（大寫）
  created_at INTEGER NOT NULL    -- Unix 時間（秒）
);
