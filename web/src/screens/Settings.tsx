// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { useEffect, useState } from 'react';
import { builtInControllerUrl, controllerUrl, setControllerUrl } from '../api/config';
import { esp } from '../api/esp';
import { server } from '../api/server';
import type { Backup, LogLevel } from '../api/types';
import { Back, ErrorNote } from '../components/ui';
import { useAction, useController } from '../state';

const TIMEZONES: [string, string][] = [
  ['PST8PDT,M3.2.0,M11.1.0', 'US Pacific'],
  ['MST7MDT,M3.2.0,M11.1.0', 'US Mountain'],
  ['MST7', 'US Arizona'],
  ['CST6CDT,M3.2.0,M11.1.0', 'US Central'],
  ['EST5EDT,M3.2.0,M11.1.0', 'US Eastern'],
];

const LEVELS: [LogLevel, string][] = [['error', 'Error'], ['warn', 'Warning'], ['info', 'Info'], ['debug', 'Debug']];

export default function Settings() {
  const { config, refresh } = useController();
  const [tz, setTz] = useState('');
  const [level, setLevel] = useState<LogLevel>('debug');
  const [logHost, setLogHost] = useState('');
  const [address, setAddress] = useState(controllerUrl());
  const [backups, setBackups] = useState<Backup[] | null>(null);
  const [saved, setSaved] = useState(false);
  const { busy, error, run } = useAction();

  useEffect(() => {
    if (!config) return;
    setTz(config.timezone);
    setLevel(config.log_level ?? 'debug');
    setLogHost(config.log_host ?? '');
  }, [config]);

  useEffect(() => {
    server.backups().then(setBackups).catch(() => setBackups([]));
  }, []);

  const save = async () => {
    if (!config) return;
    setSaved(false);
    const ok = await run(() => esp.putConfig({
      start_hour: config.start_hour,
      start_minute: config.start_minute,
      timezone: tz,
      log_level: level,
      log_host: logHost.trim(),
    }));
    if (ok) setSaved(true);
  };

  const saveAddress = () => {
    setControllerUrl(address);
    setAddress(controllerUrl());
    void refresh();
  };

  const knownTz = TIMEZONES.some(([v]) => v === tz);

  return (
    <main className="page">
      <header>
        <Back to="/details">Controller details</Back>
        <h1 style={{ marginTop: 4, fontSize: 24 }}>Settings</h1>
      </header>

      {config && (
        <section>
          <h2 className="label">Controller</h2>
          <div className="card" style={{ display: 'flex', flexDirection: 'column', gap: 14 }}>
            <div className="field">
              <label htmlFor="tz">Time zone</label>
              <select id="tz" className="input" value={tz} onChange={(e) => setTz(e.target.value)}>
                {TIMEZONES.map(([v, l]) => <option key={v} value={v}>{l}</option>)}
                {!knownTz && <option value={tz}>{tz}</option>}
              </select>
            </div>
            <div className="field">
              <label htmlFor="lvl">Log detail</label>
              <select id="lvl" className="input" value={level} onChange={(e) => setLevel(e.target.value as LogLevel)}>
                {LEVELS.map(([v, l]) => <option key={v} value={v}>{l}</option>)}
              </select>
            </div>
            <div className="field">
              <label htmlFor="lh">Send logs to (server IP, blank for off)</label>
              <div className="row" style={{ gap: 8 }}>
                <input id="lh" className="input mono grow" inputMode="decimal" value={logHost} onChange={(e) => setLogHost(e.target.value)} />
                {/^\d+\.\d+\.\d+\.\d+$/.test(location.hostname) && logHost !== location.hostname && (
                  <button type="button" className="btn small" onClick={() => setLogHost(location.hostname)}>This server</button>
                )}
              </div>
            </div>
            <ErrorNote message={error} />
            <button type="button" className="btn primary block" disabled={busy} onClick={save}>{saved ? 'Saved' : 'Save'}</button>
          </div>
        </section>
      )}

      <section>
        <h2 className="label">This device</h2>
        <div className="card" style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
          <div className="field">
            <label htmlFor="ca">Controller address</label>
            <input id="ca" className="input mono" value={address} placeholder={builtInControllerUrl()} onChange={(e) => setAddress(e.target.value)} />
          </div>
          <div className="row">
            <button type="button" className="btn grow" onClick={() => { setAddress(''); setControllerUrl(''); setAddress(controllerUrl()); void refresh(); }}>Reset</button>
            <button type="button" className="btn primary grow" onClick={saveAddress}>Save</button>
          </div>
          <p className="muted tiny" style={{ margin: 0 }}>Stored in this browser only.</p>
        </div>
      </section>

      <section>
        <h2 className="label">Backups</h2>
        <div className="list">
          {backups === null && <div className="kv muted">Loading…</div>}
          {backups?.length === 0 && <div className="kv muted">None available</div>}
          {backups?.slice(0, 5).map((b) => (
            <a key={b.timestamp} className="kv" href={`/api/backups/${encodeURIComponent(b.timestamp)}`} download style={{ textDecoration: 'none', color: 'var(--ink)' }}>
              <span>{new Date(b.taken_at).toLocaleString('en-US', { dateStyle: 'medium', timeStyle: 'short' })}</span>
              <span className="mono muted">{b.settings_hash}</span>
            </a>
          ))}
        </div>
      </section>
    </main>
  );
}
