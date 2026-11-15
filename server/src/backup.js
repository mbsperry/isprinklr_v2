// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// Nightly settings backup (architecture.md): the recovery path for a wiped or
// dead board. Pulls zones, schedules, the active name, the on/off flag and
// config from the ESP and writes them to a timestamped JSON file.
//
// Read-only toward the device: a handful of GETs, one at a time, each with a
// timeout. Nothing here can change what the ESP does, and the ESP never
// waits on this process.
//
// Each file holds the exact bodies the device's PUT endpoints take, in the
// order they must be sent to a blank board, so restoring is one PUT each
// (server/README.md).

import { mkdir, readdir, readFile, rename, writeFile } from 'node:fs/promises';
import { join } from 'node:path';

import { msUntilNext } from './retention.js';

export const BACKUP_HOUR = 3;  // local time: after the 03:30 log prune,
export const BACKUP_MINUTE = 45;  // well clear of a 07:00 run
export const FORMAT = 'isprinklr-settings-backup';
export const FORMAT_VERSION = 1;

const REQUEST_TIMEOUT_MS = 10_000;
// If settings change between the GETs, their hashes differ; try again.
const CONSISTENCY_ATTEMPTS = 3;
// A failed nightly backup (ESP offline, say) is retried this often, this many
// times, then left for the next night.
export const RETRY_DELAY_MS = 30 * 60 * 1000;
export const RETRIES = 4;

// 2026-11-16T11:45:00.123Z -> 2026-11-16T11-45-00Z. Safe in a file name and a
// URL, and sorts in time order as text.
export function timestampFor(date) {
  return date.toISOString().replace(/\.\d+Z$/, 'Z').replace(/:/g, '-');
}

const TIMESTAMP = /^\d{4}-\d{2}-\d{2}T\d{2}-\d{2}-\d{2}Z$/;
const FILE = /^settings-(\d{4}-\d{2}-\d{2}T\d{2}-\d{2}-\d{2}Z)\.json$/;

export function isTimestamp(text) {
  return TIMESTAMP.test(text);
}

// /api/status carries no "ok" field; the settings endpoints do.
async function getJson(fetchImpl, baseUrl, path, { requireOk = true } = {}) {
  const res = await fetchImpl(new URL(path, baseUrl), {
    signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
    headers: { Accept: 'application/json' },
  });
  if (!res.ok) throw new Error(`GET ${path}: HTTP ${res.status}`);
  const body = await res.json();
  if (requireOk && body?.ok !== true) throw new Error(`GET ${path}: device said not ok`);
  return body;
}

function need(cond, what) {
  if (!cond) throw new Error(`device response missing ${what}`);
}

// Fetches the device's settings and returns a backup document. Throws if the
// device is unreachable, answers oddly, or keeps changing underneath us.
export async function fetchSettings(espUrl, { fetchImpl = fetch } = {}) {
  for (let attempt = 1; ; attempt += 1) {
    const zones = await getJson(fetchImpl, espUrl, '/api/zones');
    const schedules = await getJson(fetchImpl, espUrl, '/api/schedules');
    const config = await getJson(fetchImpl, espUrl, '/api/config');
    const hashes = new Set([zones.settings_hash, schedules.settings_hash, config.settings_hash]);
    if (hashes.size !== 1) {
      if (attempt < CONSISTENCY_ATTEMPTS) continue;
      throw new Error('settings kept changing while being read');
    }
    need(Array.isArray(zones.zones), 'zones');
    need(Array.isArray(schedules.schedules), 'schedules');
    need('active' in schedules, 'active');
    need(typeof schedules.enabled === 'boolean', 'enabled');
    need(Number.isInteger(config.start_hour) && Number.isInteger(config.start_minute)
      && typeof config.timezone === 'string', 'config');

    // Only informational; a backup without it is still a backup.
    let firmwareVersion = null;
    try {
      firmwareVersion = (await getJson(fetchImpl, espUrl, '/api/status', { requireOk: false }))
        .firmware_version ?? null;
    } catch {
      // ignore
    }

    const restoreConfig = {
      start_hour: config.start_hour,
      start_minute: config.start_minute,
      timezone: config.timezone,
    };
    if ('log_host' in config) restoreConfig.log_host = config.log_host;
    if ('log_port' in config) restoreConfig.log_port = config.log_port;

    return {
      format: FORMAT,
      format_version: FORMAT_VERSION,
      taken_at: null,  // set when written
      device: String(espUrl),
      firmware_version: firmwareVersion,
      settings_hash: zones.settings_hash,
      // PUT each body to its path, in this order, to rebuild a blank board.
      // Zones must exist before schedules use them, and a schedule before it
      // can be made active; the on/off flag goes last.
      restore: [
        { path: '/api/zones', body: { zones: zones.zones } },
        { path: '/api/schedules', body: { schedules: schedules.schedules } },
        { path: '/api/schedule/active', body: { name: schedules.active } },
        { path: '/api/config', body: restoreConfig },
        { path: '/api/schedule/enabled', body: { enabled: schedules.enabled } },
      ],
    };
  }
}

// Writes doc as settings-<timestamp>.json under dir, atomically (a reader
// never sees half a file). Returns the timestamp.
export async function writeBackup(dir, doc, now = new Date()) {
  await mkdir(dir, { recursive: true });
  const ts = timestampFor(now);
  const finalPath = join(dir, `settings-${ts}.json`);
  const tmpPath = `${finalPath}.tmp`;
  const body = JSON.stringify({ ...doc, taken_at: now.toISOString() }, null, 2) + '\n';
  await writeFile(tmpPath, body);
  await rename(tmpPath, finalPath);
  return ts;
}

// Newest first: [{timestamp, taken_at, settings_hash, bytes}].
export async function listBackups(dir) {
  let names;
  try {
    names = await readdir(dir);
  } catch (err) {
    if (err.code === 'ENOENT') return [];
    throw err;
  }
  const out = [];
  for (const name of names) {
    const m = FILE.exec(name);
    if (!m) continue;
    const text = await readFile(join(dir, name), 'utf8');
    let doc = {};
    try {
      doc = JSON.parse(text);
    } catch {
      // listed anyway, so a damaged file is visible rather than hidden
    }
    out.push({
      timestamp: m[1],
      taken_at: doc.taken_at ?? null,
      settings_hash: doc.settings_hash ?? null,
      bytes: Buffer.byteLength(text),
    });
  }
  return out.sort((a, b) => (a.timestamp < b.timestamp ? 1 : -1));
}

// The parsed backup, or null if there is none with that timestamp.
export async function readBackup(dir, timestamp) {
  if (!isTimestamp(timestamp)) return null;
  try {
    return JSON.parse(await readFile(join(dir, `settings-${timestamp}.json`), 'utf8'));
  } catch (err) {
    if (err.code === 'ENOENT') return null;
    throw err;
  }
}

// One backup, start to finish. Resolves to the timestamp written.
export async function backupOnce(espUrl, dir, { fetchImpl = fetch, now = () => new Date() } = {}) {
  const doc = await fetchSettings(espUrl, { fetchImpl });
  return writeBackup(dir, doc, now());
}

// Backs up once now and then every night at BACKUP_HOUR:BACKUP_MINUTE local
// time, retrying a failure a few times. Returns { stop, stats }.
export function scheduleBackups(run, {
  clock = () => new Date(),
  log = console.log,
  hour = BACKUP_HOUR,
  minute = BACKUP_MINUTE,
  retryDelayMs = RETRY_DELAY_MS,
  retries = RETRIES,
} = {}) {
  let timer = null;
  let stopped = false;
  const stats = { last_success: null, last_timestamp: null, last_error: null, last_attempt: null };

  const arm = (ms, attempt) => {
    if (!stopped) timer = setTimeout(() => go(attempt), ms);
  };

  const go = async (attempt) => {
    stats.last_attempt = clock().toISOString();
    try {
      const ts = await run();
      stats.last_success = clock().toISOString();
      stats.last_timestamp = ts;
      stats.last_error = null;
      log(`[backup] wrote settings-${ts}.json`);
      arm(msUntilNext(clock(), hour, minute), 0);
    } catch (err) {
      stats.last_error = err.message;
      if (attempt < retries) {
        log(`[backup] failed (${err.message}); retrying in ${Math.round(retryDelayMs / 60000)} min`);
        arm(retryDelayMs, attempt + 1);
      } else {
        log(`[backup] failed (${err.message}); giving up until tonight`);
        arm(msUntilNext(clock(), hour, minute), 0);
      }
    }
  };

  go(0);
  return {
    stop: () => {
      stopped = true;
      clearTimeout(timer);
    },
    stats: () => ({ ...stats }),
  };
}
