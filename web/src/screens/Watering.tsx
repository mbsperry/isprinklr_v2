// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { Link } from 'react-router-dom';
import { esp } from '../api/esp';
import { ErrorNote } from '../components/ui';
import { formatCountdown, formatTimeOfDay } from '../lib/durations';
import { isRunning } from '../lib/health';
import { useAction, useController } from '../state';

const OUTCOME: Record<string, string> = {
  completed: 'Done', skipped: 'Skipped', interrupted: 'Stopped', failed: "Didn't start", pending: 'Waiting', running: 'Watering',
};

export default function Watering() {
  const { status, zoneName } = useController();
  const { busy, error, run } = useAction();
  const r = status?.run;

  if (!r || !isRunning(status)) {
    return (
      <main className="page">
        <h1>Nothing watering</h1>
        {r && r.entries.length > 0 && (
          <section>
            <h2 className="label">Last run{r.schedule ? `: ${r.schedule}` : ''}</h2>
            <div className="list">
              {r.entries.map((e, i) => (
                <div key={i} className="list-row">
                  <span className="grow">{zoneName(e.zone)}</span>
                  <span className="muted small">{e.duration_minutes} min · {OUTCOME[e.outcome]}</span>
                </div>
              ))}
            </div>
          </section>
        )}
        <Link className="btn primary big block" to="/water">Water a zone</Link>
      </main>
    );
  }

  const current = r.entries[r.current_index];
  const remaining = status?.zones?.remaining_seconds ?? r.remaining_seconds;
  const total = current.duration_minutes * 60;
  const pct = r.state === 'running' ? Math.min(100, Math.max(0, ((total - remaining) / total) * 100)) : 0;
  const upNext = r.entries.slice(r.current_index + 1).filter((e) => e.outcome === 'pending');
  let t = Date.now() + remaining * 1000;
  const startsAt = upNext.map((e) => { const at = new Date(t); t += e.duration_minutes * 60_000; return at; });
  const doneAt = new Date(t);

  return (
    <main className="page">
      <section className="running" aria-live="polite">
        <div className="small" style={{ fontWeight: 600, opacity: 0.9 }}>
          {r.state === 'running' ? 'Watering now' : 'Starting'}{r.schedule ? ` · ${r.schedule}` : ''}
        </div>
        <h1 style={{ marginTop: 6, fontSize: 30 }}>{zoneName(current.zone)}</h1>
        <div className="row" style={{ alignItems: 'baseline', gap: 8, marginTop: 18 }}>
          <span className="count">{r.state === 'running' ? formatCountdown(remaining) : '…'}</span>
          <span style={{ opacity: 0.9 }}>left of {current.duration_minutes} min</span>
        </div>
        <div className="bar" style={{ marginTop: 16 }} role="progressbar" aria-valuenow={Math.round(pct)} aria-valuemin={0} aria-valuemax={100}>
          <div style={{ width: `${pct}%` }} />
        </div>
        <div className="row" style={{ marginTop: 20 }}>
          <button type="button" className="btn grow" style={{ color: '#8f1f12', borderColor: '#fff' }} disabled={busy} onClick={() => run(esp.cancel)}>Stop all</button>
          {upNext.length > 0 && (
            <button type="button" className="btn grow" style={{ background: 'transparent', color: '#fff', borderColor: 'rgba(255,255,255,.7)' }} disabled={busy} onClick={() => run(esp.skip)}>Skip to next</button>
          )}
        </div>
      </section>
      <ErrorNote message={error} />

      <section className="list" aria-label="Up next">
        {upNext.length > 0 && <h2 id="nx" style={{ padding: '14px 16px 10px', fontSize: 16 }}>Up next</h2>}
        {upNext.map((e, i) => (
          <div key={i} className="list-row">
            <span className="grow" style={{ fontWeight: 600 }}>{zoneName(e.zone)}</span>
            <span className="muted small">{e.duration_minutes} min · about {formatTimeOfDay(startsAt[i])}</span>
          </div>
        ))}
        <Link className="list-row" to="/water" style={{ color: 'var(--accent)', fontWeight: 600 }}>+ Add another zone</Link>
      </section>

      <p className="muted small" style={{ textAlign: 'center', margin: 0 }}>
        All done about {formatTimeOfDay(doneAt)}. Safe to close this page.
      </p>
    </main>
  );
}
