# Security

Please do not publish real WLAN credentials, private API keys, or deployed media
paths in issues or pull requests.

The PHP image API uses a shared key intended for a small personal service. It is
not a full authentication system. For public deployments, put the API behind
normal HTTPS hosting, rotate the key if it leaks, and avoid exposing private
photo directories.

The firmware stores remembered WLAN credentials on the TF card for convenience.
Treat the TF card as sensitive if you save real networks on it.
