// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// pm2 config for the server on the Proxmox CT (docs/server-deploy.md):
//   pm2 start ecosystem.config.cjs
//
// Data lives outside the checkout, under ~/isprinklr-data, so a fresh clone
// or a `git clean` cannot touch log history or backups.

const os = require('node:os');
const path = require('node:path');

const data = path.join(os.homedir(), 'isprinklr-data');

module.exports = {
  apps: [{
    name: 'isprinklr',
    script: 'src/main.js',
    cwd: __dirname,
    // node:sqlite prints an ExperimentalWarning on every start; it is not news.
    node_args: '--disable-warning=ExperimentalWarning',
    env: {
      ISPRINKLR_DB: path.join(data, 'logs.sqlite'),
      ISPRINKLR_BACKUP_DIR: path.join(data, 'backups'),
      ISPRINKLR_WEB_DIR: path.join(__dirname, '..', 'web', 'dist'),
      ISPRINKLR_ESP_URL: 'http://192.168.88.54',
      ISPRINKLR_HTTP_PORT: '8080',
      ISPRINKLR_UDP_PORT: '5514',
    },
    // main.js flushes pending log lines on SIGTERM; give it time to.
    kill_timeout: 5000,
    max_restarts: 20,
    restart_delay: 5000,
    time: true,
  }],
};
