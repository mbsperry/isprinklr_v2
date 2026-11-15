// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// Parses the RFC 5424 lines the ESP sends (firmware/lib/logging/src/
// syslog_format.h):
//
//   <134>1 2026-11-15T22:40:20Z isprinklr isprinklr - run
//     [isp@32473 seq="42" up="81234" boot="1a2b3c4d"] zone 3 started ...
//
// Anything else that arrives on the port is kept rather than dropped, with
// module "unparsed" and the raw text as the message.

export const LEVELS = Object.freeze({
  emerg: 0, alert: 1, crit: 2, error: 3, warn: 4, notice: 5, info: 6, debug: 7,
});

const LEVEL_NAMES = Object.fromEntries(
  Object.entries(LEVELS).map(([name, n]) => [n, name]));

export function levelName(severity) {
  return LEVEL_NAMES[severity] ?? 'info';
}

// "warn" -> 4. Also accepts the number itself. Undefined if unknown.
export function parseLevel(text) {
  if (text === undefined || text === null || text === '') return undefined;
  if (/^[0-7]$/.test(String(text))) return Number(text);
  return LEVELS[String(text).toLowerCase()];
}

const HEADER =
  /^<(\d{1,3})>1 (\S+) (\S+) (\S+) (\S+) (\S+) (-|(?:\[(?:[^\]"\\]|"(?:[^"\\]|\\.)*")*\])+)(?: (.*))?$/s;
const SD_PARAM = /([^\s=\]"]+)="((?:[^"\\]|\\.)*)"/g;

function nil(field) {
  return field === '-' ? null : field;
}

function parseStructuredData(sd) {
  const params = {};
  if (sd === '-') return params;
  for (const m of sd.matchAll(SD_PARAM)) {
    params[m[1]] = m[2].replace(/\\(["\\\]])/g, '$1');
  }
  return params;
}

function toInt(text) {
  if (text === undefined || !/^\d+$/.test(text)) return null;
  const n = Number(text);
  return Number.isSafeInteger(n) ? n : null;
}

// Returns a row for LogStore. receivedAt: Date the datagram arrived.
export function parseSyslog(text, receivedAt, fromAddress = null) {
  const received = receivedAt.getTime();
  const line = text.replace(/[\r\n]+$/, '');
  const m = HEADER.exec(line);
  if (!m) {
    const pri = /^<(\d{1,3})>/.exec(line);
    return {
      time: received,
      timeSource: 'received',
      receivedAt: received,
      severity: pri ? Number(pri[1]) % 8 : LEVELS.info,
      module: 'unparsed',
      message: line,
      host: fromAddress,
      boot: null,
      seq: null,
      uptimeMs: null,
    };
  }

  const [, pri, timestamp, host, , , msgid, sd, msg = ''] = m;
  const deviceTime = nil(timestamp) === null ? NaN : Date.parse(timestamp);
  const params = parseStructuredData(sd);
  return {
    time: Number.isNaN(deviceTime) ? received : deviceTime,
    timeSource: Number.isNaN(deviceTime) ? 'received' : 'device',
    receivedAt: received,
    severity: Number(pri) % 8,
    module: nil(msgid) ?? '-',
    message: msg,
    host: nil(host) ?? fromAddress,
    boot: params.boot ?? null,
    seq: toInt(params.seq),
    uptimeMs: toInt(params.up),
  };
}
