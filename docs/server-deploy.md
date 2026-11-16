# Server deployment

The Node server runs on a Proxmox container: `isprinklr-v2.lan`, 192.168.88.43,
Ubuntu 24.04, user `isprinklr-v2` (no passwordless sudo). It has Node 24 (at
`/usr/bin/node`), git, gh (signed in as mbsperry) and curl, and its clock is set to
America/Los_Angeles, which is the time the nightly jobs run in.

Nothing on the container is load-bearing. If it is down, the ESP keeps
watering; only the web UI, log history and new backups stop.

One systemd service, `isprinklr`, does all three jobs. It restarts on
failure, starts at boot, and logs to journald.

| | |
|---|---|
| Web UI | `http://isprinklr-v2.lan:8080/`, from `web/dist` |
| Log sink | UDP 5514, into `~/isprinklr-data/logs.sqlite`, pruned to 12 months at 03:30 |
| Settings backup | from 192.168.88.54 at startup and at 03:45 nightly, into `~/isprinklr-data/backups/` |

Data sits in `~/isprinklr-data`, outside the checkout, so re-cloning cannot
lose it. Settings are in [`server/isprinklr.service`](../server/isprinklr.service).

## First install

Run these on the container, from `ssh isprinklr-v2`.

```bash
gh repo clone mbsperry/isprinklr_v2 ~/isprinklr_v2
```

```bash
cd ~/isprinklr_v2/server
```

```bash
npm test
```

Then install and start the service. This needs sudo:

```bash
sudo cp ~/isprinklr_v2/server/isprinklr.service /etc/systemd/system/ && sudo systemctl daemon-reload && sudo systemctl enable --now isprinklr
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
timezone too, so check the current ones first and send them back unchanged:

```bash
curl -s http://192.168.88.54/api/config
```

```bash
curl -X PUT -H 'Content-Type: application/json' -d '{"start_hour":7,"start_minute":0,"timezone":"PST8PDT,M3.2.0,M11.1.0","log_host":"192.168.88.43","log_port":5514}' http://192.168.88.54/api/config
```

Within a few seconds `/api/health` on the server should show `sink.received`
climbing, and `/api/status` on the device should show
`"sink": "192.168.88.43:5514"`.

## Updating

```bash
cd ~/isprinklr_v2 && git pull && sudo systemctl restart isprinklr
```

Each restart also takes a backup, which is harmless. If the unit file itself
changed, copy it again and run `sudo systemctl daemon-reload` before the
restart.

## When the frontend exists

The server serves `web/dist` as it finds it, and shows a placeholder until
there is one. Building it on the container is one more step after `git pull`;
the exact command comes with the frontend.

## Restoring a backup to the device

Backups are JSON files holding the exact body for each settings PUT, in the
order a blank board needs them (zones, schedules, active schedule, config,
on/off). To turn one into commands:

```bash
cd ~/isprinklr_v2/server && npm run -s restore-commands -- ~/isprinklr-data/backups/settings-2026-11-16T11-45-00Z.json
```

That only prints five `curl -X PUT` lines; nothing is sent. Read them, then
run them in order. A second argument such as `http://192.168.88.60` sends them
to a replacement board at another address.

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

The `isprinklr-v2` user is not in the `adm` or `systemd-journal` group, so
`journalctl` needs sudo.

```bash
systemctl status isprinklr
```

A backup that fails (the ESP offline, say) is retried every 30 minutes, four
times, then left until the next night. `/api/health` shows `last_error`.
