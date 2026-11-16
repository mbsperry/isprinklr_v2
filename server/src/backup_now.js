// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// One settings backup, now, outside the server: `npm run backup`. Uses the
// same ISPRINKLR_ESP_URL and ISPRINKLR_BACKUP_DIR as the server. On a
// deployed host (one with ~/isprinklr-data/server.env) it reads that file and
// writes to ~/isprinklr-data/backups, as the service does.

import { existsSync } from 'node:fs';
import { homedir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { backupOnce } from './backup.js';

const dataDir = join(homedir(), 'isprinklr-data');
const envFile = join(dataDir, 'server.env');
const deployed = existsSync(envFile);
if (deployed) process.loadEnvFile(envFile);  // never overrides

// Where the systemd unit puts them when deployed, else where main.js does.
const here = dirname(fileURLToPath(import.meta.url));
const backupsDir = process.env.ISPRINKLR_BACKUP_DIR
  ?? (deployed ? join(dataDir, 'backups') : join(here, '..', 'backups'));
const espUrl = process.env.ISPRINKLR_ESP_URL;
if (!espUrl) {
  console.error(`backup: set ISPRINKLR_ESP_URL, or put it in ${envFile}`);
  process.exit(2);
}

try {
  const ts = await backupOnce(espUrl, backupsDir);
  console.log(join(backupsDir, `settings-${ts}.json`));
} catch (err) {
  console.error(`backup failed: ${err.message}`);
  process.exitCode = 1;
}
