// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// iSprinklr v2 server. Holds no authority over watering: if this process is
// down, the ESP keeps watering and only log history, backups and the web UI
// are lost.
//
// Configuration, all optional, from the environment:
//   ISPRINKLR_DB          SQLite file (default server/data/logs.sqlite)
//   ISPRINKLR_UDP_PORT    syslog port (default 5514, the firmware's default)
//   ISPRINKLR_HTTP_PORT   HTTP port (default 8080)
//   ISPRINKLR_WEB_DIR     frontend build to serve (default web/dist)
//   ISPRINKLR_BACKUP_DIR  settings backups (default server/backups)
//   ISPRINKLR_ESP_URL     the device (default http://192.168.88.54); set it to
//                         an empty string to turn nightly backups off

import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { backupOnce, scheduleBackups } from './backup.js';
import { createHttpServer } from './http.js';
import { LogStore } from './log_store.js';
import { schedulePrune } from './retention.js';
import { createStaticHandler } from './static_files.js';
import { startUdpSink } from './udp_sink.js';

const here = dirname(fileURLToPath(import.meta.url));
const dbPath = process.env.ISPRINKLR_DB ?? join(here, '..', 'data', 'logs.sqlite');
const udpPort = Number(process.env.ISPRINKLR_UDP_PORT ?? 5514);
const httpPort = Number(process.env.ISPRINKLR_HTTP_PORT ?? 8080);
const webDir = process.env.ISPRINKLR_WEB_DIR ?? join(here, '..', '..', 'web', 'dist');
const backupsDir = process.env.ISPRINKLR_BACKUP_DIR ?? join(here, '..', 'backups');
const espUrl = process.env.ISPRINKLR_ESP_URL ?? 'http://192.168.88.54';

const store = new LogStore(dbPath);
console.log(`[server] log database ${dbPath}, ${store.count()} rows`);

const sink = startUdpSink(store, { port: udpPort });
const stopPrune = schedulePrune((before) => store.pruneBefore(before));
const backups = espUrl
  ? scheduleBackups(() => backupOnce(espUrl, backupsDir))
  : { stop() {}, stats: () => null };
console.log(espUrl
  ? `[backup] ${espUrl} -> ${backupsDir}, nightly`
  : '[backup] off (ISPRINKLR_ESP_URL is empty)');

const server = createHttpServer(store, {
  sinkStats: sink.stats,
  backupStats: backups.stats,
  backupsDir,
  staticHandler: createStaticHandler(webDir),
});
server.listen(httpPort, () => {
  console.log(`[server] http://localhost:${httpPort}/ serving ${webDir}`);
});

async function shutdown(signal) {
  console.log(`[server] ${signal}: flushing and closing`);
  stopPrune();
  backups.stop();
  server.close();
  await sink.close();
  store.close();
  process.exit(0);
}
process.on('SIGINT', () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));
