// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';

// In development, proxy the same paths the server answers when it hosts the
// app: /api to the log server, and /esp straight to the controller (the
// server forwards /esp the same way; see server/src/esp_proxy.js). The
// defaults match `npm run mock`.
const espUrl = process.env.ISPRINKLR_ESP_URL ?? 'http://localhost:8081';
// Extra host names the dev server answers to, comma-separated, for opening it
// from another machine by name. Site-specific, so it comes from the
// environment rather than this public file.
const devHosts = (process.env.ISPRINKLR_DEV_HOSTS ?? '')
  .split(',')
  .map((h) => h.trim())
  .filter(Boolean);
export default defineConfig({
  plugins: [react()],
  server: {
    allowedHosts: devHosts,
    proxy: {
      '/api': process.env.ISPRINKLR_SERVER_URL ?? 'http://localhost:8080',
      '/esp': { target: espUrl, rewrite: (p) => p.replace(/^\/esp/, '') },
    },
  },
  test: {
    include: ['src/**/*.test.ts'],
  },
});
