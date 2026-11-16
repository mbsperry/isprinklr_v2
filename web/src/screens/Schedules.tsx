// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { esp } from '../api/esp';
import type { Schedule } from '../api/types';
import { ConfirmSheet, ErrorNote } from '../components/ui';
import { describeDays, parseDays, sharedPattern, weekdayOf, WEEKDAYS, WEEKDAY_LETTER } from '../lib/days';
import { formatClock, formatMinutes } from '../lib/durations';
import { MAX_SCHEDULES } from '../lib/limits';
import { minutesPerWeek, nextRun, relativeDayInline } from '../lib/schedule';
import { useAction, useController } from '../state';

function daysText(s: Schedule): string {
  const shared = sharedPattern(s.items.map((i) => i.days));
  return shared ? describeDays(parseDays(shared)) : 'Days vary by zone';
}

function WeekStrip({ s }: { s: Schedule }) {
  // A week starting Monday; mark a day if any zone waters on it.
  const monday = new Date();
  monday.setDate(monday.getDate() - ((monday.getDay() + 6) % 7));
  const on = WEEKDAYS.map((_, i) => {
    const d = new Date(monday.getFullYear(), monday.getMonth(), monday.getDate() + i, 12);
    return s.items.some((it) => {
      const p = parseDays(it.days);
      return p.kind === 'all' || p.kind === 'eo' || (p.kind === 'days' && p.days.includes(weekdayOf(d)));
    });
  });
  return (
    <div aria-hidden="true" style={{ marginTop: 12 }}>
      <div className="row" style={{ gap: 4 }}>
        {on.map((x, i) => <span key={i} style={{ flex: 1, height: 6, borderRadius: 3, background: x ? 'var(--accent)' : 'var(--line)' }} />)}
      </div>
      <div className="row muted" style={{ gap: 4, marginTop: 4, fontSize: 11, fontWeight: 600 }}>
        {WEEKDAYS.map((d) => <span key={d} style={{ flex: 1, textAlign: 'center' }}>{WEEKDAY_LETTER[d]}</span>)}
      </div>
    </div>
  );
}

export default function Schedules() {
  const { schedules, config, status } = useController();
  const [confirm, setConfirm] = useState<null | 'off' | 'on'>(null);
  const [editingTime, setEditingTime] = useState(false);
  const [time, setTime] = useState('');
  const { busy, error, run, setError } = useAction();
  const navigate = useNavigate();

  if (!schedules || !config) return <main className="page"><h1>Schedules</h1><p className="muted">Loading…</p></main>;

  const enabled = schedules.enabled;
  const active = schedules.schedules.find((s) => s.name === schedules.active) ?? null;
  const others = schedules.schedules.filter((s) => s !== active);
  const next = active ? nextRun(active, config.start_hour, config.start_minute, new Date(), status?.trigger?.last_run_day) : null;

  const setEnabled = async (value: boolean) => {
    if (await run(() => esp.setEnabled(value))) setConfirm(null);
  };
  const saveTime = async () => {
    const [h, m] = time.split(':').map(Number);
    if (Number.isNaN(h) || Number.isNaN(m)) return;
    const ok = await run(() => esp.putConfig({ start_hour: h, start_minute: m, timezone: config.timezone }));
    if (ok) setEditingTime(false);
  };
  const isNever = (s: Schedule) => s.items.every((i) => parseDays(i.days).kind === 'none');

  return (
    <main className="page">
      <h1>Schedules</h1>

      <section className="card" aria-labelledby="aw">
        <div className="row">
          <div className="grow">
            <h2 id="aw">Automatic watering</h2>
            <div className="small" style={{ marginTop: 4, fontWeight: 600, color: enabled ? 'var(--ok)' : 'var(--warn)' }}>{enabled ? 'On' : 'Off'}</div>
          </div>
          <button type="button" role="switch" className="switch" aria-checked={enabled} aria-label="Automatic watering" onClick={() => { setError(null); setConfirm(enabled ? 'off' : 'on'); }} />
        </div>
        <div className="spread" style={{ marginTop: 14, paddingTop: 10, borderTop: '1px solid var(--line-soft)' }}>
          <span>Starts each day at</span>
          {editingTime ? (
            <span className="row" style={{ gap: 8 }}>
              <input className="input" type="time" aria-label="Start time" value={time} onChange={(e) => setTime(e.target.value)} style={{ width: 130 }} />
              <button type="button" className="btn small primary" disabled={busy} onClick={saveTime}>Save</button>
            </span>
          ) : (
            <button
              type="button"
              className="btn small"
              onClick={() => { setTime(`${String(config.start_hour).padStart(2, '0')}:${String(config.start_minute).padStart(2, '0')}`); setEditingTime(true); }}
            >
              {formatClock(config.start_hour, config.start_minute)}
            </button>
          )}
        </div>
      </section>
      {!confirm && <ErrorNote message={error} />}

      {active && (
        <section>
          <h2 className="label">In use</h2>
          <Link className="card" to={`/schedules/${encodeURIComponent(active.name)}`} style={{ display: 'block', textDecoration: 'none', color: 'var(--ink)', border: '2px solid var(--accent)' }}>
            <div className="spread">
              <span style={{ fontSize: 20, fontWeight: 700 }}>{active.name}</span>
              <span style={{ color: 'var(--accent)', fontWeight: 600 }}>Edit</span>
            </div>
            <div style={{ marginTop: 6 }}>{daysText(active)}</div>
            <div className="muted small" style={{ marginTop: 2 }}>
              {active.items.length} zones · {formatMinutes(minutesPerWeek(active))} a week
              {enabled && next ? ` · next ${relativeDayInline(next.date, new Date())}` : ''}
            </div>
            <WeekStrip s={active} />
          </Link>
        </section>
      )}

      {others.length > 0 && (
        <section>
          <h2 className="label">{active ? 'Others' : 'Schedules'}</h2>
          <div className="list">
            {others.map((s) => (
              <div key={s.name} className="list-row">
                <Link to={`/schedules/${encodeURIComponent(s.name)}`} className="grow" style={{ textDecoration: 'none', color: 'var(--ink)' }}>
                  <div style={{ fontWeight: 600 }}>{s.name}</div>
                  <div className="muted tiny">{isNever(s) ? 'Never on its own · run by hand' : `${daysText(s)} · ${formatMinutes(minutesPerWeek(s))} a week`}</div>
                </Link>
                {isNever(s) ? (
                  <button type="button" className="btn small" disabled={busy} onClick={async () => { if (await run(() => esp.runSchedule(s.name))) navigate('/watering'); }}>Run now</button>
                ) : (
                  <button type="button" className="btn small" disabled={busy} onClick={() => run(() => esp.setActive(s.name))}>Use this</button>
                )}
              </div>
            ))}
          </div>
        </section>
      )}

      {schedules.schedules.length < MAX_SCHEDULES && (
        <Link className="btn big block" to="/schedules/new" style={{ border: '1px dashed #9ba39e', background: 'transparent', color: 'var(--accent)' }}>+ New schedule</Link>
      )}

      {confirm === 'off' && (
        <ConfirmSheet
          title="Turn off automatic watering?"
          buttons={[{ label: 'Turn off', kind: 'danger', onClick: () => setEnabled(false), disabled: busy }]}
          cancelLabel="Keep it on"
          onClose={() => setConfirm(null)}
          error={error}
        />
      )}
      {confirm === 'on' && (
        <ConfirmSheet
          title="Turn on automatic watering?"
          buttons={[{ label: 'Turn on', kind: 'primary', onClick: () => setEnabled(true), disabled: busy }]}
          cancelLabel="Keep it off"
          onClose={() => setConfirm(null)}
          error={error}
        />
      )}
    </main>
  );
}
