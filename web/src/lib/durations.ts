// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { clampMinutes } from './limits';

/** 95 -> "1 h 35 m", 45 -> "45 min", 60 -> "1 h". */
export function formatMinutes(totalMinutes: number): string {
  const m = Math.max(0, Math.round(totalMinutes));
  if (m < 60) return `${m} min`;
  const h = Math.floor(m / 60);
  const rest = m % 60;
  return rest ? `${h} h ${rest} m` : `${h} h`;
}

/** 412 -> "6:52". */
export function formatCountdown(totalSeconds: number): string {
  const s = Math.max(0, Math.floor(totalSeconds));
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = String(s % 60).padStart(2, '0');
  return h ? `${h}:${String(m).padStart(2, '0')}:${sec}` : `${m}:${sec}`;
}

/** Scale by a percentage (90 = 10% less), rounded, kept within device bounds. */
export function scaleMinutes(minutes: number, percent: number): number {
  return clampMinutes((minutes * percent) / 100);
}

/** "7:00 AM" from hour/minute. */
export function formatClock(hour: number, minute: number): string {
  const h12 = hour % 12 === 0 ? 12 : hour % 12;
  return `${h12}:${String(minute).padStart(2, '0')} ${hour < 12 ? 'AM' : 'PM'}`;
}

export function formatTimeOfDay(d: Date): string {
  return formatClock(d.getHours(), d.getMinutes());
}
