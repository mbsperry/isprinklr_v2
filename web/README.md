# web

React frontend. Vite.

Nothing here yet. Rebuilt from scratch rather than ported: v1 used
`react-scripts` 5 (Create React App, unmaintained).

v1's `use_v2_api` branch is the reference for endpoint shapes — its `main` is
stale and calls endpoints that no longer exist.

## Notes for when this gets built

- **Two base URLs.** The ESP for status, control, and schedules; the Node service
  for log history. v1 had a single `API_SERVER`; that no longer fits.
- Served as a static build by [`../server/`](../server/). No nginx.
