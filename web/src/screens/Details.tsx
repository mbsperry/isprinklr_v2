// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { useCallback, useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { esp } from '../api/esp';
import { server } from '../api/server';
import type { LogLevel } from '../api/types';
import { Back, ConfirmSheet, ErrorNote } from '../components/ui';
import { useAction, useController } from '../state';

interface Line { key: string; time: string | null; level: LogLevel; module: string; message: string }

const PAGE = 50;
const MODULES = ['run', 'sched', 'hunter', 'time', 'settings', 'net', 'web', 'boot', 'ota'];

function uptime(ms?: number): string {
  if (ms === undefined) return '—';
  const m = Math.floor(ms / 60000);
  const d = Math.floor(m / 1440);
  const h = Math.floor((m % 1440) / 60);
  return d ? `${d} d ${h} h` : h ? `${h} h ${m % 60} m` : `${m} min`;
}

function ago(seconds?: number | null): string {
  if (seconds === undefined || seconds === null) return 'never';
  if (seconds < 90) return 'just now';
  if (seconds < 5400) return `${Math.round(seconds / 60)} min ago`;
  return `${Math.round(seconds / 3600)} h ago`;
}

function shortTime(t: string | null): string {
  if (!t) return 'before clock set';
  return new Date(t).toLocaleString('en-US', { month: 'numeric', day: 'numeric', hour: 'numeric', minute: '2-digit', second: '2-digit' });
}

export default function Details() {
  const { status, reachable } = useController();
  const [source, setSource] = useState<'server' | 'device'>('server');
  const [level, setLevel] = useState<LogLevel>('info');
  const [module, setModule] = useState('');
  const [lines, setLines] = useState<Line[] | null>(null);
  const [logError, setLogError] = useState<string | null>(null);
  const [confirmRestart, setConfirmRestart] = useState(false);
  const [shown, setShown] = useState(PAGE);
  const { busy, error, run, setError } = useAction();

  const load = useCallback(async () => {
    setLogError(null);
    try {
      if (source === 'server') {
        const l = await server.logs({ level, modules: module ? [module] : undefined, limit: 300 });
        setLines(l.map((e) => ({ key: `s${e.id}`, ...e })));
      } else {
        const l = await esp.logs(level);
        setLines(l.filter((e) => !module || e.module === module).reverse().map((e) => ({ key: `d${e.seq}`, ...e })));
      }
    } catch {
      setLines([]);
      setLogError(source === 'server' ? "Can't reach the log server" : "Can't reach the controller");
    }
  }, [source, level, module]);

  useEffect(() => { setShown(PAGE); void load(); }, [load]);

  const restart = async () => {
    const ok = await run(() => esp.restart(status?.run?.state !== 'idle'));
    if (ok) setConfirmRestart(false);
  };

  const s = status;
  const rows: [string, string][] = s && reachable ? [
    ['Controller', 'Connected'],
    ['Clock', s.time_valid ? `Synced ${ago(s.time?.last_sync_ago_seconds)}` : 'Not set'],
    ['Running for', `${uptime(s.uptime_ms)} (${s.reset_reason ?? '?'})`],
    ['Firmware', s.firmware_version ?? '—'],
    ['Settings', s.settings ? `${s.settings.load} · ${s.settings_hash ?? ''}` : '—'],
    ["Today's trigger", s.trigger?.last_decision ?? '—'],
    ['Last recovery', s.recovery?.result ?? '—'],
    ['Log export', s.logs?.sink ? `${s.logs.sink} · ${s.logs.level}` : 'Off'],
    ['Free memory', s.memory ? `${Math.round(s.memory.free_heap / 1024)} KB · min ${Math.round(s.memory.min_free_heap / 1024)} KB` : '—'],
  ] : [['Controller', 'Not connected']];

  return (
    <main className="page">
      <header>
        <Back to="/history">History</Back>
        <h1 style={{ marginTop: 4, fontSize: 24 }}>Controller details</h1>
      </header>

      <section>
        <h2 className="label">Health</h2>
        <div className="list">
          {rows.map(([k, v]) => <div key={k} className="kv"><span className="muted">{k}</span><span className={k === 'Firmware' || k === "Today's trigger" ? 'mono' : ''}>{v}</span></div>)}
        </div>
      </section>

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, minmax(0, 1fr))', gap: 8 }}>
        <Link className="btn" to="/zones">Zones</Link>
        <Link className="btn" to="/settings">Settings</Link>
        <button type="button" className="btn" disabled={!reachable} onClick={() => { setError(null); setConfirmRestart(true); }}>Restart</button>
      </div>

      <section>
        <h2 className="label">Logs</h2>
        <div className="hscroll" style={{ marginBottom: 8 }}>
          <button type="button" className="pill" aria-pressed={source === 'server'} onClick={() => setSource('server')}>History</button>
          <button type="button" className="pill" aria-pressed={source === 'device'} onClick={() => setSource('device')}>Controller, last 200</button>
          <select className="pill" aria-label="Level" value={level} onChange={(e) => setLevel(e.target.value as LogLevel)}>
            <option value="error">Errors</option>
            <option value="warn">Warnings+</option>
            <option value="info">Info+</option>
            <option value="debug">Everything</option>
          </select>
          <select className="pill" aria-label="Module" value={module} onChange={(e) => setModule(e.target.value)}>
            <option value="">All modules</option>
            {MODULES.map((m) => <option key={m} value={m}>{m}</option>)}
          </select>
          <button type="button" className="pill" onClick={load}>Refresh</button>
        </div>
        <ErrorNote message={logError} />
        {lines && lines.length > 0 && (
          <div className="list loglines mono">
            {lines.slice(0, shown).map((l) => (
              <div key={l.key}>
                <span className="muted">{shortTime(l.time)}</span><br />
                <span className={'lvl ' + l.level}>{l.level}</span>{l.module}: {l.message}
              </div>
            ))}
          </div>
        )}
        {lines && lines.length > shown && (
          <button type="button" className="btn block" style={{ marginTop: 8 }} onClick={() => setShown(shown + PAGE)}>Show more</button>
        )}
        {lines && lines.length === 0 && !logError && <p className="muted small">No lines.</p>}
      </section>

      {confirmRestart && (
        <ConfirmSheet
          title={status?.run?.state !== 'idle' ? 'Restart while watering?' : 'Restart the controller?'}
          buttons={[{ label: 'Restart', kind: 'danger', onClick: restart, disabled: busy }]}
          onClose={() => setConfirmRestart(false)}
          error={error}
        />
      )}
    </main>
  );
}
