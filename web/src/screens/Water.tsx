// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { Fragment, useState } from 'react';
import { Link, useNavigate, useSearchParams } from 'react-router-dom';
import { esp } from '../api/esp';
import { ErrorNote, Stepper } from '../components/ui';
import { formatTimeOfDay } from '../lib/durations';
import { isRunning } from '../lib/health';
import { useAction, useController } from '../state';

const PRESETS = [5, 10, 15, 20, 30];

export default function Water() {
  const { zones, status, zoneName } = useController();
  const [params, setParams] = useSearchParams();
  const selected = Number(params.get('zone')) || null;
  const [minutes, setMinutes] = useState(10);
  const [custom, setCustom] = useState(false);
  const { busy, error, run } = useAction();
  const navigate = useNavigate();

  const running = isRunning(status);
  const current = running && status?.run ? status.run.entries[status.run.current_index] : null;
  const endsAt = new Date(Date.now() + minutes * 60_000);

  const start = async () => {
    if (!selected) return;
    if (await run(() => esp.startZone(selected, minutes))) navigate('/watering');
  };

  return (
    <main className="page">
      <header className="spread" style={{ alignItems: 'baseline' }}>
        <h1>Water a zone</h1>
        <Link to="/water/several" style={{ fontWeight: 600, fontSize: 15 }}>Several zones</Link>
      </header>

      {zones.length === 0 && <p className="muted">No zones set up yet. <Link to="/zones">Set up zones</Link></p>}

      <div className="list" role="radiogroup" aria-label="Zone">
        {zones.map((z) => {
          const on = z.zone === selected;
          return (
            <Fragment key={z.zone}>
              <button
                type="button"
                role="radio"
                aria-checked={on}
                className="list-row"
                style={on ? { background: '#eef4fb', fontWeight: 700 } : undefined}
                onClick={() => setParams(on ? {} : { zone: String(z.zone) }, { replace: true })}
              >
                <span className={'radio' + (on ? ' on' : '')} />
                <span className="grow">{z.name}</span>
              </button>
              {on && (
                <div style={{ background: '#eef4fb', padding: '4px 16px 16px', borderTop: 'none' }}>
                  <div className="small" style={{ fontWeight: 600, marginBottom: 8 }}>How long?</div>
                  {custom ? (
                    <div className="row"><Stepper value={minutes} onChange={setMinutes} label="minutes" /><span className="muted small">min</span></div>
                  ) : (
                    <div className="chips">
                      {PRESETS.map((p) => (
                        <button key={p} type="button" className="chip" aria-pressed={minutes === p} onClick={() => setMinutes(p)}>{p}</button>
                      ))}
                    </div>
                  )}
                  <div className="muted tiny" style={{ marginTop: 6 }}>
                    minutes · <button type="button" className="link-btn" style={{ minHeight: 0, fontSize: 13 }} onClick={() => setCustom(!custom)}>{custom ? 'presets' : 'other amount'}</button>
                  </div>
                  <ErrorNote message={error} />
                  <button type="button" className="btn primary big block" style={{ marginTop: 12 }} disabled={busy} onClick={start}>
                    {current ? `Start ${minutes} min after ${zoneName(current.zone)}` : `Start ${minutes} minutes`}
                  </button>
                  {!current && (
                    <div className="muted tiny" style={{ marginTop: 8, textAlign: 'center' }}>Turns off by itself at {formatTimeOfDay(endsAt)}.</div>
                  )}
                </div>
              )}
            </Fragment>
          );
        })}
      </div>
    </main>
  );
}
