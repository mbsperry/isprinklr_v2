// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// Log history in SQLite (open decision 3): one table, indexed for the level,
// module and time-range filters the frontend needs, kept 12 months.
//
// Uses node:sqlite (built into Node 22.13+), so there is no native module to
// build on whatever ends up hosting this.

import { mkdirSync } from 'node:fs';
import { dirname } from 'node:path';
import { DatabaseSync } from 'node:sqlite';

import { levelName } from './syslog.js';

const SCHEMA = `
CREATE TABLE IF NOT EXISTS logs (
  id          INTEGER PRIMARY KEY,
  time_ms     INTEGER NOT NULL,  -- device time, or received time if it had none
  time_source TEXT    NOT NULL,  -- 'device' | 'estimated' | 'received'
  received_ms INTEGER NOT NULL,
  severity    INTEGER NOT NULL,  -- syslog: 3 error, 4 warn, 6 info, 7 debug
  module      TEXT    NOT NULL,
  message     TEXT    NOT NULL,
  host        TEXT,
  boot        TEXT,              -- the ESP's per-boot id
  seq         INTEGER,           -- line number since that boot
  uptime_ms   INTEGER
);
CREATE INDEX IF NOT EXISTS logs_time ON logs (time_ms);
CREATE INDEX IF NOT EXISTS logs_severity_time ON logs (severity, time_ms);
CREATE INDEX IF NOT EXISTS logs_module_time ON logs (module, time_ms);
CREATE INDEX IF NOT EXISTS logs_boot ON logs (boot, time_source);
`;

export const MAX_QUERY_LIMIT = 5000;

export class LogStore {
  // path: a file, or ':memory:' for tests.
  constructor(path) {
    if (path !== ':memory:') mkdirSync(dirname(path), { recursive: true });
    this.db = new DatabaseSync(path);
    this.db.exec('PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL;');
    this.db.exec(SCHEMA);
    this.insertStmt = this.db.prepare(`
      INSERT INTO logs (time_ms, time_source, received_ms, severity, module,
                        message, host, boot, seq, uptime_ms)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`);
    this.findAnchorStmt = this.db.prepare(`
      SELECT time_ms - uptime_ms AS offset FROM logs
      WHERE boot = ? AND host IS ? AND time_source = 'device'
        AND uptime_ms IS NOT NULL
      LIMIT 1`);
    this.backfillStmt = this.db.prepare(`
      UPDATE logs SET time_ms = ? + uptime_ms, time_source = 'estimated'
      WHERE boot = ? AND host IS ? AND time_source = 'received'
        AND uptime_ms IS NOT NULL`);
    // "host boot" -> device clock minus uptime, once a line with a real time
    // has been seen for that boot.
    this.anchors = new Map();
  }

  // Lines the ESP logged before its clock synced carry no time, only uptime
  // and a per-boot id, and would sort by when they arrived (for the backlog
  // sent once a sink is set, that can be minutes late). Once any line from
  // the same boot carries a real time, these are placed by uptime instead,
  // marked 'estimated'; accurate to about a second, as device times are.
  placeByUptime(r) {
    if (r.boot === null || r.uptimeMs === null) return;
    const key = `${r.host} ${r.boot}`;
    if (r.timeSource === 'device') {
      if (!this.anchors.has(key)) {
        const offset = r.time - r.uptimeMs;
        this.anchors.set(key, offset);
        this.backfillStmt.run(offset, r.boot, r.host);
      }
      return;
    }
    let offset = this.anchors.get(key);
    if (offset === undefined) {
      offset = this.findAnchorStmt.get(r.boot, r.host)?.offset;
      if (offset === undefined) return;
      this.anchors.set(key, offset);
    }
    r.time = Number(offset) + r.uptimeMs;
    r.timeSource = 'estimated';
  }

  // Rows from parseSyslog, in one transaction.
  insertMany(rows) {
    if (rows.length === 0) return;
    this.db.exec('BEGIN');
    try {
      for (const original of rows) {
        const r = { ...original };
        this.placeByUptime(r);
        this.insertStmt.run(r.time, r.timeSource, r.receivedAt, r.severity,
          r.module, r.message, r.host, r.boot, r.seq, r.uptimeMs);
      }
      this.db.exec('COMMIT');
    } catch (err) {
      this.db.exec('ROLLBACK');
      throw err;
    }
  }

  // Newest first.
  //   maxSeverity: this level or more severe (lower number)
  //   modules:     array of module names
  //   sinceMs / untilMs: time range, inclusive / exclusive
  //   beforeId:    for paging; pass the previous page's nextBeforeId
  //   limit:       1..MAX_QUERY_LIMIT
  query({ maxSeverity, modules, sinceMs, untilMs, beforeId, limit = 500 } = {}) {
    const where = [];
    const args = [];
    if (maxSeverity !== undefined) { where.push('severity <= ?'); args.push(maxSeverity); }
    if (modules && modules.length > 0) {
      where.push(`module IN (${modules.map(() => '?').join(',')})`);
      args.push(...modules);
    }
    if (sinceMs !== undefined) { where.push('time_ms >= ?'); args.push(sinceMs); }
    if (untilMs !== undefined) { where.push('time_ms < ?'); args.push(untilMs); }
    if (beforeId !== undefined) { where.push('id < ?'); args.push(beforeId); }
    const n = Math.max(1, Math.min(limit, MAX_QUERY_LIMIT));
    const sql = `SELECT * FROM logs ${where.length ? 'WHERE ' + where.join(' AND ') : ''}
                 ORDER BY time_ms DESC, id DESC LIMIT ?`;
    const rows = this.db.prepare(sql).all(...args, n + 1);
    const page = rows.slice(0, n).map(toApi);
    return {
      entries: page,
      // Paging by id is exact only while time order and id order agree, which
      // holds for one device sending in order; good enough for a log view.
      nextBeforeId: rows.length > n ? page[page.length - 1].id : null,
    };
  }

  // Deletes rows older than cutoffMs. Returns how many.
  pruneBefore(cutoffMs) {
    return Number(this.db.prepare('DELETE FROM logs WHERE time_ms < ?')
      .run(cutoffMs).changes);
  }

  count() {
    return Number(this.db.prepare('SELECT COUNT(*) AS n FROM logs').get().n);
  }

  close() {
    this.db.close();
  }
}

function toApi(row) {
  return {
    id: row.id,
    time: new Date(row.time_ms).toISOString(),
    time_source: row.time_source,
    received: new Date(row.received_ms).toISOString(),
    level: levelName(row.severity),
    module: row.module,
    message: row.message,
    host: row.host,
    boot: row.boot,
    seq: row.seq,
    uptime_ms: row.uptime_ms,
  };
}
