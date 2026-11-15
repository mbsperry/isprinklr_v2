# server

Node service. Serves the frontend, collects logs, backs up schedules.

The UDP log sink and log history (item 2 below) exist as of Phase 5. Static
hosting and schedule backup come with Phase 6.

Holds no authority over watering. If this process is down, the ESP keeps watering
on schedule and the only loss is visibility.

## Responsibilities

1. **Static hosting** for [`../web/`](../web/)'s build output. Replaces nginx.
2. **UDP log sink.** Receives fire-and-forget syslog from the ESP and stores it
   filterable by level, module, and time range. Deliberately not HTTP — a log
   path that can block or retry must never touch the controller.
3. **Nightly schedule backup.** Pulls the ESP's schedule and writes a timestamped
   JSON file. This is the recovery path for a wiped or dead board.

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

Configuration comes from the environment, all optional:

| Variable | Default | |
|---|---|---|
| `ISPRINKLR_DB` | `server/data/logs.sqlite` | SQLite file; `server/data/` is git-ignored |
| `ISPRINKLR_UDP_PORT` | 5514 | Syslog port; matches the firmware default |
| `ISPRINKLR_HTTP_PORT` | 8080 | `GET /api/logs`, `GET /api/health` |

Then point the device at it, giving the server's IPv4 address. A PUT to
`/api/config` requires the start time and timezone too, so send the current
values along with it:

```bash
curl -X PUT -H 'Content-Type: application/json' -d '{"start_hour":7,"start_minute":0,"timezone":"PST8PDT,M3.2.0,M11.1.0","log_host":"192.168.88.21","log_port":5514}' http://192.168.88.54/api/config
```

Sending `"log_host": ""` turns export off. Query shapes are in
[`../docs/api-contract.md`](../docs/api-contract.md#node-service). Rows older
than 12 months are pruned at 03:30 local time each night, and once at startup.
