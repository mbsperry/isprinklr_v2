// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// Shapes from docs/api-contract.md. Durations are minutes unless the field
// name says otherwise.

export interface Zone { zone: number; name: string }

export interface ScheduleItem { zone: number; days: string; duration_minutes: number }
export interface Schedule { name: string; items: ScheduleItem[] }

export interface SchedulesResponse {
  schedules: Schedule[];
  active: string | null;
  enabled: boolean;
  settings_hash?: string;
}

export type LogLevel = 'error' | 'warn' | 'info' | 'debug';

export interface DeviceConfig {
  start_hour: number;
  start_minute: number;
  timezone: string;
  log_host?: string;
  log_port?: number;
  log_level?: LogLevel;
}

export type Outcome = 'pending' | 'running' | 'completed' | 'skipped' | 'interrupted' | 'failed';

export interface RunEntry {
  zone: number;
  duration_minutes: number;
  origin: 'schedule' | 'manual';
  outcome: Outcome;
  started_epoch?: number;
}

export interface RunStatus {
  state: 'idle' | 'starting' | 'running' | 'padding';
  schedule: string | null;
  current_index: number;
  remaining_seconds: number;
  entries: RunEntry[];
}

export interface Status {
  firmware_version?: string;
  uptime_ms?: number;
  reset_reason?: string;
  time_valid?: boolean;
  settings_hash?: string;
  time?: { valid: boolean; local?: string | null; last_sync_ago_seconds?: number | null; timezone?: string };
  settings?: {
    load: 'loaded' | 'empty' | 'corrupt';
    load_error?: string;
    zones: number;
    schedules: number;
    active_schedule: string | null;
    enabled: boolean;
    start_time: string;
    today?: { zone: number; duration_minutes: number }[];
  };
  zones?: { commanded_zone: number | null; duration_minutes?: number; remaining_seconds?: number };
  run?: RunStatus;
  trigger?: { last_decision: string; window_minutes: number; last_run_day: number };
  recovery?: { result: string; window_minutes: number; late_seconds?: number };
  logs?: { capacity: number; last_seq: number; sink?: string; level?: LogLevel };
  network?: Record<string, unknown>;
  memory?: { free_heap: number; min_free_heap: number };
}

export interface DeviceLogEntry {
  seq: number;
  uptime_ms: number;
  time: string | null;
  level: LogLevel;
  module: string;
  message: string;
}

export interface ServerLogEntry {
  id: number;
  time: string;
  level: LogLevel;
  module: string;
  message: string;
}

export interface Backup { timestamp: string; taken_at: string; settings_hash: string; bytes: number }
