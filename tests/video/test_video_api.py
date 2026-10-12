"""Exercise the actual PHP endpoint on an isolated localhost server."""
import argparse
import hashlib
import http.client
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--php", required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    build = (Path(__file__).resolve().parent / "build").resolve()
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="video-api-", dir=build) as temporary:
        root = Path(temporary).resolve()
        assert root.parent == build  # Restrict automatic cleanup to this test's build directory.
        media = root / "media"
        token = secrets.token_hex(24)
        env = dict(os.environ, VIDEO_API_KEY=token, VIDEO_MEDIA_DIR=str(media))
        with socket.socket() as port_socket:
            port_socket.bind(("127.0.0.1", 0))
            port = port_socket.getsockname()[1]
        with (root / "php.log").open("wb") as log:
            process = subprocess.Popen(
                [args.php, "-n", "-S", f"127.0.0.1:{port}", "-t", str(repo / "backend/video-api")],
                env=env, stdout=log, stderr=log,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            try:
                for _ in range(100):
                    try:
                        with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                            break
                    except OSError:
                        if process.poll() is not None:
                            raise RuntimeError("PHP server failed to start")
                        time.sleep(0.05)
                else:
                    raise RuntimeError("PHP server did not listen")

                def request(method, path="/video.php?file=clip.avi", body=None, extra=None, authorized=True):
                    headers = {"Authorization": "Bearer " + token} if authorized else {}
                    headers.update(extra or {})
                    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
                    try:
                        connection.request(method, path, body=body, headers=headers)
                        response = connection.getresponse()
                        return response.status, dict(response.getheaders()), response.read()
                    finally:
                        connection.close()

                def upload(name, data):
                    boundary = "szpi-test-boundary"
                    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"{name}\"\r\n"
                            "Content-Type: video/x-msvideo\r\n\r\n").encode() + data + f"\r\n--{boundary}--\r\n".encode()
                    return request("POST", "/video.php", body, {"Content-Type": f"multipart/form-data; boundary={boundary}"})

                fixture = b"RIFF" + (4100).to_bytes(4, "little") + b"AVI " + bytes(range(256)) * 16
                assert request("GET", authorized=False)[0] == 401
                assert request("GET")[0] == 404
                assert upload("clip.avi", b"<html>not a video</html>")[0] == 415
                assert upload("clip.php", fixture)[0] == 400
                status, _, body = upload("clip.avi", fixture)
                assert status == 201, (status, body)
                assert json.loads(body)["sha256"] == hashlib.sha256(fixture).hexdigest()
                assert upload("clip.avi", fixture)[0] == 409
                status, headers, body = request("GET")
                assert status == 200 and body == fixture
                assert int(headers["Content-Length"]) == len(fixture)
                status, headers, body = request("HEAD")
                assert status == 200 and body == b"" and int(headers["Content-Length"]) == len(fixture)
                status, headers, body = request("GET", extra={"Range": "bytes=0-11"})
                assert status == 206 and body == fixture[:12]
                assert headers["Content-Range"] == f"bytes 0-11/{len(fixture)}"
                assert request("GET", extra={"Range": "bytes=-4"})[2] == fixture[-4:]
                assert request("GET", extra={"Range": "bytes=99999-"})[0] == 416
                assert request("GET", extra={"Range": "bytes=0-1,4-5"})[0] == 416
                assert request("GET", "/video.php?file=../clip.avi")[0] == 400
                assert request("GET", "/video.php?file[]=clip.avi")[0] == 400
                assert request("GET", "/media/clip.avi")[0] == 404
                assert request("DELETE")[0] == 405
                print("PHP upload/authentication/hash/HEAD/range/traversal tests passed.")
            finally:
                process.terminate()
                process.wait(timeout=10)


if __name__ == "__main__":
    main()
