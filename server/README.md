# server

Node service. Serves the frontend, collects logs, backs up schedules.

Nothing here yet.

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
