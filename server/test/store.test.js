// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import assert from 'node:assert/strict';
import { once } from 'node:events';
import dgram from 'node:dgram';
import { test } from 'node:test';

import { createHttpServer, parseLogQuery } from '../src/http.js';
import { LogStore } from '../src/log_store.js';
import { cutoff, msUntilNext, schedulePrune } from '../src/retention.js';
import { startUdpSink } from '../src/udp_sink.js';

function row(timeIso, severity, module, message) {
  const t = Date.parse(timeIso);
  return {
    time: t, timeSource: 'device', receivedAt: t, severity, module, message,
    host: 'isprinklr', boot: 'b', seq: 1, uptimeMs: 1,
  };
}

function seeded() {
  const s = new LogStore(':memory:');
  s.insertMany([
    row('2026-11-15T07:00:00Z', 6, 'run', 'zone 1 started'),
    row('2026-11-15T07:10:00Z', 6, 'run', 'zone 1 finished'),
    row('2026-11-15T07:10:05Z', 4, 'web', 'refused'),
    row('2026-11-15T08:00:00Z', 3, 'run', 'bus failed'),
    row('2026-11-15T09:00:00Z', 7, 'sched', 'trigger: disabled'),
  ]);
  return s;
}

const messages = (r) => r.entries.map((e) => e.message);

test('query filters by level, module and time, newest first', () => {
  const s = seeded();
  assert.deepEqual(messages(s.query()), [
    'trigger: disabled', 'bus failed', 'refused', 'zone 1 finished', 'zone 1 started']);
  assert.deepEqual(messages(s.query({ maxSeverity: 4 })), ['bus failed', 'refused']);
  assert.deepEqual(messages(s.query({ modules: ['run'] })),
    ['bus failed', 'zone 1 finished', 'zone 1 started']);
  assert.deepEqual(messages(s.query({
    sinceMs: Date.parse('2026-11-15T07:10:00Z'),
    untilMs: Date.parse('2026-11-15T08:00:00Z'),
  })), ['refused', 'zone 1 finished']);
  const first = s.query({ limit: 2 });
  assert.equal(first.entries.length, 2);
  assert.notEqual(first.nextBeforeId, null);
  const second = s.query({ limit: 2, beforeId: first.nextBeforeId });
  assert.deepEqual(messages(second), ['refused', 'zone 1 finished']);
  const e = first.entries[1];
  assert.equal(e.level, 'error');
  assert.equal(e.time, '2026-11-15T08:00:00.000Z');
});

test('lines from before the clock synced are placed by uptime', () => {
  const s = new LogStore(':memory:');
  const late = Date.parse('2026-11-15T22:53:50Z');
  const unsynced = (seq, up, boot = 'c7') => ({
    time: late + seq, timeSource: 'received', receivedAt: late + seq, severity: 6,
    module: 'main', message: `line ${seq}`, host: 'isprinklr', boot, seq, uptimeMs: up,
  });
  // The backlog arrives together, after the fact: two lines from before
  // the sync, then one with a real time.
  s.insertMany([unsynced(1, 55), unsynced(2, 3554)]);
  s.insertMany([{ ...unsynced(3, 7504), time: Date.parse('2026-11-15T22:53:33Z'),
                  timeSource: 'device' }]);
  // A later unsynced line from the same boot is placed straight away; one
  // from another boot is not.
  s.insertMany([unsynced(4, 8000), unsynced(5, 10, 'other')]);

  const rows = s.query().entries;
  const byMsg = Object.fromEntries(rows.map((e) => [e.message, e]));
  assert.equal(byMsg['line 1'].time, '2026-11-15T22:53:25.551Z');
  assert.equal(byMsg['line 1'].time_source, 'estimated');
  assert.equal(byMsg['line 2'].time, '2026-11-15T22:53:29.050Z');
  assert.equal(byMsg['line 3'].time_source, 'device');
  assert.equal(byMsg['line 4'].time, '2026-11-15T22:53:33.496Z');
  assert.equal(byMsg['line 5'].time_source, 'received');
  assert.deepEqual(rows.filter((e) => e.boot === 'c7').map((e) => e.message).reverse(),
    ['line 1', 'line 2', 'line 3', 'line 4']);

  // The anchor is found again after a restart (a fresh store on the same db
  // would query it; here, the same store with its cache cleared).
  s.anchors.clear();
  s.insertMany([unsynced(6, 9000)]);
  const six = s.query().entries.find((e) => e.message === 'line 6');
  assert.equal(six.time_source, 'estimated');
  assert.equal(six.time, '2026-11-15T22:53:34.496Z');
});

test('prune deletes only rows older than the cutoff', () => {
  const s = seeded();
  assert.equal(s.pruneBefore(Date.parse('2026-11-15T07:10:05Z')), 2);
  assert.equal(s.count(), 3);
});

test('cutoff is 12 calendar months back, clamped to month end', () => {
  assert.equal(cutoff(new Date(2026, 10, 15, 3, 30)).getTime(),
    new Date(2025, 10, 15, 3, 30).getTime());
  assert.equal(cutoff(new Date(2027, 2, 31, 3, 30), 1).getTime(),
    new Date(2027, 1, 28, 3, 30).getTime());
  assert.equal(cutoff(new Date(2028, 1, 29, 3, 30)).getTime(),
    new Date(2027, 1, 28, 3, 30).getTime());
});

test('prune runs at 03:30 local', () => {
  assert.equal(msUntilNext(new Date(2026, 10, 15, 3, 0)), 30 * 60 * 1000);
  assert.equal(msUntilNext(new Date(2026, 10, 15, 3, 30)), 24 * 3600 * 1000);
  const calls = [];
  const stop = schedulePrune((before) => { calls.push(before); return 0; },
    { clock: () => new Date(2026, 10, 15, 12, 0), log: () => {} });
  stop();
  assert.equal(calls.length, 1);
  assert.equal(calls[0], new Date(2025, 10, 15, 12, 0).getTime());
});

test('query string parsing', () => {
  const q = (s) => parseLogQuery(new URLSearchParams(s));
  assert.deepEqual(q(''), { limit: 500, sinceMs: undefined, untilMs: undefined, beforeId: undefined });
  const full = q('level=warn&module=run,web&module=sched&since=2026-11-15T07:00:00Z&until=1794787200000&limit=10&before_id=7');
  assert.equal(full.maxSeverity, 4);
  assert.deepEqual(full.modules, ['run', 'web', 'sched']);
  assert.equal(full.sinceMs, Date.parse('2026-11-15T07:00:00Z'));
  assert.equal(full.untilMs, 1794787200000);
  assert.equal(full.limit, 10);
  assert.equal(full.beforeId, 7);
  for (const bad of ['level=loud', 'since=yesterday', 'limit=0', 'limit=5001', 'before_id=x']) {
    assert.throws(() => q(bad), /must be/, bad);
  }
});

test('UDP in, SQLite, HTTP out', async (t) => {
  const store = new LogStore(':memory:');
  const sink = startUdpSink(store, { port: 0, host: '127.0.0.1', flushMs: 10, log: () => {} });
  const { port } = await sink.ready;
  const server = createHttpServer(store, { sinkStats: sink.stats });
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  t.after(async () => { server.close(); await sink.close(); store.close(); });

  const client = dgram.createSocket('udp4');
  const send = (line) => new Promise((resolve) => client.send(line, port, '127.0.0.1', resolve));
  await send('<134>1 2026-11-15T22:40:20Z isprinklr isprinklr - run [isp@32473 seq="1" up="5" boot="ab"] zone 3 started for 10 min (manual, 1 of 1)');
  await send('<132>1 2026-11-15T22:40:21Z isprinklr isprinklr - web [isp@32473 seq="2" up="6" boot="ab"] POST /api/zone/start refused, 400 invalid_zone: zone must be 1-48');
  client.close();

  // Lines are batched; wait for the flush.
  for (let i = 0; i < 50 && store.count() < 2; i += 1) {
    await new Promise((r) => setTimeout(r, 10));
  }

  const base = `http://127.0.0.1:${server.address().port}`;
  const all = await (await fetch(`${base}/api/logs`)).json();
  assert.equal(all.ok, true);
  assert.deepEqual(all.entries.map((e) => e.module), ['web', 'run']);
  assert.equal(all.next_before_id, null);
  const warn = await (await fetch(`${base}/api/logs?level=warn`)).json();
  assert.equal(warn.entries.length, 1);
  assert.match(warn.entries[0].message, /invalid_zone/);
  const bad = await fetch(`${base}/api/logs?level=loud`);
  assert.equal(bad.status, 400);
  const health = await (await fetch(`${base}/api/health`)).json();
  assert.equal(health.rows, 2);
  assert.equal(health.sink.received, 2);
});
