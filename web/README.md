# web

The iSprinklr frontend: React 19, TypeScript, Vite. Mobile first, because it
is mostly used from a phone, and written for anyone in the household, not just
whoever installed it.

The Node server in [`../server/`](../server/) hosts the build from `web/dist`;
see [`../docs/server-deploy.md`](../docs/server-deploy.md#building-the-frontend)
for the build command on the server.

## Two hosts

- **The controller**, for status, control and settings. The browser reaches it
  through the server: calls go to `/esp/api/...` on the page's own origin and
  the server forwards them to the device
  ([`esp_proxy.js`](../server/src/esp_proxy.js)). That is what lets the UI
  work through an HTTPS tunnel and away from home. The device's address is
  `ISPRINKLR_ESP_URL` in the server's `server.env`; the build needs no
  settings.
- **The server** that hosts the page, for log history and backups. Always the
  page's own origin.

If the server is down the controller keeps watering on its own schedule; only
this UI goes away.

## Screens

| Path | What it's for |
|---|---|
| `/` | Home: one plain status line, quick zone buttons, the schedule in use |
| `/water`, `/water/several` | Run one zone, or several in a row, or a saved schedule now |
| `/watering` | The run in progress: countdown, stop, skip, what's next |
| `/schedules`, `/schedules/:name` | Automatic watering on/off (with a confirm), start time, list and editor |
| `/history` | Day by day, from the server's logs, or the controller's own when the server is down |
| `/details`, `/zones`, `/settings` | The technical view: health, raw logs, restart, zone setup, time zone, log level, backups |

Alerts and confirms are a heading and buttons, nothing more.

## Layout

- `src/lib/` is pure logic with unit tests: day patterns, durations, next run,
  the home status line, the history wording. `src/lib/limits.ts` mirrors the
  firmware's bounds (zones 1–10, 1–240 minutes); change them together.
- `src/api/` talks to the two hosts. Durations are minutes everywhere, and say
  so in their names.
- `src/state.tsx` polls `/api/status` (every 2 s while watering, 10 s
  otherwise) and re-reads zones, schedules and config when `settings_hash`
  changes.
- `src/screens/` has one file per screen.

## Working on it

```bash
npm ci
```

```bash
npm test
```

```bash
npm run build
```

Without the device, run the mock controller and log server in one terminal:

```bash
MOCK_SPEED=60 npm run mock
```

and the dev server in another:

```bash
npm run dev
```

The dev server proxies `/esp` to `ISPRINKLR_ESP_URL` (default the mock on
:8081) and `/api` to `ISPRINKLR_SERVER_URL` (default the mock on :8080).
`MOCK_SPEED` speeds zones up; `MOCK_STATE=off` starts with automatic watering
off. The mock follows the API's shapes, not every rule the firmware enforces.
