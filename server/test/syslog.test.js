// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import assert from 'node:assert/strict';
import { test } from 'node:test';

import { parseLevel, parseSyslog } from '../src/syslog.js';

const received = new Date('2026-11-15T22:40:21.500Z');

test('parses a line from the device with a valid clock', () => {
  // Exactly what firmware/test/test_log_ring expects formatSyslog to produce.
  const row = parseSyslog(
    '<134>1 2026-11-15T22:40:20Z isprinklr isprinklr - run ' +
    '[isp@32473 seq="42" up="81234" boot="1a2b3c4d"] zone 3 started for 10 min',
    received, '192.168.88.54');
  assert.deepEqual(row, {
    time: Date.parse('2026-11-15T22:40:20Z'),
    timeSource: 'device',
    receivedAt: received.getTime(),
    severity: 6,
    module: 'run',
    message: 'zone 3 started for 10 min',
    host: 'isprinklr',
    boot: '1a2b3c4d',
    seq: 42,
    uptimeMs: 81234,
  });
});

test('a line sent before the clock synced takes the received time', () => {
  const row = parseSyslog(
    '<131>1 - isprinklr isprinklr - a_b [isp@32473 seq="1" up="0" boot="00000000"] line1 line2',
    received);
  assert.equal(row.time, received.getTime());
  assert.equal(row.timeSource, 'received');
  assert.equal(row.severity, 3);
  assert.equal(row.module, 'a_b');
  assert.equal(row.message, 'line1 line2');
  assert.equal(row.seq, 1);
});

test('a message containing brackets and quotes is kept whole', () => {
  const row = parseSyslog(
    '<132>1 2026-11-15T22:40:20Z isprinklr isprinklr - web ' +
    '[isp@32473 seq="7" up="5" boot="1"] POST /api/zone/start refused, 400 invalid_zone: zone must be 1-48 ["x"]',
    received);
  assert.equal(row.message, 'POST /api/zone/start refused, 400 invalid_zone: zone must be 1-48 ["x"]');
  assert.equal(row.severity, 4);
});

test('no structured data and an empty message', () => {
  const row = parseSyslog('<14>1 2026-11-15T22:40:20.123Z h app - - -', received);
  assert.equal(row.module, '-');
  assert.equal(row.message, '');
  assert.equal(row.seq, null);
  assert.equal(row.time, Date.parse('2026-11-15T22:40:20.123Z'));
});

test('anything else is kept as unparsed', () => {
  const row = parseSyslog('<30>Nov 15 14:40:20 router dhcp: lease', received, '192.168.88.1');
  assert.equal(row.module, 'unparsed');
  assert.equal(row.severity, 6);
  assert.equal(row.message, '<30>Nov 15 14:40:20 router dhcp: lease');
  assert.equal(row.host, '192.168.88.1');
  assert.equal(parseSyslog('hello', received).severity, 6);
});

test('level names', () => {
  assert.equal(parseLevel('warn'), 4);
  assert.equal(parseLevel('ERROR'), 3);
  assert.equal(parseLevel('7'), 7);
  assert.equal(parseLevel('verbose'), undefined);
  assert.equal(parseLevel(null), undefined);
});
