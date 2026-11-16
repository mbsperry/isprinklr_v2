// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// Device error codes to short, plain messages. Unknown codes fall back to the
// device's own message.

const MESSAGES: Record<string, string> = {
  too_large: 'Too much to save',
  queue_full: 'The queue is full',
  update_in_progress: 'Controller is updating, try again soon',
  busy: 'Controller is busy',
  run_in_progress: 'Watering in progress',
  no_active_zone: 'Nothing is watering',
  nothing_to_skip: 'Nothing to skip',
  nothing_to_run: 'Nothing to run in that schedule',
  unknown_schedule: 'That schedule no longer exists',
  storage_failed: "Couldn't save on the controller",
  invalid_zone: 'That zone is not valid',
  invalid_duration: 'Minutes must be 1 to 240',
  network: "Can't reach the sprinkler controller",
};

export class ApiError extends Error {
  constructor(
    public readonly code: string,
    public readonly status: number,
    deviceMessage?: string,
  ) {
    super(MESSAGES[code] ?? deviceMessage ?? `Request failed (${status})`);
  }
}

export function messageFor(err: unknown): string {
  if (err instanceof ApiError) return err.message;
  return MESSAGES.network;
}
