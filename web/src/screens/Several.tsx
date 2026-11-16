// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { esp } from '../api/esp';
import { Tick } from '../components/Icons';
import { Back, ErrorNote, Stepper } from '../components/ui';
import { formatMinutes, formatTimeOfDay } from '../lib/durations';
import { useAction, useController } from '../state';

export default function Several() {
  const { zones, schedules } = useController();
  const [picked, setPicked] = useState<Map<number, number>>(new Map());
  const [scheduleName, setScheduleName] = useState('');
  const { busy, error, run } = useAction();
  const navigate = useNavigate();

  const toggle = (zone: number) => {
    const next = new Map(picked);
    if (next.has(zone)) next.delete(zone);
    else next.set(zone, 10);
    setPicked(next);
  };
  const setMinutes = (zone: number, m: number) => setPicked(new Map(picked).set(zone, m));

  const items = zones.filter((z) => picked.has(z.zone)).map((z) => ({ zone: z.zone, duration_minutes: picked.get(z.zone)! }));
  const total = items.reduce((s, i) => s + i.duration_minutes, 0);

  const start = async () => {
    if (await run(() => esp.runItems(items))) navigate('/watering');
  };
  const runSchedule = async () => {
    if (scheduleName && (await run(() => esp.runSchedule(scheduleName)))) navigate('/watering');
  };

  return (
    <main className="page">
      <header>
        <Back to="/water">One zone</Back>
        <h1 style={{ marginTop: 4 }}>Water several zones</h1>
        <p className="muted" style={{ margin: '6px 0 0' }}>They run one after another, in this order.</p>
      </header>

      <div className="list">
        {zones.map((z) => {
          const on = picked.has(z.zone);
          return (
            <div key={z.zone} className="list-row" style={{ minHeight: 60 }}>
              <label className="row grow" style={{ minHeight: 44, cursor: 'pointer' }}>
                <input type="checkbox" checked={on} onChange={() => toggle(z.zone)} style={{ position: 'absolute', opacity: 0, width: 1, height: 1 }} />
                <span className={'check' + (on ? ' on' : '')}>{on && <Tick />}</span>
                <span className="grow" style={{ fontWeight: on ? 600 : 400 }}>{z.name}</span>
              </label>
              {on && <Stepper value={picked.get(z.zone)!} onChange={(m) => setMinutes(z.zone, m)} label={`minutes for ${z.name}`} />}
            </div>
          );
        })}
      </div>

      <ErrorNote message={error} />
      <button type="button" className="btn primary big block" style={{ flexDirection: 'column', gap: 0, minHeight: 64 }} disabled={busy || items.length === 0} onClick={start}>
        <span>{items.length ? `Start ${items.length} zone${items.length > 1 ? 's' : ''}` : 'Pick some zones'}</span>
        {items.length > 0 && (
          <span style={{ fontSize: 13, fontWeight: 500, opacity: 0.9 }}>
            {formatMinutes(total)} · done about {formatTimeOfDay(new Date(Date.now() + total * 60_000))}
          </span>
        )}
      </button>

      {schedules && schedules.schedules.length > 0 && (
        <section className="card" aria-labelledby="rs">
          <h2 id="rs" style={{ fontSize: 15 }}>Or run a whole schedule now</h2>
          <div className="row" style={{ marginTop: 10 }}>
            <select className="input grow" aria-label="Schedule" value={scheduleName} onChange={(e) => setScheduleName(e.target.value)}>
              <option value="">Choose…</option>
              {schedules.schedules.map((s) => <option key={s.name} value={s.name}>{s.name}</option>)}
            </select>
            <button type="button" className="btn" disabled={busy || !scheduleName} onClick={runSchedule}>Run</button>
          </div>
        </section>
      )}
    </main>
  );
}
