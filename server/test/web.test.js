// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// Static hosting and the backup endpoints, through a real HTTP server on an
// ephemeral port with temporary directories.

import assert from 'node:assert/strict';
import { once } from 'node:events';
import { mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { after, before, test } from 'node:test';

import { writeBackup } from '../src/backup.js';
import { createHttpServer } from '../src/http.js';
import { LogStore } from '../src/log_store.js';
import { createStaticHandler, resolveUnder } from '../src/static_files.js';

let tmp;
let base;
let server;

async function serve(options) {
  const s = createHttpServer(new LogStore(':memory:'), options);
  s.listen(0, '127.0.0.1');
  await once(s, 'listening');
  return { s, url: `http://127.0.0.1:${s.address().port}` };
}

before(async () => {
  tmp = await mkdtemp(join(tmpdir(), 'isprinklr-web-'));
  const web = join(tmp, 'dist');
  await mkdir(join(web, 'assets'), { recursive: true });
  await writeFile(join(web, 'index.html'), '<!doctype html><title>app</title>');
  await writeFile(join(web, 'assets', 'index-abc123.js'), 'console.log(1)');
  await writeFile(join(web, 'favicon.svg'), '<svg/>');
  await writeFile(join(tmp, 'secret.txt'), 'outside the root');
  const backups = join(tmp, 'backups');
  await writeBackup(backups, { settings_hash: 'aaaa', restore: [] }, new Date('2026-11-14T11:45:00Z'));
  await writeBackup(backups, { settings_hash: 'bbbb', restore: [] }, new Date('2026-11-15T11:45:00Z'));
  ({ s: server, url: base } = await serve({
    staticHandler: createStaticHandler(web),
    backupsDir: backups,
    backupStats: () => ({ last_timestamp: '2026-11-15T11-45-00Z' }),
  }));
});

after(async () => {
  server.close();
  await rm(tmp, { recursive: true, force: true });
});

test('serves index.html at the root, revalidated each load', async () => {
  const res = await fetch(`${base}/`);
  assert.equal(res.status, 200);
  assert.match(res.headers.get('content-type'), /^text\/html/);
  assert.equal(res.headers.get('cache-control'), 'no-cache');
  assert.equal(await res.text(), '<!doctype html><title>app</title>');
});

test('hashed assets are cached long, with the right type', async () => {
  const res = await fetch(`${base}/assets/index-abc123.js`);
  assert.equal(res.status, 200);
  assert.match(res.headers.get('content-type'), /^text\/javascript/);
  assert.match(res.headers.get('cache-control'), /immutable/);
  assert.equal(await res.text(), 'console.log(1)');
  const svg = await fetch(`${base}/favicon.svg`);
  assert.equal(svg.headers.get('content-type'), 'image/svg+xml');
  assert.equal(svg.headers.get('cache-control'), 'no-cache');
});

test('client-side routes fall back to index.html; missing files do not', async () => {
  const route = await fetch(`${base}/schedules/edit`);
  assert.equal(route.status, 200);
  assert.equal(await route.text(), '<!doctype html><title>app</title>');
  const missing = await fetch(`${base}/assets/gone-123.js`);
  assert.equal(missing.status, 404);
});

test('never serves outside the root', async () => {
  for (const p of ['/../secret.txt', '/%2e%2e/secret.txt', '/assets/%2e%2e/%2e%2e/secret.txt',
    '/..%2fsecret.txt', '/%00']) {
    const res = await fetch(`${base}${p}`);
    assert.notEqual(await res.text(), 'outside the root', p);
  }
  assert.equal(resolveUnder('/srv/web', '/../secret.txt'), null);
  assert.equal(resolveUnder('/srv/web', '/..%2fsecret.txt'), null);
  assert.equal(resolveUnder('/srv/web', '/%E0%A4%A'), null);
  assert.equal(resolveUnder('/srv/web', '/a/b.js'), '/srv/web/a/b.js');
});

test('HEAD works for static files; other methods are refused', async () => {
  const head = await fetch(`${base}/`, { method: 'HEAD' });
  assert.equal(head.status, 200);
  assert.equal(await head.text(), '');
  assert.equal((await fetch(`${base}/`, { method: 'POST' })).status, 405);
  assert.equal((await fetch(`${base}/api/logs`, { method: 'HEAD' })).status, 405);
});

test('unknown /api paths are JSON 404s, not the frontend', async () => {
  const res = await fetch(`${base}/api/nope`);
  assert.equal(res.status, 404);
  assert.deepEqual(await res.json(), { ok: false, error: 'not_found' });
});

test('says so when the frontend has not been built', async () => {
  const { s, url } = await serve({ staticHandler: createStaticHandler(join(tmp, 'nowhere')) });
  try {
    const res = await fetch(`${url}/`);
    assert.equal(res.status, 200);
    assert.match(await res.text(), /not been built/);
    assert.equal((await fetch(`${url}/x.js`)).status, 404);
    assert.equal((await fetch(`${url}/api/health`)).status, 200);
  } finally {
    s.close();
  }
});

test('GET /api/backups lists newest first', async () => {
  const body = await (await fetch(`${base}/api/backups`)).json();
  assert.equal(body.ok, true);
  assert.deepEqual(body.backups.map((b) => [b.timestamp, b.settings_hash]), [
    ['2026-11-15T11-45-00Z', 'bbbb'],
    ['2026-11-14T11-45-00Z', 'aaaa'],
  ]);
  assert.equal(body.backups[0].taken_at, '2026-11-15T11:45:00.000Z');
});

test('GET /api/backups/:timestamp and /latest', async () => {
  const one = await (await fetch(`${base}/api/backups/2026-11-14T11-45-00Z`)).json();
  assert.equal(one.backup.settings_hash, 'aaaa');
  const latest = await (await fetch(`${base}/api/backups/latest`)).json();
  assert.equal(latest.timestamp, '2026-11-15T11-45-00Z');
  assert.equal(latest.backup.settings_hash, 'bbbb');
  assert.equal((await fetch(`${base}/api/backups/2026-01-01T00-00-00Z`)).status, 404);
  assert.equal((await fetch(`${base}/api/backups/..%2f..%2fsecret`)).status, 404);
});

test('/api/health reports the last backup', async () => {
  const body = await (await fetch(`${base}/api/health`)).json();
  assert.equal(body.backup.last_timestamp, '2026-11-15T11-45-00Z');
});
