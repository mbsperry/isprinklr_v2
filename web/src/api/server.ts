// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// The Node server that hosts this app (same origin): log history and backups.

import { request } from './http';
import type { Backup, LogLevel, ServerLogEntry } from './types';

export interface LogQuery { level?: LogLevel; modules?: string[]; since?: Date; limit?: number }

export const server = {
  logs: (q: LogQuery = {}) => {
    const p = new URLSearchParams();
    if (q.level) p.set('level', q.level);
    if (q.modules?.length) p.set('module', q.modules.join(','));
    if (q.since) p.set('since', q.since.toISOString());
    p.set('limit', String(q.limit ?? 500));
    return request<{ entries: ServerLogEntry[] }>(`/api/logs?${p}`).then((r) => r.entries);
  },
  backups: () => request<{ backups: Backup[] }>('/api/backups').then((r) => r.backups),
};
