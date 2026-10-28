"""Lista wersji do aktualizacji radia przez sieć (versions.tsv w każdym wydaniu).

Radio czyta versions.tsv z najnowszego wydania (releases/latest/download/versions.tsv) i pokazuje
z niej wersje do instalacji. Na liście są: budowana właśnie wersja i kilka poprzednich wydań, które
same potrafią się aktualizować (pole "ota" ≥ 1 w ich firmware.json). Wydania starsze, bez OTA,
celowo nie trafiają na listę — radio po powrocie do nich nie mogłoby się już zaktualizować.

Format wiersza (tabulatory): wersja, ota, rozmiar, sha256, adres radio-app.bin. Od najnowszej.

Użycie (GitHub Actions; lokalnie działa bez tokenu na publicznym repozytorium):
    python tools/release_index.py --repo sewep/waveshare-s3-audio-radio --tag v1.2.0 \\
        --manifest dist/firmware.json --out dist/versions.tsv
"""

import argparse
import json
import os
import re
import sys
import urllib.request

KEEP = 6  # ile wersji trafia na listę (bieżąca + poprzednie)
VERSION_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$")


def version_key(v: str):
    m = VERSION_RE.match(v)
    if not m:
        return (-1, -1, -1, 0, "")
    major, minor, patch, pre = m.groups()
    # 1.2.0 jest nowsze niż 1.2.0-beta: wydanie bez przyrostka sortuje się wyżej
    return (int(major), int(minor), int(patch), 0 if pre else 1, pre or "")


def fetch(url: str):
    req = urllib.request.Request(url, headers={"Accept": "application/vnd.github+json", "User-Agent": "release-index"})
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if token and url.startswith("https://api.github.com/"):
        req.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read()


def entry(version: str, ota: int, app: dict, url: str) -> dict:
    return {"version": version, "ota": ota, "size": app["size"], "sha256": app["sha256"], "url": url}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", required=True)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--manifest", required=True, help="firmware.json budowanej wersji")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    base = f"https://github.com/{args.repo}/releases/download/"
    with open(args.manifest, encoding="utf-8") as f:
        cur = json.load(f)
    entries = []
    if cur.get("ota", 0) >= 1:
        entries.append(entry(cur["version"], cur["ota"], cur["app"], f"{base}{args.tag}/{cur['app']['file']}"))
    else:
        print("Budowana wersja nie ma OTA — nie trafia na listę", file=sys.stderr)

    releases = json.loads(fetch(f"https://api.github.com/repos/{args.repo}/releases?per_page=50"))
    for rel in releases:
        tag = rel["tag_name"]
        if rel["draft"] or rel["prerelease"] or tag == args.tag:
            continue
        assets = {a["name"]: a["browser_download_url"] for a in rel["assets"]}
        if "firmware.json" not in assets:
            print(f"{tag}: brak firmware.json, pomijam", file=sys.stderr)
            continue
        fw = json.loads(fetch(assets["firmware.json"]))
        app = fw.get("app", {})
        url = assets.get(app.get("file", ""), "")
        if fw.get("ota", 0) < 1:
            print(f"{tag}: bez OTA, pomijam", file=sys.stderr)
            continue
        if not url.startswith(base) or not VERSION_RE.match(fw.get("version", "")):
            print(f"{tag}: niepełny firmware.json, pomijam", file=sys.stderr)
            continue
        entries.append(entry(fw["version"], fw["ota"], app, url))

    entries.sort(key=lambda e: version_key(e["version"]), reverse=True)
    entries = entries[:KEEP]
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("# wersja\tota\trozmiar\tsha256\tadres\n")
        for e in entries:
            f.write(f"{e['version']}\t{e['ota']}\t{e['size']}\t{e['sha256']}\t{e['url']}\n")
    for e in entries:
        print(f"{e['version']}  ota={e['ota']}  {e['size']} B  {e['url']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
