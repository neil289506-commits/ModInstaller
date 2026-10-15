// SPDX-License-Identifier: GPL-3.0-only
// ModInstaller 裝置代碼註冊服務（Cloudflare Workers + D1）
//
// POST /api/register   {"code":"ABCDEFGHJKLM"}  → 201 註冊成功 / 409 代碼已存在 / 400 格式錯誤
// GET  /api/codes/:code                          → 200 存在 / 404 不存在
//
// 唯一性由 D1 的 PRIMARY KEY 保證（單一 INSERT 是原子操作），不會因為同時註冊而重複。

const CODE_RE = /^[A-Z0-9]{12}$/;
const HEADERS = {
  'content-type': 'application/json; charset=utf-8',
  'cache-control': 'no-store',
};

function json(body, status = 200) {
  return new Response(JSON.stringify(body), { status, headers: HEADERS });
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const path = url.pathname.replace(/\/+$/, '');

    if (request.method === 'POST' && path === '/api/register') {
      let body;
      try {
        body = await request.json();
      } catch {
        return json({ error: 'invalid json' }, 400);
      }
      const code = typeof body?.code === 'string' ? body.code.toUpperCase() : '';
      if (!CODE_RE.test(code)) return json({ error: 'invalid code' }, 400);
      try {
        await env.DB.prepare('INSERT INTO codes (code, created_at) VALUES (?1, ?2)')
          .bind(code, Math.floor(Date.now() / 1000))
          .run();
        return json({ ok: true }, 201);
      } catch (e) {
        if (String(e && e.message).includes('UNIQUE')) return json({ error: 'exists' }, 409);
        return json({ error: 'server error' }, 500);
      }
    }

    const m = path.match(/^\/api\/codes\/([A-Za-z0-9]{12})$/);
    if (request.method === 'GET' && m) {
      const row = await env.DB.prepare('SELECT 1 AS ok FROM codes WHERE code = ?1').bind(m[1].toUpperCase()).first();
      return row ? json({ exists: true }) : json({ exists: false }, 404);
    }

    return json({ error: 'not found' }, 404);
  },
};
