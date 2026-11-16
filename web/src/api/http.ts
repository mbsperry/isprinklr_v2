// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { ApiError } from '../lib/errors';

// Longer than the server proxy's own 10 s limit, so a slow controller shows
// up as the proxy's 504 rather than an abort here.
const TIMEOUT_MS = 12000;

// The server proxy refuses writes without this header (server/src/esp_proxy.js),
// which keeps other sites from driving the controller through it.
const WRITE_HEADER = { 'X-iSprinklr': '1' };

// The proxy's own failures mean the controller can't be reached.
const UNREACHABLE = new Set(['controller_unreachable', 'controller_timeout', 'controller_not_configured']);

export async function request<T>(url: string, init?: { method?: string; body?: unknown }): Promise<T> {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), TIMEOUT_MS);
  const method = init?.method ?? 'GET';
  const headers: Record<string, string> = method === 'GET' ? {} : { ...WRITE_HEADER };
  if (init?.body !== undefined) headers['Content-Type'] = 'application/json';
  let res: Response;
  try {
    res = await fetch(url, {
      method,
      headers,
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
    let code = body.error ?? (res.status === 413 ? 'too_large' : `http_${res.status}`);
    if (UNREACHABLE.has(code)) code = 'network';
    throw new ApiError(code, res.status, body.message);
  }
  return data as T;
}
