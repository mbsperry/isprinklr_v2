// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// One settings backup, now, outside the server: `npm run backup`. Uses the
// same ISPRINKLR_ESP_URL and ISPRINKLR_BACKUP_DIR as the server, reading
// ~/isprinklr-data/server.env for any not set in the environment.

import { existsSync } from 'node:fs';
import { homedir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { backupOnce } from './backup.js';

const envFile = join(homedir(), 'isprinklr-data', 'server.env');
if (existsSync(envFile)) process.loadEnvFile(envFile);  // never overrides

const here = dirname(fileURLToPath(import.meta.url));
const backupsDir = process.env.ISPRINKLR_BACKUP_DIR ?? join(here, '..', 'backups');
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
