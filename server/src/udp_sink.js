// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// Receives the ESP's UDP syslog and stores it. The ESP never hears back, by
// design (architecture.md: log transport is fire-and-forget), so a slow or
// stopped server costs the device nothing.
//
// Lines are written in batches, one transaction each: every second, or
// sooner once `batchSize` are waiting. Decision 3 asks for that if the server
// ends up on an SD card; it is cheap either way.

import dgram from 'node:dgram';

import { parseSyslog } from './syslog.js';

const MAX_DATAGRAM_BYTES = 2048;

export function startUdpSink(store, {
  port = 5514,
  host = '0.0.0.0',
  flushMs = 1000,
  batchSize = 200,
  log = console.log,
} = {}) {
  const socket = dgram.createSocket('udp4');
  let pending = [];
  let timer = null;
  let received = 0;

  const flush = () => {
    clearTimeout(timer);
    timer = null;
    if (pending.length === 0) return;
    const rows = pending;
    pending = [];
    try {
      store.insertMany(rows);
    } catch (err) {
      log(`[sink] could not store ${rows.length} lines: ${err.message}`);
    }
  };

  socket.on('message', (buf, rinfo) => {
    received += 1;
    const text = buf.subarray(0, MAX_DATAGRAM_BYTES).toString('utf8');
    pending.push(parseSyslog(text, new Date(), rinfo.address));
    if (pending.length >= batchSize) {
      flush();
    } else if (timer === null) {
      timer = setTimeout(flush, flushMs);
    }
  });
  socket.on('error', (err) => log(`[sink] socket error: ${err.message}`));

  const ready = new Promise((resolve, reject) => {
    socket.once('listening', () => {
      const a = socket.address();
      log(`[sink] listening for syslog on udp ${a.address}:${a.port}`);
      resolve(a);
    });
    socket.once('error', reject);
  });
  socket.bind(port, host);

  return {
    ready,
    flush,
    stats: () => ({ received, pending: pending.length }),
    close: () => new Promise((resolve) => {
      flush();
      socket.close(resolve);
    }),
  };
}
