// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { useState } from 'react';
import { esp } from '../api/esp';
import type { Zone } from '../api/types';
import { Pencil } from '../components/Icons';
import { Back, ConfirmSheet, ErrorNote } from '../components/ui';
import { isValidName, MAX_ZONE, MIN_ZONE } from '../lib/limits';
import { useAction, useController } from '../state';

interface Editing { original: number | null; zone: number; name: string }

export default function Zones() {
  const { zones, schedules } = useController();
  const [editing, setEditing] = useState<Editing | null>(null);
  const [confirmRemove, setConfirmRemove] = useState<number | null>(null);
  const { busy, error, setError, run } = useAction();

  const usedBy = (zone: number) => (schedules?.schedules ?? []).filter((s) => s.items.some((i) => i.zone === zone)).map((s) => s.name);
  const free = Array.from({ length: MAX_ZONE - MIN_ZONE + 1 }, (_, i) => i + MIN_ZONE).filter(
    (n) => !zones.some((z) => z.zone === n) || n === editing?.original,
  );

  const save = async (next: Zone[]) => {
    const sorted = [...next].sort((a, b) => a.zone - b.zone);
    return run(() => esp.putZones(sorted));
  };

  const done = async () => {
    if (!editing) return;
    const name = editing.name.trim();
    const next = editing.original === null
      ? [...zones, { zone: editing.zone, name }]
      : zones.map((z) => (z.zone === editing.original ? { zone: editing.zone, name } : z));
    if (await save(next)) setEditing(null);
  };

  const remove = async (zone: number) => {
    if (await save(zones.filter((z) => z.zone !== zone))) {
      setConfirmRemove(null);
      setEditing(null);
    }
  };

  const editor = (e: Editing) => {
    const used = e.original !== null ? usedBy(e.original) : [];
    return (
      <div key={`edit-${e.original ?? 'new'}`} style={{ background: '#eef4fb', padding: '12px 14px 16px', display: 'flex', flexDirection: 'column', gap: 12 }}>
        <div className="row">
          <div className="field" style={{ width: 90 }}>
            <label htmlFor="znum">Zone</label>
            <select id="znum" className="input" value={e.zone} disabled={used.length > 0} onChange={(ev) => setEditing({ ...e, zone: Number(ev.target.value) })}>
              {free.map((n) => <option key={n} value={n}>{n}</option>)}
            </select>
          </div>
          <div className="field grow">
            <label htmlFor="zname">Name</label>
            <input id="zname" className="input" autoFocus value={e.name} maxLength={32} onChange={(ev) => setEditing({ ...e, name: ev.target.value })} />
          </div>
        </div>
        <div className="row" style={{ flexWrap: 'wrap', gap: 8 }}>
          {e.original !== null && <button type="button" className="btn small" disabled={busy} onClick={() => run(() => esp.startZone(e.original!, 1))}>Test 1 min</button>}
          {e.original !== null && <button type="button" className="btn small danger-outline" disabled={busy || used.length > 0} onClick={() => { setError(null); setConfirmRemove(e.original); }}>Remove</button>}
          <button type="button" className="btn small" onClick={() => { setEditing(null); setError(null); }}>Cancel</button>
          <button type="button" className="btn small primary" style={{ marginLeft: 'auto' }} disabled={busy || !isValidName(e.name)} onClick={done}>Done</button>
        </div>
        {used.length > 0 && <div className="muted tiny">In {used.join(', ')}</div>}
        <ErrorNote message={confirmRemove === null ? error : null} />
      </div>
    );
  };

  return (
    <main className="page">
      <header>
        <Back to="/details">Controller details</Back>
        <h1 style={{ marginTop: 4, fontSize: 24 }}>Zones</h1>
      </header>

      <div className="list">
        {zones.map((z) =>
          editing?.original === z.zone ? editor(editing) : (
            <div key={z.zone} className="list-row" style={{ paddingRight: 4 }}>
              <span style={{ width: 30, height: 30, borderRadius: 8, background: '#eef0ec', fontSize: 14, fontWeight: 700, display: 'flex', alignItems: 'center', justifyContent: 'center', flex: 'none' }}>{z.zone}</span>
              <span className="grow" style={{ fontWeight: 600 }}>{z.name}</span>
              <button type="button" className="icon-btn" aria-label={`Edit ${z.name}`} onClick={() => { setError(null); setEditing({ original: z.zone, zone: z.zone, name: z.name }); }}><Pencil /></button>
            </div>
          ),
        )}
        {editing?.original === null && editor(editing)}
        {!editing && zones.length < MAX_ZONE && (
          <button type="button" className="list-row" style={{ color: 'var(--accent)', fontWeight: 600 }} onClick={() => { setError(null); setEditing({ original: null, zone: free[0], name: '' }); }}>+ Add zone</button>
        )}
      </div>
      <p className="muted tiny" style={{ margin: '0 4px' }}>Up to {MAX_ZONE} zones. Zones in a schedule can't be removed.</p>

      {confirmRemove !== null && (
        <ConfirmSheet
          title={`Remove zone ${confirmRemove}?`}
          buttons={[{ label: 'Remove', kind: 'danger', onClick: () => remove(confirmRemove), disabled: busy }]}
          onClose={() => setConfirmRemove(null)}
          error={error}
        />
      )}
    </main>
  );
}
