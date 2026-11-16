// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// Day patterns, as the device stores them: colon-joined weekdays
// (M, TU, W, TH, F, SA, SU, Monday first), or ALL, NONE, EO.
// See docs/api-contract.md "Day patterns".

export const WEEKDAYS = ['M', 'TU', 'W', 'TH', 'F', 'SA', 'SU'] as const;
export type Weekday = (typeof WEEKDAYS)[number];

export const WEEKDAY_LONG: Record<Weekday, string> = {
  M: 'Monday', TU: 'Tuesday', W: 'Wednesday', TH: 'Thursday',
  F: 'Friday', SA: 'Saturday', SU: 'Sunday',
};
export const WEEKDAY_LETTER: Record<Weekday, string> = {
  M: 'M', TU: 'T', W: 'W', TH: 'T', F: 'F', SA: 'S', SU: 'S',
};

export type DayPattern =
  | { kind: 'all' }
  | { kind: 'none' }
  | { kind: 'eo' }
  | { kind: 'days'; days: Weekday[] };

export function parseDays(raw: string): DayPattern {
  const s = raw.trim().toUpperCase();
  if (s === 'ALL') return { kind: 'all' };
  if (s === 'NONE' || s === '') return { kind: 'none' };
  if (s === 'EO') return { kind: 'eo' };
  const set = new Set(s.split(':').map((d) => d.trim()));
  const days = WEEKDAYS.filter((d) => set.has(d));
  return days.length ? { kind: 'days', days } : { kind: 'none' };
}

export function formatDays(p: DayPattern): string {
  switch (p.kind) {
    case 'all': return 'ALL';
    case 'none': return 'NONE';
    case 'eo': return 'EO';
    case 'days':
      if (p.days.length === 0) return 'NONE';
      if (p.days.length === 7) return 'ALL';
      return WEEKDAYS.filter((d) => p.days.includes(d)).join(':');
  }
}

export function toggleDay(p: DayPattern, day: Weekday): DayPattern {
  const current = p.kind === 'days' ? p.days : p.kind === 'all' ? [...WEEKDAYS] : [];
  const next = current.includes(day) ? current.filter((d) => d !== day) : [...current, day];
  if (next.length === 0) return { kind: 'none' };
  if (next.length === 7) return { kind: 'all' };
  return { kind: 'days', days: WEEKDAYS.filter((d) => next.includes(d)) };
}

/** Days since 1970-01-01 for a local calendar date. */
export function localDayNumber(d: Date): number {
  return Math.floor(Date.UTC(d.getFullYear(), d.getMonth(), d.getDate()) / 86_400_000);
}

/** JS getDay() is Sunday=0; map to our Monday-first list. */
export function weekdayOf(d: Date): Weekday {
  return WEEKDAYS[(d.getDay() + 6) % 7];
}

export function runsOn(p: DayPattern, d: Date): boolean {
  switch (p.kind) {
    case 'all': return true;
    case 'none': return false;
    case 'eo': return localDayNumber(d) % 2 === 0;
    case 'days': return p.days.includes(weekdayOf(d));
  }
}

function joinAnd(parts: string[]): string {
  if (parts.length <= 1) return parts.join('');
  return parts.slice(0, -1).join(', ') + ' and ' + parts[parts.length - 1];
}

/** "Tuesdays and Fridays", "Every day", "Every other day", "Never". */
export function describeDays(p: DayPattern): string {
  switch (p.kind) {
    case 'all': return 'Every day';
    case 'none': return 'Never on its own';
    case 'eo': return 'Every other day';
    case 'days': {
      const set = new Set(p.days);
      const weekdays: Weekday[] = ['M', 'TU', 'W', 'TH', 'F'];
      if (set.size === 5 && weekdays.every((d) => set.has(d))) return 'Weekdays';
      if (set.size === 2 && set.has('SA') && set.has('SU')) return 'Weekends';
      return joinAnd(p.days.map((d) => WEEKDAY_LONG[d] + 's'));
    }
  }
}

/** One pattern shared by every item, or null when they differ. */
export function sharedPattern(raw: string[]): string | null {
  if (raw.length === 0) return null;
  const canon = raw.map((r) => formatDays(parseDays(r)));
  return canon.every((c) => c === canon[0]) ? canon[0] : null;
}
