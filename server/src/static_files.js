// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// Serves web/'s Vite build (architecture.md: no nginx). Anything under /api is
// handled elsewhere; this only answers the rest.
//
// - A path that names a file under the root is served as that file.
// - A path with no file extension that names nothing falls back to
//   index.html, so client-side routes survive a reload.
// - Vite's hashed bundles under /assets/ are cached for a year; everything
//   else, index.html above all, is revalidated on every load so a new build
//   shows up at once.
// - Nothing outside the root is ever served, whatever the path says.

import { createReadStream } from 'node:fs';
import { stat } from 'node:fs/promises';
import { extname, join, resolve, sep } from 'node:path';

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json',
  '.map': 'application/json',
  '.webmanifest': 'application/manifest+json',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.gif': 'image/gif',
  '.webp': 'image/webp',
  '.ico': 'image/x-icon',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2',
  '.txt': 'text/plain; charset=utf-8',
};

const NOT_BUILT = `<!doctype html>
<meta charset="utf-8">
<title>iSprinklr</title>
<p>The iSprinklr frontend has not been built yet. The server's API is up:
<a href="/api/health">/api/health</a>, <a href="/api/logs">/api/logs</a>,
<a href="/api/backups">/api/backups</a>.</p>
<p>The sprinkler controller's own status page is on the device itself.</p>
`;

async function fileAt(path) {
  try {
    const s = await stat(path);
    return s.isFile() ? s : null;
  } catch {
    return null;
  }
}

// Exported for tests: maps a URL path to a file under root, or null when the
// path would leave it. Does not touch the filesystem.
export function resolveUnder(root, urlPath) {
  let decoded;
  try {
    decoded = decodeURIComponent(urlPath);
  } catch {
    return null;
  }
  if (decoded.includes('\0')) return null;
  const base = resolve(root);
  const target = resolve(base, '.' + (decoded.startsWith('/') ? decoded : '/' + decoded));
  if (target !== base && !target.startsWith(base + sep)) return null;
  return target;
}

// Returns an async (req, res, pathname) handler for GET and HEAD.
export function createStaticHandler(root) {
  const base = resolve(root);
  const indexPath = join(base, 'index.html');

  const sendFile = (req, res, path, info) => {
    const isAsset = path.startsWith(join(base, 'assets') + sep);
    res.writeHead(200, {
      'Content-Type': TYPES[extname(path).toLowerCase()] ?? 'application/octet-stream',
      'Content-Length': info.size,
      'Cache-Control': isAsset ? 'public, max-age=31536000, immutable' : 'no-cache',
      'X-Content-Type-Options': 'nosniff',
    });
    if (req.method === 'HEAD') {
      res.end();
      return;
    }
    createReadStream(path)
      .on('error', () => res.destroy())
      .pipe(res);
  };

  const sendText = (req, res, status, type, body) => {
    res.writeHead(status, { 'Content-Type': type, 'Cache-Control': 'no-cache' });
    res.end(req.method === 'HEAD' ? undefined : body);
  };

  return async (req, res, pathname) => {
    const target = resolveUnder(base, pathname);
    if (target === null) {
      sendText(req, res, 404, 'text/plain; charset=utf-8', 'not found\n');
      return;
    }
    let info = await fileAt(target);
    if (info) {
      sendFile(req, res, target, info);
      return;
    }
    // A directory, or a client-side route.
    info = await fileAt(join(target, 'index.html'));
    if (info) {
      sendFile(req, res, join(target, 'index.html'), info);
      return;
    }
    if (extname(target) === '') {
      info = await fileAt(indexPath);
      if (info) {
        sendFile(req, res, indexPath, info);
        return;
      }
      sendText(req, res, 200, 'text/html; charset=utf-8', NOT_BUILT);
      return;
    }
    sendText(req, res, 404, 'text/plain; charset=utf-8', 'not found\n');
  };
}
