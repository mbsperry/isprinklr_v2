// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

import { Route, Routes, useLocation } from 'react-router-dom';
import { TabBar } from './components/ui';
import Details from './screens/Details';
import History from './screens/History';
import Home from './screens/Home';
import ScheduleEdit from './screens/ScheduleEdit';
import Schedules from './screens/Schedules';
import Settings from './screens/Settings';
import Several from './screens/Several';
import Water from './screens/Water';
import Watering from './screens/Watering';
import Zones from './screens/Zones';

export default function App() {
  const { pathname } = useLocation();
  // The schedule editor has its own save bar and no tabs.
  const showTabs = !/^\/schedules\/.+/.test(pathname);
  return (
    <>
      <Routes>
        <Route path="/" element={<Home />} />
        <Route path="/water" element={<Water />} />
        <Route path="/water/several" element={<Several />} />
        <Route path="/watering" element={<Watering />} />
        <Route path="/schedules" element={<Schedules />} />
        <Route path="/schedules/new" element={<ScheduleEdit key="new" />} />
        <Route path="/schedules/:name" element={<ScheduleEdit key={pathname} />} />
        <Route path="/history" element={<History />} />
        <Route path="/details" element={<Details />} />
        <Route path="/zones" element={<Zones />} />
        <Route path="/settings" element={<Settings />} />
        <Route path="*" element={<Home />} />
      </Routes>
      {showTabs && <TabBar />}
    </>
  );
}
