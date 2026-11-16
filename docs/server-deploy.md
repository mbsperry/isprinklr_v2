# Server deployment

The Node server runs on a small Linux container (Ubuntu 24.04) under its own
user account, as a systemd service. It needs Node 22.13 or later on the
`PATH`, git, gh or another way to clone the repo, and curl. The nightly jobs
run in the container's local time, so its time zone should match the
device's.

Below, `$ISPRINKLR_DEVICE` is the sprinkler controller's address, set in the
shell, and `<server-ip>` is the container's, to fill in by hand. Site settings like these live in an env file on the
container, not in the repo.

Nothing on the container is load-bearing. If it is down, the ESP keeps
watering; only the web UI, log history and new backups stop.

One systemd service, `isprinklr`, does all three jobs. It restarts on
failure, starts at boot, and logs to journald.

| | |
|---|---|
| Web UI | `http://<server-ip>:8080/`, from `web/dist` |
| Log sink | UDP 5514, into `~/isprinklr-data/logs.sqlite`, pruned to 12 months at 03:30 |
| Settings backup | from the device at startup and at 03:45 nightly, into `~/isprinklr-data/backups/` |

Data sits in `~/isprinklr-data`, outside the checkout, so re-cloning cannot
lose it. The unit is [`server/isprinklr.service`](../server/isprinklr.service),
a template filled in with the service user's name and home directory at
install time.

## First install

Run these on the container, as the user the service will run as.

```bash
gh repo clone mbsperry/isprinklr_v2 ~/isprinklr_v2
```

```bash
cd ~/isprinklr_v2/server
```

```bash
npm test
```

Create the settings file, then edit `ISPRINKLR_ESP_URL` in it to the
device's address:

```bash
mkdir -p ~/isprinklr-data && cp ~/isprinklr_v2/server/server.env.example ~/isprinklr-data/server.env
```

```bash
nano ~/isprinklr-data/server.env
```

Then install and start the service. This needs sudo, and fills in the
template with the current user and home directory:

```bash
sed -e "s|@USER@|$USER|g" -e "s|@HOME@|$HOME|g" ~/isprinklr_v2/server/isprinklr.service | sudo tee /etc/systemd/system/isprinklr.service > /dev/null && sudo systemctl daemon-reload && sudo systemctl enable --now isprinklr
```

Check it. The health response should show a `backup.last_timestamp`, and the
backups folder one file.

```bash
curl -s http://localhost:8080/api/health
```

```bash
ls ~/isprinklr-data/backups
```

## Point the ESP's logs at it

This changes a device setting. A PUT to `/api/config` needs the start time and
timezone too, so check the current ones first and send them back unchanged,
with `log_host` set to the container's address:

```bash
curl -s http://$ISPRINKLR_DEVICE/api/config
```

```bash
curl -X PUT -H 'Content-Type: application/json' -d '{"start_hour":7,"start_minute":0,"timezone":"PST8PDT,M3.2.0,M11.1.0","log_host":"<server-ip>","log_port":5514}' http://$ISPRINKLR_DEVICE/api/config
```

Within a few seconds `/api/health` on the server should show `sink.received`
climbing, and `/api/status` on the device should show the server under
`logs.sink`.

## Updating

```bash
cd ~/isprinklr_v2 && git pull && sudo systemctl restart isprinklr
```

Each restart also takes a backup, which is harmless. If `isprinklr.service`
changed, run the install command above again instead of the restart.

## Building the frontend

The server serves `web/dist` as it finds it, and shows a placeholder until
there is one, so a rebuild needs no restart. Building needs npm, unlike the
server. The build carries no addresses:

```bash
cd ~/isprinklr_v2/web && npm ci && npm run build
```

After a later `git pull`, run the same command again.

The app reaches the controller through this server, at `ISPRINKLR_ESP_URL` in
`~/isprinklr-data/server.env`. If the controller moves, change it there and
restart the service.

## Remote access

Point a Cloudflare tunnel's public hostname at `http://localhost:8080` on this
container, and put a Cloudflare Access policy on that hostname before turning
it on. The app can start, stop and restart the controller, and has no login of
its own. The controller is never exposed; only this server talks to it.

## Restoring a backup to the device

Backups are JSON files holding the exact body for each settings PUT, in the
order a blank board needs them (zones, schedules, active schedule, config,
on/off). To turn one into commands:

```bash
cd ~/isprinklr_v2/server && npm run -s restore-commands -- ~/isprinklr-data/backups/settings-2026-11-16T11-45-00Z.json
```

That only prints five `curl -X PUT` lines; nothing is sent. Read them, then
run them in order. They go to the address the backup was taken from; a second
argument such as `http://<new-device-ip>` sends them to a replacement board
instead.

The order is for a blank board. On one that already has settings, a PUT can be
refused, for example zones that would remove a zone an existing schedule uses.
The device then changes nothing and its error names the rule; send the lines in
whatever order clears it. The restore path has not been run against the
device.

The restored config includes `log_host`, so a restored board resumes sending
logs to the server.

## Logs and troubleshooting

```bash
sudo journalctl -u isprinklr -n 100
```

`journalctl` needs sudo unless the user is in the `adm` or `systemd-journal`
group.

```bash
systemctl status isprinklr
```

A backup that fails (the ESP offline, say) is retried every 30 minutes, four
times, then left until the next night. `/api/health` shows `last_error`.
