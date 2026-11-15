// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// iSprinklr v2 server. Holds no authority over watering: if this process is
// down, the ESP keeps watering and only log history is lost.
//
// Configuration, all optional, from the environment:
//   ISPRINKLR_DB         SQLite file (default server/data/logs.sqlite)
//   ISPRINKLR_UDP_PORT   syslog port (default 5514, the firmware's default)
//   ISPRINKLR_HTTP_PORT  HTTP port (default 8080)

import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { createHttpServer } from './http.js';
import { LogStore } from './log_store.js';
import { schedulePrune } from './retention.js';
import { startUdpSink } from './udp_sink.js';

const here = dirname(fileURLToPath(import.meta.url));
const dbPath = process.env.ISPRINKLR_DB ?? join(here, '..', 'data', 'logs.sqlite');
const udpPort = Number(process.env.ISPRINKLR_UDP_PORT ?? 5514);
const httpPort = Number(process.env.ISPRINKLR_HTTP_PORT ?? 8080);

const store = new LogStore(dbPath);
console.log(`[server] log database ${dbPath}, ${store.count()} rows`);

const sink = startUdpSink(store, { port: udpPort });
const stopPrune = schedulePrune((before) => store.pruneBefore(before));
const server = createHttpServer(store, { sinkStats: sink.stats });
server.listen(httpPort, () => {
  console.log(`[server] http://localhost:${httpPort}/api/logs`);
});

async function shutdown(signal) {
  console.log(`[server] ${signal}: flushing and closing`);
  stopPrune();
  server.close();
  await sink.close();
  store.close();
  process.exit(0);
}
process.on('SIGINT', () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));
