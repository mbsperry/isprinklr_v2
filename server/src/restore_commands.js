// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// Prints the curl commands that restore a settings backup to the device:
// `npm run -s restore-commands -- <backup.json> [device-url]`. It only
// prints them; nothing is sent. Restoring overwrites the device's settings,
// so a person reads and runs them.

import { readFile } from 'node:fs/promises';
import { pathToFileURL } from 'node:url';

import { FORMAT } from './backup.js';

// Exported for tests.
export function restoreCommands(doc, deviceUrl) {
  if (doc?.format !== FORMAT || !Array.isArray(doc.restore)) {
    throw new Error('not an iSprinklr settings backup');
  }
  const base = String(deviceUrl ?? doc.device).replace(/\/+$/, '');
  const quote = (s) => `'${s.replace(/'/g, `'\\''`)}'`;
  return doc.restore.map(({ path, body }) =>
    `curl -sS -X PUT -H 'Content-Type: application/json' -d ${quote(JSON.stringify(body))} ${base}${path}`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const [file, deviceUrl] = process.argv.slice(2);
  if (!file) {
    console.error('usage: npm run -s restore-commands -- <backup.json> [device-url]');
    process.exit(2);
  }
  try {
    const doc = JSON.parse(await readFile(file, 'utf8'));
    console.log(restoreCommands(doc, deviceUrl).join('\n'));
  } catch (err) {
    console.error(`restore-commands: ${err.message}`);
    process.exit(1);
  }
}
