// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// Decision 3: keep 12 months, prune nightly.

export const RETENTION_MONTHS = 12;
export const PRUNE_HOUR = 3;  // local time; well clear of a 07:00 run
export const PRUNE_MINUTE = 30;

// The same local date and time, `months` earlier, clamped to the end of a
// shorter month (2027-03-31 minus one month is 2027-02-28).
export function cutoff(now, months = RETENTION_MONTHS) {
  const d = new Date(now.getTime());
  const day = d.getDate();
  d.setDate(1);
  d.setMonth(d.getMonth() - months);
  const lastDay = new Date(d.getFullYear(), d.getMonth() + 1, 0).getDate();
  d.setDate(Math.min(day, lastDay));
  return d;
}

// Milliseconds from now until the next hour:minute local time.
export function msUntilNext(now, hour = PRUNE_HOUR, minute = PRUNE_MINUTE) {
  const next = new Date(now.getTime());
  next.setHours(hour, minute, 0, 0);
  if (next <= now) next.setDate(next.getDate() + 1);
  return next.getTime() - now.getTime();
}

// Runs prune() now and then every night. Returns a function that stops it.
export function schedulePrune(prune, { clock = () => new Date(), log = console.log } = {}) {
  let timer = null;
  let stopped = false;
  const run = () => {
    try {
      const before = cutoff(clock());
      const n = prune(before.getTime());
      log(`[retention] pruned ${n} log rows older than ${before.toISOString()}`);
    } catch (err) {
      log(`[retention] prune failed: ${err.message}`);
    }
    if (!stopped) timer = setTimeout(run, msUntilNext(clock()));
  };
  run();
  return () => {
    stopped = true;
    clearTimeout(timer);
  };
}
