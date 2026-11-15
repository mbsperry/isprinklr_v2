// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// GET /api/logs: log history, filterable by level, module and time range
// (docs/api-contract.md, Node service). Static hosting and schedule backups
// come with Phase 6.

import http from 'node:http';

import { MAX_QUERY_LIMIT } from './log_store.js';
import { parseLevel } from './syslog.js';

class BadRequest extends Error {}

// ISO 8601 or epoch milliseconds.
function parseTime(name, text) {
  if (text === null) return undefined;
  const ms = /^\d+$/.test(text) ? Number(text) : Date.parse(text);
  if (Number.isNaN(ms)) {
    throw new BadRequest(`${name} must be an ISO 8601 time or epoch milliseconds`);
  }
  return ms;
}

function parseCount(name, text, min, max) {
  if (text === null) return undefined;
  if (!/^\d+$/.test(text) || Number(text) < min || Number(text) > max) {
    throw new BadRequest(`${name} must be a whole number, ${min}-${max}`);
  }
  return Number(text);
}

// Exported for tests: URLSearchParams -> LogStore.query() options.
export function parseLogQuery(params) {
  const options = {};
  const level = params.get('level');
  if (level !== null) {
    options.maxSeverity = parseLevel(level);
    if (options.maxSeverity === undefined) {
      throw new BadRequest('level must be error, warn, info or debug');
    }
  }
  const modules = params.getAll('module').flatMap((m) => m.split(','))
    .map((m) => m.trim()).filter(Boolean);
  if (modules.length > 0) options.modules = modules;
  options.sinceMs = parseTime('since', params.get('since'));
  options.untilMs = parseTime('until', params.get('until'));
  options.beforeId = parseCount('before_id', params.get('before_id'), 1, Number.MAX_SAFE_INTEGER);
  options.limit = parseCount('limit', params.get('limit'), 1, MAX_QUERY_LIMIT) ?? 500;
  return options;
}

function send(res, status, body) {
  res.writeHead(status, {
    'Content-Type': 'application/json',
    // The frontend may be served from elsewhere during development; this is a
    // read-only API on a trusted LAN.
    'Access-Control-Allow-Origin': '*',
  });
  res.end(JSON.stringify(body));
}

export function createHttpServer(store, { sinkStats = () => ({}) } = {}) {
  return http.createServer((req, res) => {
    const url = new URL(req.url, 'http://localhost');
    if (req.method !== 'GET') {
      send(res, 405, { ok: false, error: 'method_not_allowed' });
      return;
    }
    try {
      if (url.pathname === '/api/logs') {
        const result = store.query(parseLogQuery(url.searchParams));
        send(res, 200, {
          ok: true,
          entries: result.entries,
          next_before_id: result.nextBeforeId,
        });
        return;
      }
      if (url.pathname === '/api/health') {
        send(res, 200, { ok: true, rows: store.count(), sink: sinkStats() });
        return;
      }
      send(res, 404, { ok: false, error: 'not_found' });
    } catch (err) {
      if (err instanceof BadRequest) {
        send(res, 400, { ok: false, error: 'invalid_query', message: err.message });
      } else {
        send(res, 500, { ok: false, error: 'internal', message: err.message });
      }
    }
  });
}
