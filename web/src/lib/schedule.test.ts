// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { describe, expect, it } from 'vitest';
import type { Schedule } from '../api/types';
import { localDayNumber } from './days';
import { formatClock, formatCountdown, formatMinutes, scaleMinutes } from './durations';
import { minutesPerWeek, moveItem, nextRun, scaleSchedule } from './schedule';

const fall: Schedule = {
  name: 'Late Fall',
  items: [
    { zone: 1, days: 'TU:F', duration_minutes: 20 },
    { zone: 2, days: 'TU:F', duration_minutes: 15 },
    { zone: 3, days: 'F', duration_minutes: 10 },
  ],
};

describe('durations', () => {
  it('formats minutes and countdowns', () => {
    expect(formatMinutes(45)).toBe('45 min');
    expect(formatMinutes(60)).toBe('1 h');
    expect(formatMinutes(110)).toBe('1 h 50 m');
    expect(formatCountdown(412)).toBe('6:52');
    expect(formatCountdown(3725)).toBe('1:02:05');
    expect(formatClock(7, 0)).toBe('7:00 AM');
    expect(formatClock(0, 5)).toBe('12:05 AM');
    expect(formatClock(13, 30)).toBe('1:30 PM');
  });

  it('scales within device bounds', () => {
    expect(scaleMinutes(20, 110)).toBe(22);
    expect(scaleMinutes(15, 90)).toBe(14);
    expect(scaleMinutes(1, 90)).toBe(1);
    expect(scaleMinutes(240, 110)).toBe(240);
  });
});

describe('schedules', () => {
  it('finds the next run', () => {
    const sunday = new Date(2026, 10, 15, 15, 0);
    const n = nextRun(fall, 7, 0, sunday)!;
    expect(n.date.getDay()).toBe(2);
    expect(n.minutes).toBe(35);
    expect(n.zones).toBe(2);
  });

  it('counts today only before the start time and if not already run', () => {
    const tueEarly = new Date(2026, 10, 17, 6, 0);
    expect(nextRun(fall, 7, 0, tueEarly)!.date.getDate()).toBe(17);
    expect(nextRun(fall, 7, 0, tueEarly, localDayNumber(tueEarly))!.date.getDate()).toBe(20);
    const tueLate = new Date(2026, 10, 17, 9, 0);
    const n = nextRun(fall, 7, 0, tueLate)!;
    expect(n.date.getDate()).toBe(20);
    expect(n.minutes).toBe(45);
  });

  it('returns null for a schedule that never runs', () => {
    expect(nextRun({ name: 'x', items: [{ zone: 1, days: 'NONE', duration_minutes: 5 }] }, 7, 0, new Date())).toBeNull();
  });

  it('totals a week and scales every item', () => {
    expect(minutesPerWeek(fall)).toBe(80);
    expect(scaleSchedule(fall, 110).items.map((i) => i.duration_minutes)).toEqual([22, 17, 11]);
  });

  it('moves items within bounds', () => {
    expect(moveItem([1, 2, 3], 0, 1)).toEqual([2, 1, 3]);
    expect(moveItem([1, 2, 3], 2, 3)).toEqual([1, 2, 3]);
  });
});
