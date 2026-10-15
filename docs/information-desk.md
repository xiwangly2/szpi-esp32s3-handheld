# Information Desk

The fourth launcher page has `信息台`, with weather, RSS, and service-status
tabs. Lists scroll vertically. Refresh is manual or once every five minutes
while the page is open. Leaving closes the worker and releases its memory.
The device consumes a compact authenticated JSON response, not raw XML or HTML.

## Device Configuration

After AWS deployment, copy the private generated
`backend/serverless-media/.deploy/info.ini` to `/szpi/config/info.ini` on TF.
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
