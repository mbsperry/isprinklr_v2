// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { describe, expect, it } from 'vitest';
import { describeDays, formatDays, localDayNumber, parseDays, runsOn, sharedPattern, toggleDay } from './days';

describe('day patterns', () => {
  it('parses case-insensitively and returns canonical order', () => {
    expect(formatDays(parseDays('f:Tu'))).toBe('TU:F');
    expect(formatDays(parseDays('all'))).toBe('ALL');
    expect(formatDays(parseDays('EO'))).toBe('EO');
    expect(formatDays(parseDays(''))).toBe('NONE');
  });

  it('toggling collapses to ALL and NONE', () => {
    let p = parseDays('M:TU:W:TH:F:SA');
    p = toggleDay(p, 'SU');
    expect(formatDays(p)).toBe('ALL');
    p = toggleDay(parseDays('M'), 'M');
    expect(formatDays(p)).toBe('NONE');
    expect(formatDays(toggleDay(parseDays('ALL'), 'M'))).toBe('TU:W:TH:F:SA:SU');
  });

  it('describes patterns in words', () => {
    expect(describeDays(parseDays('TU:F'))).toBe('Tuesdays and Fridays');
    expect(describeDays(parseDays('M:W:F'))).toBe('Mondays, Wednesdays and Fridays');
    expect(describeDays(parseDays('M:TU:W:TH:F'))).toBe('Weekdays');
    expect(describeDays(parseDays('ALL'))).toBe('Every day');
  });

  it('matches days of the week, Monday first', () => {
    const sun = new Date(2026, 10, 15, 12);
    const tue = new Date(2026, 10, 17, 12);
    expect(runsOn(parseDays('SU'), sun)).toBe(true);
    expect(runsOn(parseDays('TU:F'), sun)).toBe(false);
    expect(runsOn(parseDays('TU:F'), tue)).toBe(true);
  });

  it('EO uses calendar-day parity since 1970, like the firmware', () => {
    const d = new Date(2026, 10, 15, 12);
    const even = localDayNumber(d) % 2 === 0;
    expect(runsOn(parseDays('EO'), d)).toBe(even);
    expect(runsOn(parseDays('EO'), new Date(2026, 10, 16, 12))).toBe(!even);
    // Dec 31 and Jan 1 never both run (v1's day-of-year bug).
    expect(runsOn(parseDays('EO'), new Date(2026, 11, 31, 12))).not.toBe(runsOn(parseDays('EO'), new Date(2027, 0, 1, 12)));
  });

  it('finds a shared pattern', () => {
    expect(sharedPattern(['TU:F', 'f:tu'])).toBe('TU:F');
    expect(sharedPattern(['TU:F', 'ALL'])).toBeNull();
  });
});
