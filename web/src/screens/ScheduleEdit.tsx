// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { useEffect, useMemo, useState } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import { esp } from '../api/esp';
import type { Schedule } from '../api/types';
import { Close } from '../components/Icons';
import { Back, ConfirmSheet, ErrorNote, Stepper } from '../components/ui';
import { describeDays, formatDays, parseDays, sharedPattern, toggleDay, WEEKDAYS, WEEKDAY_LETTER, WEEKDAY_LONG, type DayPattern } from '../lib/days';
import { formatClock, formatMinutes } from '../lib/durations';
import { isValidName } from '../lib/limits';
import { minutesPerWeek, moveItem, nextRun, relativeDay, scaleSchedule, setAllDays } from '../lib/schedule';
import { useAction, useController } from '../state';

const DEFAULT_MINUTES = 10;

function DayPicker({ value, onChange, idPrefix }: { value: string; onChange: (days: string) => void; idPrefix: string }) {
  const p = parseDays(value);
  // "Pick days" stays selected while picking, even if every day or none is picked.
  const [picking, setPicking] = useState(p.kind === 'days');
  const mode = picking ? 'days' : p.kind;
  const pick = (m: string) => {
    setPicking(m === 'days');
    if (m === 'all') onChange('ALL');
    else if (m === 'eo') onChange('EO');
    else if (m === 'none') onChange('NONE');
    else if (p.kind === 'eo') onChange('NONE');
  };
  const days: DayPattern = p.kind === 'all' ? { kind: 'days', days: [...WEEKDAYS] } : p;
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
      <div className="seg" role="group" aria-label="Day pattern">
        {[['days', 'Pick days'], ['all', 'Every day'], ['eo', 'Alternate'], ['none', 'Never']].map(([k, label]) => (
          <button key={k} type="button" aria-pressed={mode === k} onClick={() => pick(k)}>{label}</button>
        ))}
      </div>
      {mode === 'days' && (
        <div className="days">
          {WEEKDAYS.map((d) => (
            <button
              key={d}
              id={`${idPrefix}-${d}`}
              type="button"
              className="day"
              aria-label={WEEKDAY_LONG[d]}
              aria-pressed={days.kind === 'days' && days.days.includes(d)}
              onClick={() => onChange(formatDays(toggleDay(days, d)))}
            >
              {WEEKDAY_LETTER[d]}
            </button>
          ))}
        </div>
      )}
    </div>
  );
}

export default function ScheduleEdit() {
  const { name: param } = useParams();
  const isNew = param === undefined;
  const { schedules, zones, config, zoneName, refresh } = useController();
  const navigate = useNavigate();
  const { busy, error, setError, run } = useAction();

  const original = useMemo(
    () => (isNew ? null : schedules?.schedules.find((s) => s.name === param) ?? null),
    [isNew, param, schedules],
  );
  const [draft, setDraft] = useState<Schedule | null>(null);
  const [perZone, setPerZone] = useState(false);
  const [confirmDelete, setConfirmDelete] = useState(false);
  const [baseDays, setBaseDays] = useState('ALL');

  useEffect(() => {
    if (draft || !schedules) return;
    if (isNew) {
      setDraft({ name: '', items: [] });
    } else if (original) {
      setDraft(structuredClone(original));
      setPerZone(original.items.length > 0 && sharedPattern(original.items.map((i) => i.days)) === null);
    }
  }, [draft, schedules, isNew, original]);

  if (!schedules || !config) return <main className="page no-tabs"><p className="muted">Loading…</p></main>;
  if (!isNew && !original && !draft) {
    return <main className="page no-tabs"><Back to="/schedules">Schedules</Back><p>That schedule no longer exists.</p></main>;
  }
  if (!draft) return null;

  const isActive = !isNew && schedules.active === param;
  const sharedDays = sharedPattern(draft.items.map((i) => i.days)) ?? baseDays;
  const newDays = sharedDays;
  const setNewDays = (d: string) => { setBaseDays(d); setDraft(setAllDays(draft, d)); };
  const unused = zones.filter((z) => !draft.items.some((i) => i.zone === z.zone));
  const dirty = isNew || JSON.stringify(draft) !== JSON.stringify(original);
  const nameTaken = schedules.schedules.some((s) => s.name === draft.name.trim() && s.name !== param);
  const nameOk = isValidName(draft.name) && !nameTaken;
  const next = nextRun(draft, config.start_hour, config.start_minute, new Date());

  const update = (i: number, patch: Partial<Schedule['items'][number]>) =>
    setDraft({ ...draft, items: draft.items.map((it, j) => (j === i ? { ...it, ...patch } : it)) });
  const addZone = (zone: number) =>
    setDraft({ ...draft, items: [...draft.items, { zone, days: perZone ? 'NONE' : newDays, duration_minutes: DEFAULT_MINUTES }] });

  const save = async () => {
    const clean: Schedule = { ...draft, name: draft.name.trim() };
    const list = isNew
      ? [...schedules.schedules, clean]
      : schedules.schedules.map((s) => (s.name === param ? clean : s));
    const renamingActive = isActive && clean.name !== param;
    const ok = await run(async () => {
      // The device refuses a save that drops the active schedule's name, so a
      // rename of the active one clears it, saves, then points it at the new name.
      if (renamingActive) await esp.setActive(null);
      try {
        await esp.putSchedules(list);
      } catch (e) {
        if (renamingActive) await esp.setActive(param!).catch(() => undefined);
        throw e;
      }
      if (renamingActive) await esp.setActive(clean.name);
    });
    if (!ok) return;
    // Wait for the saved schedules to arrive before leaving, so the editor
    // compares the draft against the new stored copy rather than the old one.
    await refresh();
    navigate(`/schedules/${encodeURIComponent(clean.name)}`, { replace: true });
  };

  const remove = async () => {
    const ok = await run(() => esp.putSchedules(schedules.schedules.filter((s) => s.name !== param)));
    if (ok) navigate('/schedules', { replace: true });
  };

  return (
    <main className="page no-tabs">
      <header className="spread">
        <Back to="/schedules">Schedules</Back>
        {isActive && <span className="badge">IN USE</span>}
      </header>

      <div className="field">
        <label htmlFor="sname" className="label" style={{ margin: '0 4px' }}>Name</label>
        <input
          id="sname"
          className="input"
          style={{ fontSize: 20, fontWeight: 700, minHeight: 52 }}
          value={draft.name}
          maxLength={32}
          placeholder="e.g. Summer"
          onChange={(e) => setDraft({ ...draft, name: e.target.value })}
        />
        {nameTaken && <span className="tiny" style={{ color: 'var(--warn)' }}>Another schedule has that name</span>}
      </div>

      {!perZone && (
        <section aria-labelledby="dy">
          <h2 id="dy" className="label">Which days</h2>
          <div className="card" style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
            <DayPicker value={newDays} onChange={setNewDays} idPrefix="all" />
            <div className="muted small">
              All zones use these days.{' '}
              <button type="button" className="link-btn" style={{ minHeight: 0, fontSize: 14 }} onClick={() => setPerZone(true)}>Set days zone by zone</button>
            </div>
          </div>
        </section>
      )}

      <section aria-labelledby="zs">
        <div className="spread" style={{ marginBottom: 8 }}>
          <h2 id="zs" className="label" style={{ margin: '0 4px' }}>Zones, in order</h2>
          {draft.items.length > 0 && (
            <div className="row" style={{ gap: 6 }} role="group" aria-label="Adjust all durations">
              <button type="button" className="btn small" onClick={() => setDraft(scaleSchedule(draft, 90))}>−10%</button>
              <button type="button" className="btn small" onClick={() => setDraft(scaleSchedule(draft, 110))}>+10%</button>
            </div>
          )}
        </div>
        <div className="list">
          {draft.items.map((it, i) => (
            <div key={it.zone} style={{ padding: perZone ? '4px 0 12px' : 0 }}>
              <div className="list-row" style={{ padding: '4px 4px 4px 8px', minHeight: 64 }}>
                <div className="ord">
                  <button type="button" aria-label={`Move ${zoneName(it.zone)} up`} disabled={i === 0} onClick={() => setDraft({ ...draft, items: moveItem(draft.items, i, i - 1) })}>▲</button>
                  <button type="button" aria-label={`Move ${zoneName(it.zone)} down`} disabled={i === draft.items.length - 1} onClick={() => setDraft({ ...draft, items: moveItem(draft.items, i, i + 1) })}>▼</button>
                </div>
                <span className="grow" style={{ fontWeight: 600, fontSize: 15 }}>{zoneName(it.zone)}</span>
                <Stepper value={it.duration_minutes} onChange={(m) => update(i, { duration_minutes: m })} label={`minutes for ${zoneName(it.zone)}`} />
                <button type="button" className="icon-btn" aria-label={`Remove ${zoneName(it.zone)}`} onClick={() => setDraft({ ...draft, items: draft.items.filter((_, j) => j !== i) })}><Close /></button>
              </div>
              {perZone && (
                <div style={{ padding: '0 12px' }}>
                  <DayPicker value={it.days} onChange={(d) => update(i, { days: d })} idPrefix={`z${it.zone}`} />
                </div>
              )}
            </div>
          ))}
          {unused.map((z) => (
            <button key={z.zone} type="button" className="list-row" style={{ color: 'var(--accent)', fontWeight: 600, fontSize: 15 }} onClick={() => addZone(z.zone)}>+ Add {z.name}</button>
          ))}
        </div>
        <p className="muted tiny" style={{ margin: '8px 4px 0' }}>Minutes per zone, 1 to 240.</p>
        {perZone && (
          <button type="button" className="link-btn" onClick={() => { setPerZone(false); setDraft(setAllDays(draft, sharedDays)); }}>Use the same days for every zone</button>
        )}
      </section>

      {draft.items.length > 0 && (
        <section className="card" style={{ background: 'var(--accent-soft)', borderColor: '#c9dcef' }}>
          <div style={{ fontWeight: 700, color: 'var(--accent-ink)' }}>
            {!perZone ? `${describeDays(parseDays(sharedDays))} · ` : ''}{formatMinutes(minutesPerWeek(draft))} a week
          </div>
          <div className="small" style={{ marginTop: 4, color: '#23405e' }}>
            Starts {formatClock(config.start_hour, config.start_minute)}.
            {next ? ` Next: ${relativeDay(next.date, new Date())}, ${formatMinutes(next.minutes)}.` : ' Never runs on its own.'}
          </div>
        </section>
      )}

      {!isNew && (
        <div className="row">
          <button type="button" className="btn grow" disabled={busy || dirty} onClick={async () => { if (await run(() => esp.runSchedule(param!))) navigate('/watering'); }}>Run now</button>
          <button type="button" className="btn grow danger-outline" disabled={busy || isActive} onClick={() => { setError(null); setConfirmDelete(true); }}>{isActive ? 'In use' : 'Delete'}</button>
        </div>
      )}

      <ErrorNote message={!confirmDelete ? error : null} />

      {dirty && (
        <div className="savebar">
          <span className="grow small">{isNew ? 'New schedule' : 'Unsaved changes'}</span>
          {!isNew && <button type="button" className="btn small" style={{ background: 'transparent', color: '#fff', borderColor: '#6b736e' }} onClick={() => setDraft(structuredClone(original!))}>Undo</button>}
          <button type="button" className="btn small primary" style={{ background: '#6fa8dc', borderColor: '#6fa8dc', color: '#0e1a26' }} disabled={busy || !nameOk || draft.items.length === 0} onClick={save}>Save</button>
        </div>
      )}

      {confirmDelete && (
        <ConfirmSheet
          title={`Delete ${param}?`}
          buttons={[{ label: 'Delete', kind: 'danger', onClick: remove, disabled: busy }]}
          onClose={() => setConfirmDelete(false)}
          error={error}
        />
      )}
    </main>
  );
}
