# SPDX-FileCopyrightText: 2026 Matt Sperry
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlatformIO pre-build script: warns when secrets.ini does not name the LAN NTP
# server. A warning, not an error, so CI and other machines still build.

Import("env")  # noqa: F821 (provided by PlatformIO)

config = env.GetProjectConfig()  # noqa: F821
ntp = config.get("secrets", "ntp_server", "").strip()

if not ntp:
    print(
        "\n*** WARNING: secrets.ini sets no ntp_server. This build has no LAN NTP\n"
        "*** server and uses only public ones, so after a reboot during a WAN\n"
        "*** outage the device has no valid time and will not water. For the\n"
        "*** installed device, copy secrets.example.ini to secrets.ini and set\n"
        "*** ntp_server to the router's address before building.\n"
    )
