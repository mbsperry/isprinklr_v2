# SPDX-FileCopyrightText: 2026 Matt Sperry
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlatformIO pre-build script: warns when site settings that live outside git
# are missing from the environment. A warning, not an error, so CI and other
# machines still build.

import os

Import("env")  # noqa: F821 (provided by PlatformIO)

if not os.environ.get("ISPRINKLR_NTP_SERVER"):
    print(
        "\n*** WARNING: ISPRINKLR_NTP_SERVER is not set. This build has no LAN NTP\n"
        "*** server and uses only public ones, so after a reboot during a WAN\n"
        "*** outage the device has no valid time and will not water. For the\n"
        "*** installed device, set it to the router's address before building.\n"
    )
