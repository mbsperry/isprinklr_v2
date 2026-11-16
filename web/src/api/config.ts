// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// Two hosts (docs/architecture.md): the controller for status, control and
// settings, and the server that hosts this app for log history and backups.
// The browser only talks to the server, this page's own origin. The server
// forwards /esp/ to the controller (server/src/esp_proxy.js), so the app
// works through an HTTPS tunnel and away from home. The controller's address
// is set on the server (ISPRINKLR_ESP_URL in server.env).

export const CONTROLLER_BASE = '/esp';
