// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { describe, expect, it } from 'vitest';
import type { Status } from '../api/types';
import { assessHealth } from './health';
import { groupByDay, nameZones, storyText } from './history';

const good: Status = {
  time_valid: true,
  settings: { load: 'loaded', zones: 8, schedules: 5, active_schedule: 'Late Fall', enabled: true, start_time: '07:00' },
  run: { state: 'idle', schedule: 'Late Fall', current_index: 0, remaining_seconds: 0, entries: [] },
};

describe('home status', () => {
  it('is all good when nothing is wrong', () => {
    expect(assessHealth(good, true)).toMatchObject({ ok: true, headline: 'All good' });
  });

  it('puts an unreachable controller first', () => {
    expect(assessHealth(good, false).headline).toBe("Can't reach the sprinkler controller");
  });

  it('flags the scheduler being off', () => {
    const off = { ...good, settings: { ...good.settings!, enabled: false } };
    expect(assessHealth(off, true)).toMatchObject({ ok: false, headline: 'Automatic watering is off', action: 'schedules' });
  });

  it('flags a failed last run but not a cancelled one', () => {
    const entry = { zone: 1, duration_minutes: 5, origin: 'schedule' as const };
    const failed = { ...good, run: { ...good.run!, entries: [{ ...entry, outcome: 'failed' as const }] } };
    const cancelled = { ...good, run: { ...good.run!, entries: [{ ...entry, outcome: 'interrupted' as const }] } };
    expect(assessHealth(failed, true).headline).toBe('Last watering stopped early');
    expect(assessHealth(cancelled, true).ok).toBe(true);
  });

  it('flags an invalid clock', () => {
    expect(assessHealth({ ...good, time_valid: false }, true).ok).toBe(false);
  });
});

describe('history', () => {
  const zones = [{ zone: 3, name: 'Back lawn' }];

  it('names zones', () => {
    expect(nameZones('zone 3 finished: ran its full 20 min', zones)).toBe('Back lawn finished: ran its full 20 min');
    expect(nameZones('zone 13 finished', zones)).toBe('zone 13 finished');
  });

  it('groups story lines by day, newest first, and marks problems', () => {
    const days = groupByDay([
      { time: '2026-11-13T15:24:00Z', level: 'info', module: 'run', message: 'zone 3 cut short by skip after 4 min of 15 min (scheduled)' },
      { time: '2026-11-13T15:00:00Z', level: 'info', module: 'run', message: "queued 6 zones (scheduled, 'Late Fall'); starting now" },
      { time: '2026-11-10T15:50:00Z', level: 'info', module: 'run', message: "run 'Late Fall' finished: 6 completed" },
      { time: '2026-11-10T15:50:00Z', level: 'debug', module: 'run', message: 'bus busy; zone 1 start attempt 1 not taken, retrying' },
      { time: '2026-11-10T15:50:00Z', level: 'info', module: 'web', message: 'GET /api/status' },
    ], zones);
    expect(days).toHaveLength(2);
    expect(days[0].tone).toBe('ok');
    expect(days[0].items).toHaveLength(1);
    expect(days[0].items[0].text).toBe('Back lawn cut short by skip after 4 min of 15 min');
    expect(days[1].items[0].text).toBe('Late Fall finished: 6 completed');
  });

  it('shortens and hides routine lines, and flags real problems', () => {
    expect(storyText("zone 3 finished: ran its full 20 min (scheduled, 'Late Fall')", zones)).toBeNull();
    expect(storyText("starting today's scheduled run 'Late Fall'", zones)).toBeNull();
    expect(storyText('zone 3 finished: ran its full 10 min (manual)', zones)).toBe('Back lawn watered 10 min');
    expect(storyText('scheduled run at 07:00 not started: the scheduler is off', zones)).toBe('Scheduled run at 07:00 not started: the scheduler is off');
    const failed = groupByDay([{ time: '2026-11-13T15:24:00Z', level: 'error', module: 'run', message: 'zone 3 not started: the bus refused it 5 times; abandoning the run' }], zones);
    expect(failed[0].tone).toBe('warn');
  });
});
