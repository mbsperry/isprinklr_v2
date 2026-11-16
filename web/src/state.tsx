// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// One place that polls the controller. Status is polled; zones, schedules and
// config are re-read whenever the status's settings_hash changes.

import { createContext, useCallback, useContext, useEffect, useRef, useState, type ReactNode } from 'react';
import { esp } from './api/esp';
import type { DeviceConfig, SchedulesResponse, Status, Zone } from './api/types';
import { messageFor } from './lib/errors';
import { isRunning } from './lib/health';

interface Controller {
  status: Status | null;
  reachable: boolean;
  loaded: boolean;
  zones: Zone[];
  schedules: SchedulesResponse | null;
  config: DeviceConfig | null;
  refresh: () => Promise<void>;
  zoneName: (zone: number) => string;
}

const Ctx = createContext<Controller | null>(null);

const FAST_MS = 2000;
const SLOW_MS = 10000;

export function ControllerProvider({ children }: { children: ReactNode }) {
  const [status, setStatus] = useState<Status | null>(null);
  const [reachable, setReachable] = useState(true);
  const [loaded, setLoaded] = useState(false);
  const [zones, setZones] = useState<Zone[]>([]);
  const [schedules, setSchedules] = useState<SchedulesResponse | null>(null);
  const [config, setConfig] = useState<DeviceConfig | null>(null);
  const hashRef = useRef<string | undefined>(undefined);
  const timer = useRef<number | undefined>(undefined);

  const loadSettings = useCallback(async () => {
    const [z, s, c] = await Promise.all([esp.zones(), esp.schedules(), esp.config()]);
    setZones(z);
    setSchedules(s);
    setConfig(c);
  }, []);

  const refresh = useCallback(async () => {
    window.clearTimeout(timer.current);
    let next = SLOW_MS;
    try {
      const st = await esp.status();
      setStatus(st);
      setReachable(true);
      if (st.settings_hash !== hashRef.current) {
        await loadSettings();
        hashRef.current = st.settings_hash;
      }
      if (isRunning(st)) next = FAST_MS;
    } catch {
      setReachable(false);
    } finally {
      setLoaded(true);
      if (!document.hidden) timer.current = window.setTimeout(refresh, next);
    }
  }, [loadSettings]);

  useEffect(() => {
    void refresh();
    const onVisible = () => { if (!document.hidden) void refresh(); };
    document.addEventListener('visibilitychange', onVisible);
    return () => {
      window.clearTimeout(timer.current);
      document.removeEventListener('visibilitychange', onVisible);
    };
  }, [refresh]);

  const zoneName = useCallback(
    (n: number) => zones.find((z) => z.zone === n)?.name ?? `Zone ${n}`,
    [zones],
  );

  return (
    <Ctx.Provider value={{ status, reachable, loaded, zones, schedules, config, refresh, zoneName }}>
      {children}
    </Ctx.Provider>
  );
}

export function useController(): Controller {
  const c = useContext(Ctx);
  if (!c) throw new Error('useController outside ControllerProvider');
  return c;
}

/** Run an action, refresh status after, and keep a short message for the UI. */
export function useAction() {
  const { refresh } = useController();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const run = useCallback(
    async (fn: () => Promise<unknown>): Promise<boolean> => {
      setBusy(true);
      setError(null);
      try {
        await fn();
        return true;
      } catch (e) {
        setError(messageFor(e));
        return false;
      } finally {
        setBusy(false);
        void refresh();
      }
    },
    [refresh],
  );
  return { busy, error, setError, run };
}
