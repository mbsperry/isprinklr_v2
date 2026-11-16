// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { Link, useNavigate } from 'react-router-dom';
import { CalendarIcon, CheckCircle, Chevron, Warn } from '../components/Icons';
import { Connection } from '../components/ui';
import { describeDays, parseDays, sharedPattern } from '../lib/days';
import { formatClock, formatCountdown, formatTimeOfDay } from '../lib/durations';
import { assessHealth, isRunning } from '../lib/health';
import { nextRun, relativeDayInline } from '../lib/schedule';
import { useController } from '../state';

function partOfDay(d: Date): string {
  const h = d.getHours();
  const day = d.toLocaleDateString('en-US', { weekday: 'long' });
  return `${day} ${h < 12 ? 'morning' : h < 17 ? 'afternoon' : 'evening'}`;
}

export default function Home() {
  const { status, reachable, loaded, zones, schedules, config, refresh, zoneName } = useController();
  const navigate = useNavigate();
  const now = new Date();
  const health = assessHealth(status, reachable);
  const active = schedules?.schedules.find((s) => s.name === schedules.active) ?? null;
  const next = active && config && status?.settings?.enabled
    ? nextRun(active, config.start_hour, config.start_minute, now, status.trigger?.last_run_day)
    : null;
  const run = status?.run;
  const running = isRunning(status);
  const current = running && run ? run.entries[run.current_index] : null;

  const act = () => {
    if (health.action === 'retry') void refresh();
    else if (health.action === 'schedules') navigate('/schedules');
    else if (health.action === 'history') navigate('/history');
    else if (health.action === 'details') navigate('/details');
  };

  if (!loaded) return <main className="page"><p className="muted">Loading…</p></main>;

  const shared = active ? sharedPattern(active.items.map((i) => i.days)) : null;

  return (
    <main className="page">
      <header className="spread">
        <span className="muted" style={{ fontWeight: 600, fontSize: 15 }}>{partOfDay(now)}</span>
        <Connection />
      </header>

      <section className={'hero ' + (health.ok ? 'ok' : 'warn')} aria-live="polite">
        <div className="row">
          {health.ok ? <CheckCircle /> : <Warn />}
          <h1>{health.headline}</h1>
        </div>
        {health.ok && next && (
          <p>Next watering is <strong>{relativeDayInline(next.date, now)} at {formatTimeOfDay(next.date)}</strong>.</p>
        )}
        {health.action && (
          <button type="button" className="btn block" style={{ marginTop: 16 }} onClick={act}>{health.actionLabel}</button>
        )}
      </section>

      {current && run && (
        <Link to="/watering" className="card" style={{ textDecoration: 'none', color: 'var(--ink)', borderColor: 'var(--accent)', borderWidth: 2 }}>
          <div className="spread">
            <div>
              <div className="tiny" style={{ fontWeight: 700, color: 'var(--accent-ink)' }}>WATERING NOW</div>
              <div style={{ fontSize: 18, fontWeight: 700, marginTop: 2 }}>{zoneName(current.zone)}</div>
            </div>
            <span className="mono" style={{ fontSize: 24, fontWeight: 700, color: 'var(--accent-ink)' }}>
              {formatCountdown(status?.zones?.remaining_seconds ?? run.remaining_seconds)}
            </span>
          </div>
        </Link>
      )}

      {zones.length > 0 && (
        <section aria-labelledby="qw">
          <div className="spread" style={{ marginBottom: 10 }}>
            <h2 id="qw">Water a zone</h2>
            <Link to="/water/several" style={{ fontSize: 14, fontWeight: 600 }}>Several zones</Link>
          </div>
          <div className="zone-grid">
            {zones.map((z) => (
              <Link key={z.zone} className="zone-btn" to={`/water?zone=${z.zone}`}>{z.name}</Link>
            ))}
          </div>
        </section>
      )}

      {active && (
        <Link className="card row" to={`/schedules/${encodeURIComponent(active.name)}`} style={{ textDecoration: 'none', color: 'var(--ink)' }}>
          <span style={{ color: 'var(--accent)' }}><CalendarIcon /></span>
          <div className="grow">
            <div style={{ fontWeight: 700 }}>{active.name} schedule</div>
            <div className="muted small">
              {shared ? describeDays(parseDays(shared)) : 'Days vary by zone'}
              {config && ` at ${formatClock(config.start_hour, config.start_minute)}`}
            </div>
          </div>
          <span className="muted"><Chevron /></span>
        </Link>
      )}
    </main>
  );
}
