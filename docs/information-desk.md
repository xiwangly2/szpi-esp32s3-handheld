# Information Desk

The fourth launcher page has `信息台`, with weather, RSS, and service-status
tabs. Lists scroll vertically. Refresh is manual or once every five minutes
while the page is open. Leaving closes the worker and releases its memory.
The device consumes a compact authenticated JSON response, not raw XML or HTML.

## Device Configuration

### Local Web Setup

Open `信息台` > gear > Wi-Fi symbol. Choose `同一WLAN` to use the device's
current WLAN address, or `临时热点` to temporarily enable AP+STA mode. The
device displays the page URL and a six-digit pairing code. Hotspot mode also
displays a random WPA2 password and a Wi-Fi QR; the top-right button toggles
between joining the hotspot and opening the web-page QR. No cloud registration
is needed and no extra cloud resources are created.

The browser can edit the service URL/token directly or select the existing
private `.deploy/info.ini` using `导入 INI`. This reads the file in the browser;
it does not require inserting a TF card. Saving writes the configuration to
device NVS, then closes setup after five seconds and refreshes the information
desk. Leaving the token empty retains it; replacement and clearing are explicit.
Stored tokens are never read back through the web API. The portal configures
this information service only, not WLAN credentials or backend feed/location
preferences. Those remain separate settings.

All web assets are embedded, so the temporary hotspot does not need Internet
access. AP+STA keeps the station interface enabled; both interfaces share one
radio/channel, with the external AP's channel taking priority. There is **no
NAT or Internet sharing**. The information fetch worker pauses during setup to
limit memory usage. Returning from setup restores STA mode without intentionally
disconnecting the existing station. On first use without saved WLAN, open the
WLAN application once to initialize its driver before starting a setup hotspot.

The portal uses local HTTP on port 8080, not browser-trusted HTTPS. Use the
same-WLAN mode only on a trusted LAN; it does not protect against active LAN
interception. Prefer the temporary WPA2 hotspot on shared networks. Its server
accepts connections only on the selected interface/address, validates Host and
Origin, has no CORS, issues an in-memory 128-bit session after pairing, bounds
request sizes, and closes after five minutes or eight incorrect pairing attempts.
The password/PIN are displayed locally but not logged. Closing the page cancels
setup. The hotspot is not configured to persist across boots. Never expose this
HTTP service through port forwarding.

URL and token are now saved as one NVS blob. Existing separate URL/token keys
are read as a migration fallback; after saving with this firmware, older firmware
does not read that newer blob and may show its older settings.

### TF Import (Optional)

For offline import, the private generated
`backend/serverless-media/.deploy/info.ini` belongs at `/szpi/config/info.ini` on TF.
Open the information desk; on first use the TF file is loaded automatically.
The settings gear has an HTTPS URL, a masked read token, TF import and Save.
Saved NVS settings take precedence over TF; use TF import then Save to replace
them. Saving empty fields explicitly clears the device settings. NVS settings
work without TF. User-edited settings are not automatically written back to TF.

```ini
url=https://YOUR_API_HOST/info
token=YOUR_READ_ONLY_APPLICATION_TOKEN
```

HTTPS certificate verification stays enabled. Query tokens and URL passwords
are not accepted by this UI: the token travels in an Authorization header, and
redirects are refused. Never put AWS/Cloudflare/Tencent/Alibaba account keys on
the handheld. The configuration file contains a plaintext application token;
keep populated copies out of Git, screenshots and shared firmware packages.

During a request error the open page retains its previous contents and reports
the failure. This device cache is RAM-only and is released on exit. Upstream
stale data is separately marked by the backend, including its original fetch
timestamp. A source failure is not a claim that the monitored service is down.

## Backend Sources

The existing serverless media backend also exposes `GET /info`, authenticated
with its independent read/admin tokens. Existing media/upload routes remain
unchanged. No database, extra function or public storage bucket is required.

Copy `backend/serverless-media/info.example.json` to its ignored `.deploy/info.json`
and edit it before an AWS deployment. For other providers set the same JSON in
the `information_config` Terraform variable. Empty configuration disables all
upstream requests. The example location is Shenzhen, not detected user location.

- Weather: Open-Meteo current temperature, apparent temperature, humidity,
  weather code and wind speed, configured with a label/latitude/longitude.
  Attribution is displayed. The free endpoint is intended for non-commercial
  use; review [Open-Meteo terms](https://open-meteo.com/en/terms) before commercial
  deployment and adapt the endpoint/account accordingly.
- RSS/Atom: up to two deployment-configured feeds and six total entries.
  Public HTTPS hosts are explicitly allowlisted in `src/information.mjs`.
  Arbitrary client URLs, redirects, credentials and query strings are rejected.
  Custom sources need a deliberate allowlist review, not an open proxy.
- Status: GitHub and Cloudflare official Statuspage APIs. These are the
  providers' published status, not active probes of your own servers.

Responses are capped at 16 KiB. Upstreams have 4.5-second deadlines and bounded
decompressed bodies. The SAX parser rejects DTDs, malformed XML and deep nesting.
Warm instances coalesce requests and cache for five minutes; failed refreshes
retry after one minute and can serve a marked last-success result for 30 minutes.
This cache is per instance, not a distributed cache. Cold starts and parallel
instances can refetch; apply platform rate/concurrency limits for larger fleets.

## Deployment And Evidence

Use the [serverless deployment guide](../backend/serverless-media/README.md).
The AWS command now defaults to plan-only; `--apply` is explicit. It backs up
local state before init, refuses deletion/replacement plans and will not silently
regenerate missing tokens for an existing deployment. Keep state/backups private.

On 2026-10-15 the existing AWS Lambda was updated with user approval: zero new
resources, one in-place update, zero deletions. `/info` returned 401 anonymously
and 200 with the read token; all three sources reported `ok`. The response was
1139 bytes. The existing video download still returned 307. This demonstrates
AWS behavior, not other-cloud deployment or device touch/render acceptance.

Terraform validate passed for all four templates. OpenTofu 1.13.1 init/validate
also passed in an isolated directory; production state was not migrated.
OpenTofu selected AWS 6.68.0, Archive 2.8.1, Cloudflare 5.28.0, Tencent 1.83.35,
Alibaba 1.294.0 there. Its registry/lock-file changes stayed in that directory.
Tencent/Alibaba providers had no registry GPG signature; their reported
validation is schema validation, not a complete supply-chain certification.

## Checks

Run `npm test` and `npm run build` in `backend/serverless-media`.
Firmware payload checks run with:

```powershell
cmake -S tests/information -B tests/information/build
cmake --build tests/information/build --config Debug
ctest --test-dir tests/information/build -C Debug --output-on-failure
```

Set `CJSON_SOURCE_DIR` at configure time if ESP-IDF's cJSON is elsewhere.
Hardware checks: import/save/reopen configuration; switch all three tabs;
scroll RSS; refresh while connected/disconnected; return during a request and
reopen quickly. Check no reboot, old-page update, leaked token or overlap.

Local setup checks also cover Host/Origin rejection, missing/incorrect session
tokens, duplicate/malformed JSON fields, explicit token keep/replace/clear,
and wrong PIN input. `tools/setup-preview.mjs` serves an offline browser fixture
on a random loopback port with synthetic PIN `123456`; it never accesses the
device, cloud or real configuration. Browser checks on 2026-10-15 covered wrong
PIN, pairing, INI selection/import and save confirmation at 360x740 and
1280x800. This fixture does not prove actual hotspot radio behavior or NVS writes.
Real-device acceptance must additionally check AP+STA join, persistent settings
after reboot, auto-close/expiry, returning while setup starts, and recovery of
internal memory after repeated open/close cycles.
