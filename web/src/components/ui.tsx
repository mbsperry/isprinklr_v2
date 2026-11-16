// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { useEffect, useRef, type ReactNode } from 'react';
import { Link, NavLink } from 'react-router-dom';
import { clampMinutes, MAX_RUN_MINUTES, MIN_RUN_MINUTES } from '../lib/limits';
import { useController } from '../state';
import { CalendarIcon, ClockIcon, DropIcon, HomeIcon } from './Icons';

export function TabBar() {
  const tab = (to: string, label: string, icon: ReactNode, end = false) => (
    <NavLink to={to} end={end} className={({ isActive }) => 'tab' + (isActive ? ' active' : '')}>
      {icon}
      {label}
    </NavLink>
  );
  return (
    <div className="tabbar">
      <nav aria-label="Main">
        {tab('/', 'Home', <HomeIcon />, true)}
        {tab('/water', 'Water', <DropIcon />)}
        {tab('/schedules', 'Schedules', <CalendarIcon />)}
        {tab('/history', 'History', <ClockIcon />)}
      </nav>
    </div>
  );
}

export function Connection() {
  const { reachable, loaded } = useController();
  if (!loaded) return null;
  return reachable ? (
    <span className="conn" style={{ color: 'var(--ok)' }}><span className="dot" style={{ background: '#2f7d3e' }} />Connected</span>
  ) : (
    <span className="conn" style={{ color: 'var(--warn)' }}><span className="dot" style={{ background: '#c25e00' }} />Not connected</span>
  );
}

export function Back({ to, children }: { to: string; children: ReactNode }) {
  return <Link className="back" to={to}>‹ {children}</Link>;
}

export function ErrorNote({ message }: { message: string | null }) {
  if (!message) return null;
  return <div className="error" role="alert">{message}</div>;
}

export function Stepper({ value, onChange, label }: { value: number; onChange: (v: number) => void; label: string }) {
  return (
    <span className="stepper">
      <button type="button" aria-label={`Less ${label}`} onClick={() => onChange(clampMinutes(value - 1))} disabled={value <= MIN_RUN_MINUTES}>−</button>
      <input
        aria-label={label}
        inputMode="numeric"
        type="number"
        min={MIN_RUN_MINUTES}
        max={MAX_RUN_MINUTES}
        value={value}
        onChange={(e) => onChange(clampMinutes(Number(e.target.value)))}
      />
      <button type="button" aria-label={`More ${label}`} onClick={() => onChange(clampMinutes(value + 1))} disabled={value >= MAX_RUN_MINUTES}>+</button>
    </span>
  );
}

export interface SheetButton { label: string; kind?: 'primary' | 'danger'; onClick: () => void; disabled?: boolean }

/** A short confirm: one heading, buttons. No explanatory text (Matt's rule). */
export function ConfirmSheet({ title, buttons, onClose, error, cancelLabel = 'Cancel' }: { title: string; buttons: SheetButton[]; onClose: () => void; error?: string | null; cancelLabel?: string }) {
  const first = useRef<HTMLButtonElement>(null);
  useEffect(() => {
    first.current?.focus();
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') onClose(); };
    document.addEventListener('keydown', onKey);
    return () => document.removeEventListener('keydown', onKey);
  }, [onClose]);
  return (
    <div className="sheet-backdrop" onClick={onClose}>
      <div className="sheet" role="dialog" aria-modal="true" aria-labelledby="sheet-title" onClick={(e) => e.stopPropagation()}>
        <div className="grabber" aria-hidden="true" />
        <h2 id="sheet-title">{title}</h2>
        <ErrorNote message={error ?? null} />
        {buttons.map((b, i) => (
          <button
            key={b.label}
            ref={i === 0 ? first : undefined}
            type="button"
            className={'btn big block ' + (b.kind ?? '')}
            onClick={b.onClick}
            disabled={b.disabled}
          >
            {b.label}
          </button>
        ))}
        <button type="button" className="btn big block" onClick={onClose}>{cancelLabel}</button>
      </div>
    </div>
  );
}
