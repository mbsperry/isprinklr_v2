// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// One settings backup, now, outside the server: `npm run backup`. Uses the
// same ISPRINKLR_ESP_URL and ISPRINKLR_BACKUP_DIR as the server.

import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { backupOnce } from './backup.js';

const here = dirname(fileURLToPath(import.meta.url));
const backupsDir = process.env.ISPRINKLR_BACKUP_DIR ?? join(here, '..', 'backups');
const espUrl = process.env.ISPRINKLR_ESP_URL || 'http://192.168.88.54';

try {
  const ts = await backupOnce(espUrl, backupsDir);
  console.log(join(backupsDir, `settings-${ts}.json`));
} catch (err) {
  console.error(`backup failed: ${err.message}`);
  process.exitCode = 1;
}
