// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import assert from 'node:assert/strict';
import { mkdtemp, readdir, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';

import {
  backupOnce, fetchSettings, isTimestamp, listBackups, readBackup,
  scheduleBackups, timestampFor,
} from '../src/backup.js';

// Shapes copied from the device on 2026-11-15 (firmware 0.6.0-phase5),
// trimmed.
function device({ hashes = ['78a2614c'], fail = null } = {}) {
  let call = 0;
  const seen = [];
  const fetchImpl = async (url, init) => {
    const path = new URL(url).pathname;
    seen.push(path);
    assert.ok(init.signal, 'every request has a timeout');
    if (fail && fail(path)) return new Response('nope', { status: 503 });
    const hash = hashes[Math.min(call++, hashes.length - 1)];
    const bodies = {
      '/api/zones': { ok: true, settings_hash: hash,
        zones: [{ zone: 1, name: 'Walkway Beds' }, { zone: 2, name: 'NE Beds' }] },
      '/api/schedules': { ok: true, settings_hash: hash,
        schedules: [{ name: 'Late Fall', items: [{ zone: 1, days: 'TH', duration_minutes: 20 }] }],
        active: 'Late Fall', enabled: false },
      '/api/config': { ok: true, settings_hash: hash, start_hour: 7, start_minute: 0,
        timezone: 'PST8PDT,M3.2.0,M11.1.0', log_host: '', log_port: 5514 },
      '/api/status': { firmware_version: '0.6.0-phase5', settings_hash: hash },
    };
    return Response.json(bodies[path]);
  };
  return { fetchImpl, seen };
}

test('timestamps are file- and URL-safe and sort as text', () => {
  assert.equal(timestampFor(new Date('2026-11-16T11:45:00.123Z')), '2026-11-16T11-45-00Z');
  assert.ok(isTimestamp('2026-11-16T11-45-00Z'));
  assert.ok(!isTimestamp('../../etc/passwd'));
  assert.ok(!isTimestamp('2026-11-16T11:45:00Z'));
});

test('a backup holds the PUT bodies in restore order', async () => {
  const { fetchImpl, seen } = device();
  const doc = await fetchSettings('http://esp.test', { fetchImpl });
  assert.deepEqual(seen, ['/api/zones', '/api/schedules', '/api/config', '/api/status']);
  assert.equal(doc.settings_hash, '78a2614c');
  assert.equal(doc.firmware_version, '0.6.0-phase5');
  assert.deepEqual(doc.restore.map((r) => r.path), [
    '/api/zones', '/api/schedules', '/api/schedule/active', '/api/config',
    '/api/schedule/enabled',
  ]);
  const body = Object.fromEntries(doc.restore.map((r) => [r.path, r.body]));
  assert.deepEqual(body['/api/zones'].zones[0], { zone: 1, name: 'Walkway Beds' });
  assert.equal(body['/api/schedules'].schedules[0].items[0].duration_minutes, 20);
  assert.deepEqual(body['/api/schedule/active'], { name: 'Late Fall' });
  assert.deepEqual(body['/api/schedule/enabled'], { enabled: false });
  // No ok/settings_hash leaking into a body the device would reject.
  assert.deepEqual(body['/api/config'], {
    start_hour: 7, start_minute: 0, timezone: 'PST8PDT,M3.2.0,M11.1.0',
    log_host: '', log_port: 5514,
  });
});

test('settings that change mid-read are read again', async () => {
  const { fetchImpl, seen } = device({ hashes: ['aaaa', 'bbbb', 'bbbb', 'bbbb', 'bbbb', 'bbbb'] });
  const doc = await fetchSettings('http://esp.test', { fetchImpl });
  assert.equal(doc.settings_hash, 'bbbb');
  assert.equal(seen.filter((p) => p === '/api/zones').length, 2);
});

test('settings that never settle are an error, not a mixed backup', async () => {
  const hashes = Array.from({ length: 20 }, (_, i) => `h${i}`);
  const { fetchImpl } = device({ hashes });
  await assert.rejects(fetchSettings('http://esp.test', { fetchImpl }), /kept changing/);
});

test('an unreachable settings endpoint fails the backup; status does not', async () => {
  await assert.rejects(
    fetchSettings('http://esp.test', device({ fail: (p) => p === '/api/config' })),
    /api\/config: HTTP 503/);
  const doc = await fetchSettings('http://esp.test', device({ fail: (p) => p === '/api/status' }));
  assert.equal(doc.firmware_version, null);
});

test('backupOnce writes one file, readable back, and nothing half-written', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'isprinklr-backup-'));
  try {
    const ts = await backupOnce('http://esp.test', dir, {
      ...device(), now: () => new Date('2026-11-16T11:45:00Z'),
    });
    assert.equal(ts, '2026-11-16T11-45-00Z');
    assert.deepEqual(await readdir(dir), ['settings-2026-11-16T11-45-00Z.json']);
    const raw = JSON.parse(await readFile(join(dir, 'settings-2026-11-16T11-45-00Z.json'), 'utf8'));
    assert.equal(raw.taken_at, '2026-11-16T11:45:00.000Z');
    assert.equal(raw.format, 'isprinklr-settings-backup');
    assert.deepEqual(await readBackup(dir, ts), raw);
    assert.equal(await readBackup(dir, '2020-01-01T00-00-00Z'), null);
    assert.equal(await readBackup(dir, '../x'), null);
    const list = await listBackups(dir);
    assert.equal(list.length, 1);
    assert.equal(list[0].settings_hash, '78a2614c');
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
});

test('listBackups on a missing directory is empty', async () => {
  assert.deepEqual(await listBackups(join(tmpdir(), 'isprinklr-does-not-exist')), []);
});

test('scheduleBackups runs now, retries failures, then waits for the night', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const logs = [];
  let n = 0;
  const outcomes = [new Error('offline'), new Error('offline'), 'ts-ok'];
  const run = async () => {
    const o = outcomes[n++];
    if (o instanceof Error) throw o;
    return o;
  };
  const clock = () => new Date(2026, 10, 16, 3, 45, 0);
  const sched = scheduleBackups(run, { clock, log: (m) => logs.push(m), retryDelayMs: 1000, retries: 4 });
  await Promise.resolve();
  await Promise.resolve();
  assert.equal(n, 1);
  assert.equal(sched.stats().last_error, 'offline');
  t.mock.timers.tick(1000);
  await new Promise((r) => setImmediate(r));
  assert.equal(n, 2);
  t.mock.timers.tick(1000);
  await new Promise((r) => setImmediate(r));
  assert.equal(n, 3);
  assert.equal(sched.stats().last_timestamp, 'ts-ok');
  assert.equal(sched.stats().last_error, null);
  // Next one is a day away, not a retry delay away.
  t.mock.timers.tick(1000);
  await new Promise((r) => setImmediate(r));
  assert.equal(n, 3);
  sched.stop();
  assert.match(logs.join('\n'), /retrying/);
});

test('scheduleBackups gives up until the next night after its retries', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  let n = 0;
  const run = async () => { n++; throw new Error('offline'); };
  const clock = () => new Date(2026, 10, 16, 3, 45, 0);
  const logs = [];
  const sched = scheduleBackups(run, { clock, log: (m) => logs.push(m), retryDelayMs: 1000, retries: 2 });
  for (let i = 0; i < 5; i++) {
    await new Promise((r) => setImmediate(r));
    t.mock.timers.tick(1000);
  }
  assert.equal(n, 3);
  assert.match(logs.at(-1), /giving up until tonight/);
  sched.stop();
});

test('restore commands PUT each body in order, shell-quoted', async () => {
  const { restoreCommands } = await import('../src/restore_commands.js');
  const doc = await fetchSettings('http://esp.test', device());
  doc.restore[0].body.zones[0].name = "Bob's Beds";
  const cmds = restoreCommands(doc, 'http://192.0.2.10/');
  assert.equal(cmds.length, 5);
  assert.match(cmds[0], /'\{"zones":\[\{"zone":1,"name":"Bob'\\''s Beds"\}/);
  assert.ok(cmds[0].endsWith(' http://192.0.2.10/api/zones'));
  assert.ok(cmds[4].endsWith(' http://192.0.2.10/api/schedule/enabled'));
  assert.ok(restoreCommands(doc)[0].endsWith(' http://esp.test/api/zones'));
  assert.throws(() => restoreCommands({}), /not an iSprinklr/);
});
