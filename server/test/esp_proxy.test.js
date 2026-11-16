// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// The /esp/ proxy, through a real HTTP server in front of a fake controller.

import assert from 'node:assert/strict';
import { once } from 'node:events';
import http from 'node:http';
import { after, before, test } from 'node:test';

import { createEspProxy, MAX_BODY_BYTES } from '../src/esp_proxy.js';
import { createHttpServer } from '../src/http.js';
import { LogStore } from '../src/log_store.js';

let device;
let deviceUrl;
let seen = [];
let hang = false;

async function listen(s) {
  s.listen(0, '127.0.0.1');
  await once(s, 'listening');
  return `http://127.0.0.1:${s.address().port}`;
}

async function serve(espUrl, options) {
  const s = createHttpServer(new LogStore(':memory:'), { espProxy: createEspProxy(espUrl, options) });
  return { s, url: await listen(s) };
}

let front;
let base;

before(async () => {
  device = http.createServer(async (req, res) => {
    const chunks = [];
    for await (const c of req) chunks.push(c);
    seen.push({ method: req.method, url: req.url, headers: req.headers, body: Buffer.concat(chunks).toString() });
    if (hang) return;
    if (req.url === '/api/zones' && req.method === 'PUT') {
      res.writeHead(413, { 'Content-Type': 'application/json' });
      res.end('{"ok":false,"error":"too_large"}');
      return;
    }
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ ok: true, path: req.url }));
  });
  deviceUrl = await listen(device);
  ({ s: front, url: base } = await serve(`${deviceUrl}/`));
});

after(() => {
  front.close();
  device.closeAllConnections();
  device.close();
});

test('GET is forwarded with its query, minus the /esp prefix', async () => {
  seen = [];
  const res = await fetch(`${base}/esp/api/logs?level=info`, { headers: { Cookie: 'CF_Authorization=x', Origin: 'https://example.com' } });
  assert.equal(res.status, 200);
  assert.equal(res.headers.get('cache-control'), 'no-store');
  assert.deepEqual(await res.json(), { ok: true, path: '/api/logs?level=info' });
  assert.equal(seen[0].url, '/api/logs?level=info');
  assert.equal(seen[0].headers.cookie, undefined);
  assert.equal(seen[0].headers.origin, undefined);
});

test('writes need the X-iSprinklr header', async () => {
  seen = [];
  const bare = await fetch(`${base}/esp/api/zone/stop`, { method: 'POST', body: '{}' });
  assert.equal(bare.status, 403);
  assert.equal(seen.length, 0);
  const ok = await fetch(`${base}/esp/api/zone/start`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', 'X-iSprinklr': '1' },
    body: '{"zone":3,"duration_minutes":5}',
  });
  assert.equal(ok.status, 200);
  assert.equal(seen[0].method, 'POST');
  assert.equal(seen[0].body, '{"zone":3,"duration_minutes":5}');
  assert.equal(seen[0].headers['content-type'], 'application/json');
});

test('the device status and error body come back unchanged', async () => {
  const res = await fetch(`${base}/esp/api/zones`, {
    method: 'PUT', headers: { 'Content-Type': 'application/json', 'X-iSprinklr': '1' }, body: '{"zones":[]}',
  });
  assert.equal(res.status, 413);
  assert.deepEqual(await res.json(), { ok: false, error: 'too_large' });
});

test('refuses firmware upload, other paths and other methods', async () => {
  seen = [];
  const h = { 'X-iSprinklr': '1' };
  assert.equal((await fetch(`${base}/esp/api/update`, { method: 'POST', headers: h, body: 'x' })).status, 404);
  assert.equal((await fetch(`${base}/esp/index.html`)).status, 404);
  assert.equal((await fetch(`${base}/esp`)).status, 404);
  assert.equal((await fetch(`${base}/esp/api/status`, { method: 'DELETE', headers: h })).status, 405);
  assert.equal((await fetch(`${base}/esp/api/status`, { method: 'OPTIONS' })).status, 405);
  assert.equal(seen.length, 0);
});

test('large bodies are refused before reaching the device', async () => {
  seen = [];
  const res = await fetch(`${base}/esp/api/schedules`, {
    method: 'PUT', headers: { 'X-iSprinklr': '1' }, body: 'x'.repeat(MAX_BODY_BYTES + 1),
  });
  assert.equal(res.status, 413);
  assert.equal(seen.length, 0);
});

test('an unreachable or slow device is a 502 or 504', async () => {
  const gone = await serve('http://127.0.0.1:1');
  try {
    const res = await fetch(`${gone.url}/esp/api/status`);
    assert.equal(res.status, 502);
    assert.equal((await res.json()).error, 'controller_unreachable');
  } finally {
    gone.s.close();
  }
  const slow = await serve(deviceUrl, { timeoutMs: 100 });
  hang = true;
  try {
    const res = await fetch(`${slow.url}/esp/api/status`);
    assert.equal(res.status, 504);
    assert.equal((await res.json()).error, 'controller_timeout');
  } finally {
    hang = false;
    slow.s.close();
  }
});

test('without a device address, says so', async () => {
  const none = await serve('');
  try {
    const res = await fetch(`${none.url}/esp/api/status`);
    assert.equal(res.status, 503);
    assert.equal((await res.json()).error, 'controller_not_configured');
  } finally {
    none.s.close();
  }
});
