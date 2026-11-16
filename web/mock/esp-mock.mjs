// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// A fake controller and log server for working on the UI without the device.
// Not a test of the firmware: shapes follow docs/api-contract.md, behaviour is
// approximate. Controller on :8081 (what `npm run dev` proxies /esp to), log
// server on :8080 (what it proxies /api to).
//
//   MOCK_SPEED=60 npm run mock    runs zones 60x faster than real time
//   MOCK_STATE=off npm run mock   starts with automatic watering off

import http from 'node:http';

const SPEED = Number(process.env.MOCK_SPEED ?? 1);
const ESP_PORT = Number(process.env.MOCK_ESP_PORT ?? 8081);
const SERVER_PORT = Number(process.env.MOCK_SERVER_PORT ?? 8080);
const PAD_MS = 5000 / SPEED;

const zones = [
  'Front lawn', 'Side yard', 'Back lawn', 'North strip',
  'Vegetable beds', 'Walkway beds', 'Roses', 'Patio pots',
].map((name, i) => ({ zone: i + 1, name }));

const fall = (days) => [1, 2, 3, 5, 6, 8].map((zone, i) => ({ zone, days, duration_minutes: [20, 15, 20, 20, 25, 10][i] }));
let schedules = [
  { name: 'Late Fall', items: fall('TU:F') },
  { name: 'Fall', items: fall('M:W:F') },
  { name: 'Summer', items: zones.map((z) => ({ zone: z.zone, days: 'ALL', duration_minutes: 20 })) },
  { name: 'Spring', items: fall('EO') },
  { name: 'Catch up', items: zones.map((z) => ({ zone: z.zone, days: 'NONE', duration_minutes: 15 })) },
];
let active = 'Late Fall';
let enabled = process.env.MOCK_STATE !== 'off';
let config = { start_hour: 7, start_minute: 0, timezone: 'PST8PDT,M3.2.0,M11.1.0', log_host: '', log_port: 5514, log_level: 'debug' };
let hash = 1;
const bootAt = Date.now();

// The run: entries plus the time the current zone started.
let run = { state: 'idle', schedule: null, current_index: 0, entries: [] };
let zoneStartedAt = 0;
let padUntil = 0;

const ring = [];
let seq = 0;
function log(level, module, message) {
  ring.push({ seq: seq++, uptime_ms: Date.now() - bootAt, time: new Date().toISOString(), level, module, message });
  if (ring.length > 200) ring.shift();
}

function startCurrent() {
  run.state = 'running';
  zoneStartedAt = Date.now();
  const e = run.entries[run.current_index];
  e.outcome = 'running';
  e.started_epoch = Math.floor(Date.now() / 1000);
  log('info', 'run', `zone ${e.zone} started for ${e.duration_minutes} min (${e.origin}, ${run.current_index + 1} of ${run.entries.length})`);
}

function advance() {
  const nextIndex = run.entries.findIndex((e, i) => i > run.current_index && e.outcome === 'pending');
  if (nextIndex === -1) {
    log('info', 'run', `${run.schedule ? `run '${run.schedule}'` : 'manual run'} finished`);
    run.state = 'idle';
    return;
  }
  run.current_index = nextIndex;
  run.state = 'padding';
  padUntil = Date.now() + PAD_MS;
}

function tick() {
  if (run.state === 'running') {
    const e = run.entries[run.current_index];
    if (Date.now() - zoneStartedAt >= (e.duration_minutes * 60000) / SPEED) {
      e.outcome = 'completed';
      log('info', 'run', `zone ${e.zone} finished: ran its full ${e.duration_minutes} min`);
      advance();
    }
  } else if (run.state === 'padding' && Date.now() >= padUntil) {
    startCurrent();
  }
}
setInterval(tick, 200);

function enqueue(items, origin, scheduleName) {
  const idle = run.state === 'idle';
  if (idle) run = { state: 'idle', schedule: scheduleName ?? null, current_index: 0, entries: [] };
  const ahead = run.entries.filter((e) => e.outcome === 'pending' || e.outcome === 'running').length;
  for (const it of items) run.entries.push({ zone: it.zone, duration_minutes: it.duration_minutes, origin, outcome: 'pending' });
  log('info', 'run', `queued ${items.length} zone(s) (${origin})`);
  if (idle) {
    run.current_index = 0;
    startCurrent();
  }
  return ahead;
}

function remainingSeconds() {
  if (run.state !== 'running') return 0;
  const e = run.entries[run.current_index];
  return Math.max(0, Math.round((e.duration_minutes * 60000 - (Date.now() - zoneStartedAt) * SPEED) / 1000));
}

function status() {
  const running = run.state === 'running';
  const e = run.entries[run.current_index];
  return {
    firmware_version: '0.0.0-mock',
    uptime_ms: Date.now() - bootAt,
    reset_reason: 'poweron',
    time_valid: true,
    settings_hash: hash.toString(16).padStart(8, '0'),
    time: { valid: true, last_sync_ago_seconds: 240, timezone: config.timezone },
    settings: { load: 'loaded', zones: zones.length, schedules: schedules.length, active_schedule: active, enabled, start_time: `${String(config.start_hour).padStart(2, '0')}:${String(config.start_minute).padStart(2, '0')}` },
    zones: { commanded_zone: running ? e.zone : null, duration_minutes: running ? e.duration_minutes : undefined, remaining_seconds: running ? remainingSeconds() : undefined },
    run: { ...run, remaining_seconds: remainingSeconds() },
    trigger: { last_decision: enabled ? 'before_start' : 'disabled', window_minutes: 60, last_run_day: 0 },
    recovery: { result: 'none', window_minutes: 60 },
    logs: { capacity: 200, last_seq: seq, sink: config.log_host ? `${config.log_host}:${config.log_port}` : '', level: config.log_level },
    memory: { free_heap: 186000, min_free_heap: 151000 },
  };
}

function send(res, code, body) {
  res.writeHead(code, {
    'Content-Type': 'application/json',
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'GET, POST, PUT, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type',
  });
  res.end(JSON.stringify(body));
}
const ok = (res, body = {}) => send(res, 200, { ok: true, settings_hash: status().settings_hash, ...body });
const fail = (res, code, error, message = error) => send(res, code, { ok: false, error, message });
const changed = () => { hash++; };

async function body(req) {
  let s = '';
  for await (const chunk of req) s += chunk;
  try { return s ? JSON.parse(s) : {}; } catch { return null; }
}

http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://x');
  const key = `${req.method} ${url.pathname}`;
  if (req.method === 'OPTIONS') return send(res, 204, {});
  const b = req.method === 'GET' ? {} : await body(req);
  if (b === null) return fail(res, 400, 'invalid_body');
  switch (key) {
    case 'GET /api/status': return send(res, 200, status());
    case 'GET /api/zones': return ok(res, { zones });
    case 'PUT /api/zones': {
      const used = new Set(schedules.flatMap((s) => s.items.map((i) => i.zone)));
      if ([...used].some((z) => !b.zones.some((n) => n.zone === z))) return fail(res, 400, 'invalid_settings', 'a schedule uses that zone');
      if (b.zones.length > 10 || b.zones.some((z) => z.zone < 1 || z.zone > 10)) return fail(res, 400, 'invalid_settings', 'zones 1-10');
      zones.splice(0, zones.length, ...b.zones);
      changed();
      return ok(res, { zones });
    }
    case 'GET /api/schedules': return ok(res, { schedules, active, enabled });
    case 'PUT /api/schedules':
      if (active && !b.schedules.some((s) => s.name === active)) return fail(res, 400, 'invalid_settings', 'active schedule missing');
      schedules = b.schedules;
      changed();
      return ok(res, { schedules, active, enabled });
    case 'PUT /api/schedule/active':
      if (b.name !== null && !schedules.some((s) => s.name === b.name)) return fail(res, 400, 'invalid_settings', 'unknown schedule');
      active = b.name;
      changed();
      return ok(res, { name: active });
    case 'PUT /api/schedule/enabled':
      enabled = !!b.enabled;
      changed();
      log('info', 'sched', `scheduler ${enabled ? 'on' : 'off'}`);
      return ok(res, { enabled });
    case 'GET /api/config': return ok(res, config);
    case 'PUT /api/config':
      config = { ...config, ...b };
      changed();
      return ok(res, config);
    case 'POST /api/zone/start':
      return send(res, 202, { ok: true, action: 'start', zone: b.zone, duration_minutes: b.duration_minutes, queued_behind: enqueue([b], 'manual') });
    case 'POST /api/run':
      return send(res, 202, { ok: true, action: 'run', queued_behind: enqueue(b.items, 'manual') });
    case 'POST /api/schedule/run': {
      const s = schedules.find((x) => x.name === b.name);
      if (!s) return fail(res, 404, 'unknown_schedule');
      const items = s.items.filter((i) => i.days !== 'NONE' || s.items.every((x) => x.days === 'NONE'));
      return send(res, 202, { ok: true, action: 'run_schedule', schedule: s.name, queued_behind: enqueue(items, 'manual', s.name) });
    }
    case 'POST /api/zone/stop':
      if (run.state === 'idle') return fail(res, 409, 'no_active_zone');
      for (const e of run.entries) if (e.outcome === 'pending' || e.outcome === 'running') e.outcome = 'interrupted';
      run.state = 'idle';
      log('info', 'run', 'run cancelled');
      return send(res, 202, { ok: true, action: 'cancel', zone: null });
    case 'POST /api/run/skip': {
      if (run.state === 'idle') return fail(res, 409, 'nothing_to_skip');
      const e = run.entries[run.current_index];
      e.outcome = 'skipped';
      log('info', 'run', `zone ${e.zone} cut short by skip`);
      advance();
      return send(res, 202, { ok: true, action: 'skip', zone: e.zone });
    }
    case 'POST /api/restart':
      if (run.state !== 'idle' && url.searchParams.get('force') !== '1') return fail(res, 409, 'run_in_progress');
      return ok(res, { action: 'restart' });
    case 'GET /api/logs':
      return ok(res, { capacity: 200, entries: ring });
    default:
      return fail(res, 404, 'not_found');
  }
}).listen(ESP_PORT, () => console.log(`mock controller on http://localhost:${ESP_PORT}`));

// The log server: a fortnight of plausible history.
const history = [];
{
  let id = 1;
  const now = new Date();
  for (let back = 14; back >= 1; back--) {
    const d = new Date(now.getFullYear(), now.getMonth(), now.getDate() - back, 7, 0);
    const wd = d.getDay();
    if (wd !== 2 && wd !== 5) continue;
    let t = d.getTime();
    const at = (level, module, message) => history.push({ id: id++, time: new Date(t).toISOString(), level, module, message });
    at('info', 'sched', "starting today's scheduled run 'Late Fall'");
    for (const it of fall('TU:F')) {
      at('info', 'run', `zone ${it.zone} started for ${it.duration_minutes} min (scheduled, 'Late Fall')`);
      t += it.duration_minutes * 60000;
      if (back === 4 && it.zone === 2) at('info', 'run', `zone 2 cut short by skip after 4 min of 15 min (scheduled, 'Late Fall')`);
      else at('info', 'run', `zone ${it.zone} finished: ran its full ${it.duration_minutes} min (scheduled, 'Late Fall')`);
      t += 5000;
    }
    at('info', 'run', `run 'Late Fall' finished: ${back === 4 ? '5 completed, 1 cut short' : '6 completed'}`);
  }
  history.reverse();
}

http.createServer((req, res) => {
  const url = new URL(req.url, 'http://x');
  if (url.pathname === '/api/logs') {
    const modules = url.searchParams.get('module')?.split(',');
    const order = ['debug', 'info', 'warn', 'error'];
    const min = order.indexOf(url.searchParams.get('level') ?? 'debug');
    const entries = history.filter((e) => (!modules || modules.includes(e.module)) && order.indexOf(e.level) >= min);
    return send(res, 200, { ok: true, next_before_id: null, entries });
  }
  if (url.pathname === '/api/backups') {
    return send(res, 200, { ok: true, backups: [{ timestamp: '2026-11-16T11-45-00Z', taken_at: '2026-11-16T11:45:00.000Z', settings_hash: '78a2614c', bytes: 7208 }] });
  }
  return send(res, 404, { ok: false, error: 'not_found' });
}).listen(SERVER_PORT, () => console.log(`mock log server on http://localhost:${SERVER_PORT}`));
