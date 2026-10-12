# Portable Serverless Video API

One HTTP contract, with adapters for AWS S3, Cloudflare R2, Tencent COS, and
Alibaba OSS. Functions authenticate requests and sign direct object-storage
URLs. **Video bytes do not pass through Lambda/API Gateway or function memory.**
Transcoding stays on the PC using `tools/powershell/convert-video.ps1`.

## Deployment Status

| Target | Runtime / storage | Evidence |
| --- | --- | --- |
| AWS | Lambda Node 22 + HTTP API + private S3 | Deployed in ap-east-1; upload, redirect, full download and SHA256 verified |
| Cloudflare | Worker + private R2 | Bundle and signature tests pass; Terraform validates; not deployed to an account |
| Tencent | SCF HTTP container + private COS | Native SDK signature test and Terraform validation; no cloud deployment |
| Alibaba | FC v3 HTTP container + private OSS | Native SDK signature test and Terraform validation; no cloud deployment |

The non-AWS templates are starting configurations, not claims of account-level
qualification. Regions, service activation, container-registry access, public
HTTP endpoints, and provider quotas still need validation on those accounts.

## AWS: One Command After Setup

Requires Node.js 22+, Terraform 1.6+, Git LFS, and configured AWS credentials.
The AWS CLI is useful for inspection, but the deployed Lambda uses its own IAM
execution role, **not the PC's access key**.

```powershell
cd backend/serverless-media
npm ci
npm test
npm run build
npm run deploy:aws
npm run upload -- ..\..\release\video\BadApple.avi
```

Set `AWS_REGION` to change the default `ap-east-1` region before initial deploy.
The script uses `terraform init`, `validate`, saved `plan`, and `apply`; a repeat
run is idempotent. Use `node tools/deploy-aws.mjs --plan-only` to review first.
Set `TERRAFORM_BIN=tofu` to use OpenTofu with the same configuration; OpenTofu
execution has not been tested in this checkout.

Outputs are saved to `.deploy/aws.json`. Independent random admin/read tokens
are generated once and retained in `.deploy/aws-secrets.json`. The ready-to-use
device config is `.deploy/video.ini`; put it at `/szpi/config/video.ini` on TF.
Then use `网络` in the handheld video player. The stable API URL issues a new
short-lived download URL each time, so the TF config itself does not expire.

Terraform creates a dedicated bucket, scoped IAM role, Lambda, HTTP API with
5 requests/second throttling, and seven-day function logs. Bucket deletion is
protected and `force_destroy` is false. Deleting infrastructure does not
silently erase videos. Cloud usage is billed to the selected account.

## Common API

- `GET /health`: public status; no credentials or bucket information.
- `GET /media/BadApple.avi`: read/admin Bearer token, or `?key=READ_TOKEN` for
  the ESP client. Returns HTTP 307 to an expiring signed object URL.
- `HEAD /media/BadApple.avi`: authenticated metadata, including Content-Length.
- `POST /uploads/BadApple.avi`: admin **Bearer** token only. Send JSON with
  `size`, `sha256` (hex), and `md5` (base64). Returns a PUT URL and the exact
  headers the upload client must send. Do not forward the API Bearer token to
  object storage. The supplied CLI handles this correctly.

Filenames are ASCII `.avi` basenames. Manifests are capped at 2 KiB, videos at
256 MiB. Direct uploads carry checksum and no-overwrite headers. Existing names
return 409, so use versioned filenames. This API transports converted media;
the device checks the AVI/MJPEG/PCM format before playing it.

## Cloudflare

Build once with `npm run build`, then use `infra/cloudflare`. Provide
`CLOUDFLARE_API_TOKEN` for Terraform and private variables `account_id`,
`read_token`, `admin_token`, `r2_access_key_id`, and `r2_secret_access_key`.
The R2 S3 credentials should be limited to this bucket. They are Worker secrets,
not deployment-account credentials. The module creates the R2 bucket, Worker,
and enables its workers.dev endpoint. Workers/R2 must be activated first.

```powershell
terraform -chdir=infra/cloudflare init
terraform -chdir=infra/cloudflare apply
```

## Tencent And Alibaba

Both use the same Node 22 HTTP container (`PORT=9000`) with native COS/OSS
signers, because S3 compatibility is not identical across providers.

```powershell
npm run build
docker build -t YOUR_REGISTRY/szpi-media:1 .
docker push YOUR_REGISTRY/szpi-media:1
terraform -chdir=infra/tencent init
terraform -chdir=infra/tencent apply
# Or use infra/alicloud for FC v3.
```

Provide `image`, `bucket_name`, the independent read/admin tokens, and
**bucket-scoped** `storage_access_key` / `storage_secret_key` variables. Do not
use an account-root key for these storage variables. Terraform provider
authentication is configured separately through each provider's normal
credential chain. COS bucket names include the account APPID suffix. Tencent's
template uses a personal TCR image; enterprise registries need their registry ID
and matching image configuration. Enable/configure an external SCF HTTP URL or
API Gateway according to the account's HTTP-function support. Alibaba's module
creates an anonymous HTTP trigger; application tokens still protect media.

The container build, registry push, native provider IAM policies, and actual
SCF/FC runtime behavior have not been qualified on those accounts. The templates
do not change an existing domain, gateway, or network. urllib proxy mode is not
supported in the bundled OSS adapter.

## State And Source Control

Commit code, Terraform files, provider lock files, and `package-lock.json`.
Never commit `.deploy`, `.dev.vars`, Terraform state/plans/secret variable files,
or uploaded personal media. Terraform state contains runtime secrets even when
console output marks them sensitive. For shared operations, use a protected
remote state backend with locking and access control; preserve the local state
until it is migrated. Do not run apply from an empty state against this existing
deployment; retain the state or import the existing resources.

Test with `npm test` and `terraform validate` in each provider directory.
The signature tests are offline checks; they do not prove non-AWS uploads.
