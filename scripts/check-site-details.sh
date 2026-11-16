#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Matt Sperry
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fails if a tracked file holds what looks like a site detail: a private
# IPv4 address, a LAN host name (.lan, .local, .home.arpa, .internal), or a
# home directory path. The repo is public; site values belong in gitignored
# files (firmware/secrets.ini, server.env) or environment variables.
#
# Test fixtures that need private addresses are excluded by path below.
# Placeholders that are fine anywhere are listed in ALLOWED (`time.local` is
# a field in the status page script, not a host).

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

PATTERN='\b(10\.[0-9]{1,3}|172\.(1[6-9]|2[0-9]|3[01])|192\.168)\.[0-9]{1,3}\.[0-9]{1,3}\b'
PATTERN+='|\b[A-Za-z0-9-]+\.(lan|local|home\.arpa|internal)\b'
PATTERN+='|/Users/[A-Za-z]|/home/[a-z][a-z0-9_-]*/'

EXCLUDES=(
  ':!firmware/test/**'
  ':!firmware/lib/api/src/cors.h'
  ':!**/package-lock.json'
  ':!scripts/check-site-details.sh'
)

ALLOWED='\b(myhost|server|sprinkler|device|ct|time)\.(lan|local|home\.arpa)\b'

hits=$(git grep -nIE "$PATTERN" -- . "${EXCLUDES[@]}" | grep -vE "$ALLOWED" || true)

if [ -n "$hits" ]; then
  echo "Possible site details in tracked files (the repo is public):"
  echo "$hits"
  echo
  echo "Move real values to firmware/secrets.ini, server.env or an environment"
  echo "variable. For a placeholder, use a name in ALLOWED in this script or an"
  echo "address from 192.0.2.0/24."
  exit 1
fi
echo "No site details found."
