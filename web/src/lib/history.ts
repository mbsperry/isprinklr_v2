// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// Turns run/sched log lines into a day-by-day story for the History screen.

import type { LogLevel, Zone } from '../api/types';
import { localDayNumber } from './days';

export interface LogLine { time: string | null; level: LogLevel; module: string; message: string }

export type DayTone = 'ok' | 'warn' | 'none';

export interface HistoryItem { time: Date; text: string; tone: DayTone }
export interface HistoryDay { day: number; date: Date; tone: DayTone; items: HistoryItem[] }

/** "zone 3 finished" -> "Back lawn finished" when the zone has a name. */
export function nameZones(message: string, zones: Zone[]): string {
  const names = new Map(zones.map((z) => [z.zone, z.name]));
  return message.replace(/\bzone (\d+)\b/g, (whole, n: string) => names.get(Number(n)) ?? whole);
}

function capitalize(s: string): string {
  return s.charAt(0).toUpperCase() + s.slice(1);
}

/** Lines worth showing a non-technical reader. */
export function isStoryLine(l: LogLine): boolean {
  if (l.module !== 'run' && l.module !== 'sched') return false;
  if (l.level === 'debug') return false;
  return !/^(queued |bus busy|last run day|trigger:)|started for |checkpoint/.test(l.message);
}

// A skip or cancel is someone's choice, not a problem; these are.
export function toneOf(l: LogLine): DayTone {
  if (l.level === 'error' || l.level === 'warn') return 'warn';
  if (/not started|abandon|failed/.test(l.message)) return 'warn';
  return 'ok';
}

/**
 * Shorter wording for the History screen, or null to leave the line out.
 * Per-zone completions inside a schedule are left out: the run's own
 * "finished" line covers them.
 */
export function storyText(message: string, zones: Zone[]): string | null {
  let m = message;
  if (/^starting today's scheduled run/.test(m)) return null;
  if (/^zone \d+ finished: ran its full .*\(scheduled/.test(m)) return null;
  if (/^manual run finished: \d+ completed$/.test(m)) return null;
  m = m.replace(/ \((scheduled|manual)[^)]*\)/g, '');
  m = m.replace(/^zone (\d+) finished: ran its full (.+)$/, 'zone $1 watered $2');
  m = m.replace(/^run '([^']+)' (finished|cancelled|abandoned): /, '$1 $2: ');
  return capitalize(nameZones(m, zones));
}

/** Group lines by local date, newest day first, oldest line first within a day. */
export function groupByDay(lines: LogLine[], zones: Zone[]): HistoryDay[] {
  const days = new Map<number, HistoryDay>();
  for (const l of lines) {
    if (!l.time || !isStoryLine(l)) continue;
    const text = storyText(l.message, zones);
    if (text === null) continue;
    const t = new Date(l.time);
    const key = localDayNumber(t);
    let d = days.get(key);
    if (!d) {
      d = { day: key, date: t, tone: 'none', items: [] };
      days.set(key, d);
    }
    const tone = toneOf(l);
    d.items.push({ time: t, text, tone });
    if (tone === 'warn') d.tone = 'warn';
    else if (d.tone === 'none') d.tone = 'ok';
  }
  const out = [...days.values()].sort((a, b) => b.day - a.day);
  for (const d of out) d.items.sort((a, b) => a.time.getTime() - b.time.getTime());
  return out;
}
