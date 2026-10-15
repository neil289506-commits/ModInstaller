// 本機測試：用記憶體模擬 D1，驗證註冊 / 重複 / 查詢邏輯
import worker from './src/index.js';
const rows = new Map();
const DB = { prepare(sql) { return { _a: [], bind(...a) { this._a = a; return this; },
  async run() { if (sql.startsWith('INSERT')) { if (rows.has(this._a[0])) throw new Error('UNIQUE constraint failed: codes.code'); rows.set(this._a[0], this._a[1]); } },
  async first() { return rows.has(this._a[0]) ? { ok: 1 } : null; } }; } };
const call = (method, path, body) => worker.fetch(new Request('https://x.test' + path, { method, body: body && JSON.stringify(body) }), { DB });
let fails = 0; const check = (name, cond) => { console.log((cond ? 'ok: ' : 'FAIL: ') + name); if (!cond) fails++; };
check('register 201', (await call('POST', '/api/register', { code: 'ABCDEFGHJKLM' })).status === 201);
check('duplicate 409', (await call('POST', '/api/register', { code: 'ABCDEFGHJKLM' })).status === 409);
check('lowercase normalised → 409', (await call('POST', '/api/register', { code: 'abcdefghjklm' })).status === 409);
check('bad length 400', (await call('POST', '/api/register', { code: 'ABC' })).status === 400);
check('bad chars 400', (await call('POST', '/api/register', { code: 'ABCDEFGHJK!M' })).status === 400);
check('bad json 400', (await worker.fetch(new Request('https://x.test/api/register', { method: 'POST', body: 'zzz' }), { DB })).status === 400);
check('exists 200', (await call('GET', '/api/codes/ABCDEFGHJKLM')).status === 200);
check('missing 404', (await call('GET', '/api/codes/ZZZZZZZZZZZZ')).status === 404);
check('unknown route 404', (await call('GET', '/nope')).status === 404);
const concurrent = await Promise.all(Array.from({ length: 20 }, () => call('POST', '/api/register', { code: 'QWERTYUIPASD' })));
check('concurrent: exactly one 201', concurrent.filter(r => r.status === 201).length === 1);
process.exit(fails ? 1 : 0);
