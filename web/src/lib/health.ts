// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// What the home screen says. One headline, at most one action. Order matters:
// the first problem found wins.

import type { Status } from '../api/types';

export type HealthAction = 'retry' | 'schedules' | 'history' | 'details' | null;

export interface Health {
  ok: boolean;
  headline: string;
  action: HealthAction;
  actionLabel?: string;
}

export function assessHealth(status: Status | null, reachable: boolean): Health {
  if (!reachable || !status) {
    return { ok: false, headline: "Can't reach the sprinkler controller", action: 'retry', actionLabel: 'Try again' };
  }
  const s = status.settings;
  if (s && s.load !== 'loaded') {
    return { ok: false, headline: 'Controller settings need attention', action: 'details', actionLabel: 'Details' };
  }
  if (status.time_valid === false) {
    return { ok: false, headline: "Clock not set, schedules on hold", action: 'details', actionLabel: 'Details' };
  }
  if (s && !s.enabled) {
    return { ok: false, headline: 'Automatic watering is off', action: 'schedules', actionLabel: 'Schedules' };
  }
  if (s && !s.active_schedule) {
    return { ok: false, headline: 'No schedule in use', action: 'schedules', actionLabel: 'Schedules' };
  }
  const run = status.run;
  if (run && run.state === 'idle' && run.entries.some((e) => e.outcome === 'failed')) {
    return { ok: false, headline: 'Last watering stopped early', action: 'history', actionLabel: 'What happened' };
  }
  if (status.recovery?.result === 'skipped') {
    return { ok: false, headline: 'Last watering stopped early', action: 'history', actionLabel: 'What happened' };
  }
  return { ok: true, headline: 'All good', action: null };
}

export function isRunning(status: Status | null): boolean {
  return !!status?.run && status.run.state !== 'idle';
}
