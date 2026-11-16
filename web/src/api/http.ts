// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { ApiError } from '../lib/errors';

const TIMEOUT_MS = 8000;

export async function request<T>(url: string, init?: { method?: string; body?: unknown }): Promise<T> {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), TIMEOUT_MS);
  let res: Response;
  try {
    res = await fetch(url, {
      method: init?.method ?? 'GET',
      headers: init?.body !== undefined ? { 'Content-Type': 'application/json' } : undefined,
      body: init?.body !== undefined ? JSON.stringify(init.body) : undefined,
      signal: ctrl.signal,
    });
  } catch {
    throw new ApiError('network', 0);
  } finally {
    clearTimeout(timer);
  }
  let data: unknown = null;
  try {
    data = await res.json();
  } catch {
    // A bare error page; handled below.
  }
  const body = (data ?? {}) as { ok?: boolean; error?: string; message?: string };
  if (!res.ok || body.ok === false) {
    const code = body.error ?? (res.status === 413 ? 'too_large' : `http_${res.status}`);
    throw new ApiError(code, res.status, body.message);
  }
  return data as T;
}
