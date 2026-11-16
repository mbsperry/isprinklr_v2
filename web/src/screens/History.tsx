// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { esp } from '../api/esp';
import { server } from '../api/server';
import { Chevron, SmallBang, SmallDash, SmallTick } from '../components/Icons';
import { formatTimeOfDay } from '../lib/durations';
import { groupByDay, type HistoryDay, type LogLine } from '../lib/history';
import { relativeDay } from '../lib/schedule';
import { useController } from '../state';

const DAYS_BACK = 14;

export default function History() {
  const { zones } = useController();
  const [lines, setLines] = useState<LogLine[] | null>(null);
  const [source, setSource] = useState<'server' | 'device' | 'none'>('server');

  useEffect(() => {
    let cancelled = false;
    const since = new Date(Date.now() - DAYS_BACK * 86_400_000);
    server
      .logs({ level: 'info', modules: ['run', 'sched'], since, limit: 2000 })
      .then((l) => { if (!cancelled) { setLines(l); setSource('server'); } })
      .catch(() =>
        esp.logs('info')
          .then((l) => { if (!cancelled) { setLines(l); setSource('device'); } })
          .catch(() => { if (!cancelled) { setLines([]); setSource('none'); } }),
      );
    return () => { cancelled = true; };
  }, []);

  const days: HistoryDay[] = lines ? groupByDay(lines, zones) : [];
  const now = new Date();
  const problems = days.filter((d) => d.tone === 'warn').length;

  return (
    <main className="page">
      <h1>History</h1>

      {lines === null && <p className="muted">Loading…</p>}
      {source === 'device' && <p className="muted small" style={{ margin: 0 }}>Log server unavailable. Showing the controller's recent lines.</p>}
      {source === 'none' && <p className="muted">No history available right now.</p>}

      {days.length > 0 && (
        <div className="row" style={{ gap: 10 }}>
          <div className="card grow" style={{ padding: '12px 14px' }}><div style={{ fontSize: 22, fontWeight: 700 }}>{days.length}</div><div className="muted tiny">days with activity</div></div>
          <div className="card grow" style={{ padding: '12px 14px' }}><div style={{ fontSize: 22, fontWeight: 700 }}>{problems}</div><div className="muted tiny">needed a look</div></div>
        </div>
      )}
      {lines && days.length === 0 && source !== 'none' && <p className="muted">Nothing has watered in the last {DAYS_BACK} days.</p>}

      {days.length > 0 && (
        <div className="list">
          {days.map((d) => (
            <div key={d.day} className="row" style={{ alignItems: 'flex-start', padding: '14px 16px' }}>
              <span className={'tone ' + d.tone} aria-hidden="true">{d.tone === 'ok' ? <SmallTick /> : d.tone === 'warn' ? <SmallBang /> : <SmallDash />}</span>
              <div className="grow">
                <div style={{ fontWeight: 700 }}>{relativeDay(d.date, now)}</div>
                <ul style={{ listStyle: 'none', margin: '4px 0 0', padding: 0, display: 'flex', flexDirection: 'column', gap: 4 }}>
                  {d.items.map((it, i) => (
                    <li key={i} className="small" style={{ display: 'flex', gap: 8, color: it.tone === 'warn' ? 'var(--warn)' : undefined }}>
                      <span className="muted" style={{ flex: 'none', width: 64 }}>{formatTimeOfDay(it.time)}</span>
                      <span>{it.text}</span>
                    </li>
                  ))}
                </ul>
              </div>
            </div>
          ))}
        </div>
      )}

      <Link className="card spread" to="/details" style={{ textDecoration: 'none', color: 'var(--ink)', fontWeight: 600 }}>
        Controller details and full logs <span className="muted"><Chevron /></span>
      </Link>
    </main>
  );
}
