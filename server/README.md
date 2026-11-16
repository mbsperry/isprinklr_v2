# server

Node service. Serves the frontend, collects logs, backs up schedules.

The UDP log sink and log history exist as of Phase 5; static hosting and the
nightly settings backup as of Phase 6. It runs as a systemd service on a small Linux
container; [`../docs/server-deploy.md`](../docs/server-deploy.md) has the
install, update and restore steps.

Holds no authority over watering. If this process is down, the ESP keeps watering
on schedule and the only loss is visibility.

## Responsibilities

1. **Static hosting** for [`../web/`](../web/)'s build output. Replaces nginx.
   Unknown extensionless paths get `index.html`, for client-side routes;
   Vite's hashed `/assets/` are cached long, everything else revalidated.
2. **UDP log sink.** Receives fire-and-forget syslog from the ESP and stores it
   filterable by level, module, and time range. Deliberately not HTTP — a log
   path that can block or retry must never touch the controller.
3. **Nightly settings backup.** Pulls the ESP's zones, schedules, active
   schedule, on/off flag and config, at startup and at 03:45 local time, and
   writes them to `settings-<timestamp>.json`. GETs only, each with a 10 s
   timeout; a failure is retried every 30 minutes, four times. This is the
   recovery path for a wiped or dead board.

Replaces v1's FastAPI service, which owned scheduling and was therefore
load-bearing. This one is not.

## Running it

Needs Node 22.13 or later, for the built-in `node:sqlite`. There are no npm
dependencies, so there is nothing to install.

```bash
cd server
```

```bash
npm start
```

```bash
npm test
```

```bash
npm run backup
```

`npm run backup` takes one backup now and prints its path.

Configuration comes from the environment, all optional:

| Variable | Default | |
|---|---|---|
| `ISPRINKLR_DB` | `server/data/logs.sqlite` | SQLite file; `server/data/` is git-ignored |
| `ISPRINKLR_UDP_PORT` | 5514 | Syslog port; matches the firmware default |
| `ISPRINKLR_HTTP_PORT` | 8080 | The web UI and `/api/logs`, `/api/health`, `/api/backups` |
| `ISPRINKLR_WEB_DIR` | `web/dist` | Frontend build; a placeholder page is served until it exists |
| `ISPRINKLR_BACKUP_DIR` | `server/backups` | Settings backups; git-ignored |
| `ISPRINKLR_ESP_URL` | unset | The device, e.g. `http://192.0.2.10`; backups are off without it |

Under systemd, `isprinklr.service` sets the data paths to `~/isprinklr-data`
and reads site settings from `~/isprinklr-data/server.env`; see
`server.env.example`.

Then point the device at it, giving the server's IPv4 address. A PUT to
`/api/config` requires the start time and timezone too, so send the current
values along with it:

```bash
curl -X PUT -H 'Content-Type: application/json' -d '{"start_hour":7,"start_minute":0,"timezone":"PST8PDT,M3.2.0,M11.1.0","log_host":"192.168.88.21","log_port":5514}' http://192.168.88.54/api/config
```

Sending `"log_host": ""` turns export off. Query shapes are in
[`../docs/api-contract.md`](../docs/api-contract.md#node-service). Rows older
than 12 months are pruned at 03:30 local time each night, and once at startup.

To turn a backup back into device settings, `npm run -s restore-commands --
<file>` prints the PUTs to send; see
[`../docs/server-deploy.md`](../docs/server-deploy.md#restoring-a-backup-to-the-device).
