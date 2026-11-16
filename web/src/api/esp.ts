// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// The controller's API (docs/api-contract.md, "ESP32").

import { controllerUrl } from './config';
import { request } from './http';
import type { DeviceConfig, DeviceLogEntry, LogLevel, Schedule, SchedulesResponse, Status, Zone } from './types';

const u = (path: string) => controllerUrl() + path;

export const esp = {
  status: () => request<Status>(u('/api/status')),

  zones: () => request<{ zones: Zone[] }>(u('/api/zones')).then((r) => r.zones),
  putZones: (zones: Zone[]) => request(u('/api/zones'), { method: 'PUT', body: { zones } }),

  schedules: () => request<SchedulesResponse>(u('/api/schedules')),
  putSchedules: (schedules: Schedule[]) => request(u('/api/schedules'), { method: 'PUT', body: { schedules } }),
  setActive: (name: string | null) => request(u('/api/schedule/active'), { method: 'PUT', body: { name } }),
  setEnabled: (enabled: boolean) => request(u('/api/schedule/enabled'), { method: 'PUT', body: { enabled } }),

  config: () => request<DeviceConfig>(u('/api/config')),
  putConfig: (c: DeviceConfig) => request(u('/api/config'), { method: 'PUT', body: c }),

  startZone: (zone: number, duration_minutes: number) =>
    request<{ queued_behind: number }>(u('/api/zone/start'), { method: 'POST', body: { zone, duration_minutes } }),
  runItems: (items: { zone: number; duration_minutes: number }[]) =>
    request<{ queued_behind: number }>(u('/api/run'), { method: 'POST', body: { items } }),
  runSchedule: (name: string) => request(u('/api/schedule/run'), { method: 'POST', body: { name } }),
  cancel: () => request(u('/api/zone/stop'), { method: 'POST', body: {} }),
  skip: () => request(u('/api/run/skip'), { method: 'POST' }),
  restart: (force = false) => request(u(`/api/restart${force ? '?force=1' : ''}`), { method: 'POST' }),

  logs: (level: LogLevel = 'debug') =>
    request<{ entries: DeviceLogEntry[] }>(u(`/api/logs?level=${level}`)).then((r) => r.entries),
};
