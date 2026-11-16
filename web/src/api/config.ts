// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// Two hosts (docs/architecture.md): the controller for status, control and
// settings, and the server that hosts this app for log history and backups.
// The server is always this page's own origin. The controller's address is
// fixed at build time by VITE_ESP_URL and can be overridden per browser from
// Settings, in case the controller moves.

const KEY = 'isprinklr.controllerUrl';

export function builtInControllerUrl(): string {
  return (import.meta.env.VITE_ESP_URL ?? '').replace(/\/+$/, '');
}

export function controllerUrl(): string {
  try {
    const saved = localStorage.getItem(KEY);
    if (saved) return saved;
  } catch {
    // Storage blocked: use the built-in address.
  }
  return builtInControllerUrl();
}

/** Accepts "192.0.2.10" or "http://192.0.2.10"; empty restores the built-in address. */
export function setControllerUrl(value: string): void {
  const v = value.trim().replace(/\/+$/, '');
  try {
    if (!v) localStorage.removeItem(KEY);
    else localStorage.setItem(KEY, /^https?:\/\//.test(v) ? v : `http://${v}`);
  } catch {
    // Nothing to do; the setting just won't persist.
  }
}
