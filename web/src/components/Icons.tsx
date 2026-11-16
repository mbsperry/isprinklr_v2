// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

const base = { width: 24, height: 24, viewBox: '0 0 24 24', fill: 'none', stroke: 'currentColor', strokeWidth: 2, strokeLinecap: 'round' as const, strokeLinejoin: 'round' as const, 'aria-hidden': true };

export const HomeIcon = () => <svg {...base}><path d="M4 11l8-7 8 7v9a1 1 0 0 1-1 1h-5v-6h-4v6H5a1 1 0 0 1-1-1z" /></svg>;
export const DropIcon = () => <svg {...base}><path d="M12 3c3 4 6 7.5 6 11a6 6 0 0 1-12 0c0-3.5 3-7 6-11z" /></svg>;
export const CalendarIcon = () => <svg {...base}><rect x="3" y="5" width="18" height="16" rx="2" /><path d="M3 10h18M8 3v4M16 3v4" /></svg>;
export const ClockIcon = () => <svg {...base}><circle cx="12" cy="12" r="9" /><path d="M12 7v5l3 2" /></svg>;
export const CheckCircle = () => <svg {...base} width={34} height={34} strokeWidth={2.2}><circle cx="12" cy="12" r="10" /><path d="M7.5 12.5l3 3 6-6.5" /></svg>;
export const Warn = () => <svg {...base} width={34} height={34} strokeWidth={2.2}><path d="M12 3l10 18H2z" /><path d="M12 10v5M12 18v.5" /></svg>;
export const Chevron = () => <svg {...base} width={20} height={20}><path d="M9 6l6 6-6 6" /></svg>;
export const Close = () => <svg {...base} width={18} height={18}><path d="M6 6l12 12M18 6L6 18" /></svg>;
export const Pencil = () => <svg {...base} width={18} height={18}><path d="M4 20h4L19 9l-4-4L4 16z" /></svg>;
export const Tick = () => <svg {...base} width={14} height={14} strokeWidth={3.5} stroke="#fff"><path d="M5 12.5l4.5 4.5L19 7" /></svg>;
export const SmallTick = () => <svg {...base} width={16} height={16} strokeWidth={3}><path d="M5 12.5l4.5 4.5L19 7" /></svg>;
export const SmallBang = () => <svg {...base} width={16} height={16} strokeWidth={3}><path d="M12 6v8M12 18v.5" /></svg>;
export const SmallDash = () => <svg {...base} width={16} height={16} strokeWidth={2.5}><path d="M6 12h12" /></svg>;
