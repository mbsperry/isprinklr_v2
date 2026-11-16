// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// The device enforces these; the UI mirrors them so it can stop a bad value
// before sending it. Source of truth: firmware/lib/hunter/src/hunter_limits.h
// and firmware/lib/settings/src/settings.cpp. Change them together.
export const MIN_ZONE = 1;
export const MAX_ZONE = 10;
export const MIN_RUN_MINUTES = 1;
export const MAX_RUN_MINUTES = 240;
export const MAX_SCHEDULES = 10;
export const MAX_NAME_BYTES = 32;

export function clampMinutes(minutes: number): number {
  if (!Number.isFinite(minutes)) return MIN_RUN_MINUTES;
  return Math.min(MAX_RUN_MINUTES, Math.max(MIN_RUN_MINUTES, Math.round(minutes)));
}

export function nameBytes(name: string): number {
  return new TextEncoder().encode(name).length;
}

export function isValidName(name: string): boolean {
  const n = nameBytes(name.trim());
  return n >= 1 && n <= MAX_NAME_BYTES;
}
