// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// Forwards the web UI's controller calls to the device, so the browser only
// ever talks to this server's origin. That is what lets the UI work through
// an HTTPS tunnel (Cloudflare) and away from home: the browser can't reach
// the device's LAN address, and an https page can't call an http one.
//
//   /esp/api/status  ->  ISPRINKLR_ESP_URL/api/status
//
// The proxy carries UI traffic only. Watering never depends on it: schedules
// live on the device, which keeps running if this server is down.
//
// Requests that change anything (anything but GET) must carry the header
// X-iSprinklr: 1. A page on another site can't add a custom header without a
// CORS preflight, and this server answers none, so it can't drive the
// controller through the proxy. Firmware upload (/api/update) is not
// forwarded; do OTA from the LAN.

export const PROXY_PREFIX = '/esp';
export const CSRF_HEADER = 'x-isprinklr';
export const MAX_BODY_BYTES = 16 * 1024;
export const TIMEOUT_MS = 10_000;

const METHODS = new Set(['GET', 'POST', 'PUT']);

function sendJson(res, status, body) {
  res.writeHead(status, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
  res.end(JSON.stringify(body));
}

class TooLarge extends Error {}

async function readBody(req, limit) {
  const declared = Number(req.headers['content-length']);
  if (declared > limit) throw new TooLarge();
  const chunks = [];
  let size = 0;
  for await (const chunk of req) {
    size += chunk.length;
    if (size > limit) throw new TooLarge();
    chunks.push(chunk);
  }
  return Buffer.concat(chunks);
}

// espUrl: the device, e.g. http://192.0.2.10. Empty: every call answers 503.
// Returns handler(req, res, url) for paths under PROXY_PREFIX.
export function createEspProxy(espUrl, { timeoutMs = TIMEOUT_MS, fetchImpl = fetch } = {}) {
  const base = (espUrl ?? '').replace(/\/+$/, '');
  return async function proxy(req, res, url) {
    const path = url.pathname.slice(PROXY_PREFIX.length);
    if (!path.startsWith('/api/') || path === '/api/update' || path.startsWith('/api/update/')) {
      sendJson(res, 404, { ok: false, error: 'not_found' });
      return;
    }
    if (!METHODS.has(req.method)) {
      sendJson(res, 405, { ok: false, error: 'method_not_allowed' });
      return;
    }
    if (req.method !== 'GET' && req.headers[CSRF_HEADER] !== '1') {
      sendJson(res, 403, {
        ok: false, error: 'forbidden', message: `missing ${CSRF_HEADER} header`,
      });
      return;
    }
    if (!base) {
      sendJson(res, 503, {
        ok: false, error: 'controller_not_configured', message: 'ISPRINKLR_ESP_URL is not set',
      });
      return;
    }

    let body;
    if (req.method !== 'GET') {
      try {
        body = await readBody(req, MAX_BODY_BYTES);
      } catch (err) {
        if (err instanceof TooLarge) {
          sendJson(res, 413, { ok: false, error: 'too_large' });
          return;
        }
        throw err;
      }
    }

    // Only the content type goes through: no cookies, no Cloudflare headers,
    // no Origin (the device's CORS check is for browsers calling it directly).
    const headers = {};
    if (req.headers['content-type']) headers['Content-Type'] = req.headers['content-type'];

    let upstream;
    try {
      upstream = await fetchImpl(base + path + url.search, {
        method: req.method,
        headers,
        body: body && body.length > 0 ? body : undefined,
        signal: AbortSignal.timeout(timeoutMs),
        redirect: 'manual',
      });
    } catch (err) {
      const timedOut = err?.name === 'TimeoutError';
      sendJson(res, timedOut ? 504 : 502, {
        ok: false,
        error: timedOut ? 'controller_timeout' : 'controller_unreachable',
      });
      return;
    }
    const text = Buffer.from(await upstream.arrayBuffer());
    res.writeHead(upstream.status, {
      'Content-Type': upstream.headers.get('content-type') ?? 'application/json',
      'Cache-Control': 'no-store',
    });
    res.end(text);
  };
}
