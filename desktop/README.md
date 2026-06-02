# FluxGate Desktop

A native control-panel app for **macOS** and **Linux**, built with Electron. It's a
thin, good-looking front end over FluxGate's local admin API — it does not replace
the proxy, it drives it.

What it gives you over the browser dashboard:

- A real app window with a sidebar (Overview · Requests · Providers · Settings · Logs).
- **Start / Stop the proxy** from the title bar — the app spawns the `FluxGate`
  binary and streams its logs into the Logs tab.
- Live metrics, request inspector, per-provider analytics and runtime settings,
  exactly like the web panel, but as a desktop app with a dock icon.

## Run in development

```sh
cd desktop
npm install
npm start
```

By default it connects to `http://127.0.0.1:9090`. If your proxy uses a different
admin host/port or a Bearer token, set it in **Settings → Connection**.

The app auto-detects the `FluxGate` binary at `../build/FluxGate`, then on `PATH`
and common install locations. Override it in **Settings → Launch options**.

## Build installers

```sh
npm run dist          # current platform
npm run dist:mac      # .dmg + .zip
npm run dist:linux    # AppImage + .deb
```

Output lands in `desktop/dist/`. Building for Linux from macOS (and vice-versa)
may require Docker; see [electron-builder docs](https://www.electron.build/).

## How it connects

The proxy's admin server (`--admin host:port`, default `127.0.0.1:9090`) exposes a
small JSON API the app polls: `/stats`, `/providers`, `/requests`, `/api/config`,
`POST /api/control`, `POST /api/cache/clear`. CORS is open and Bearer-token auth is
honoured when a token is configured.
