// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';

// In development the log server's /api lives on another port; proxy it so the
// app can use same-origin paths, exactly as it does when the server hosts it.
export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      '/api': process.env.ISPRINKLR_SERVER_URL ?? 'http://localhost:8080',
    },
  },
  test: {
    include: ['src/**/*.test.ts'],
  },
});
