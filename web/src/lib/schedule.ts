// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import type { Schedule } from '../api/types';
import { localDayNumber, parseDays, runsOn } from './days';
import { scaleMinutes } from './durations';

/** Minutes a schedule waters on a given date. */
export function minutesOn(s: Schedule, d: Date): number {
  return s.items
    .filter((i) => runsOn(parseDays(i.days), d))
    .reduce((sum, i) => sum + i.duration_minutes, 0);
}

export function zonesOn(s: Schedule, d: Date): number {
  return s.items.filter((i) => runsOn(parseDays(i.days), d)).length;
}

export interface NextRun { date: Date; minutes: number; zones: number }

/**
 * The next time the schedule will water, starting from `now`. Today counts only
 * if the start time is still ahead and the device has not already run today.
 * Looks two weeks ahead, which covers every weekly and every-other-day pattern.
 */
export function nextRun(
  s: Schedule,
  startHour: number,
  startMinute: number,
  now: Date,
  lastRunDay?: number,
): NextRun | null {
  for (let offset = 0; offset < 15; offset++) {
    const day = new Date(now.getFullYear(), now.getMonth(), now.getDate() + offset, startHour, startMinute);
    if (offset === 0 && (day <= now || lastRunDay === localDayNumber(now))) continue;
    const minutes = minutesOn(s, day);
    if (minutes > 0) return { date: day, minutes, zones: zonesOn(s, day) };
  }
  return null;
}

/** Total watering minutes per week, counting EO as 3.5 days. */
export function minutesPerWeek(s: Schedule): number {
  return s.items.reduce((sum, i) => {
    const p = parseDays(i.days);
    const perWeek = p.kind === 'all' ? 7 : p.kind === 'eo' ? 3.5 : p.kind === 'days' ? p.days.length : 0;
    return sum + i.duration_minutes * perWeek;
  }, 0);
}

export function scaleSchedule(s: Schedule, percent: number): Schedule {
  return { ...s, items: s.items.map((i) => ({ ...i, duration_minutes: scaleMinutes(i.duration_minutes, percent) })) };
}

export function setAllDays(s: Schedule, days: string): Schedule {
  return { ...s, items: s.items.map((i) => ({ ...i, days })) };
}

export function moveItem<T>(list: T[], from: number, to: number): T[] {
  if (to < 0 || to >= list.length || from === to) return list;
  const copy = list.slice();
  const [it] = copy.splice(from, 1);
  copy.splice(to, 0, it);
  return copy;
}

/** For use mid-sentence: "today", "tomorrow", else as relativeDay. */
export function relativeDayInline(d: Date, now: Date): string {
  const r = relativeDay(d, now);
  return r === 'Today' || r === 'Tomorrow' ? r.toLowerCase() : r;
}

/** "Tue Nov 17", or "Today"/"Tomorrow" relative to now. */
export function relativeDay(d: Date, now: Date): string {
  const diff = localDayNumber(d) - localDayNumber(now);
  if (diff === 0) return 'Today';
  if (diff === 1) return 'Tomorrow';
  if (diff > 1 && diff < 7) return d.toLocaleDateString('en-US', { weekday: 'long' });
  return d.toLocaleDateString('en-US', { weekday: 'short', month: 'short', day: 'numeric' });
}
