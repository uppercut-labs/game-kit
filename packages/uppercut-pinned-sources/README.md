# uppercut-pinned-sources

Fetches the build inputs a project lists with a SHA-256 (source images, sound banks, archives) into a cache directory. A file already in the cache is hashed and reused when it matches, so a build with an intact cache never touches the network. A missing file is downloaded, hashed and written into place only when it matches its pin. Any mismatch, in a download or in the cache, is refused, and the cache is never silently replaced. Each call reports what it fetched and what it reused. Standard library only.

```sh
pip install uppercut-pinned-sources
```

## Manifest

One source per line, three fields separated by spaces or tabs: the SHA-256 in lowercase hex, the file name in the cache, and where to download it from. The location is a full URL, or a path joined to a base URL given at fetch time. Blank lines and lines starting with `#` are skipped.

```text
# sha256 name location
708373b2af7fd07ac8de1500556006aaf904c41a99985a849cacc78874f38eac aerial_rocks_02.jpg Textures/jpg/1k/aerial_rocks_02/aerial_rocks_02_diff_1k.jpg
4084ddd6c57d76e05ac7de9f563077aabd77d07f6b03593d0930a3fc515494b4 sky.jpg https://example.com/files/sky%20panorama.jpg
```

The name must be a plain file name (no `/` or `\`) and may appear once. A relative location is joined with `urllib.parse.urljoin` after a `/` is added to the base when it has none, so `Textures/a.jpg` on `https://host/files` becomes `https://host/files/Textures/a.jpg`. Keep locations already percent-encoded, as the example's `%20` is.

## Command line

```sh
uppercut-pinned-sources art/sources.txt ~/.cache/game-art --base-url https://dl.polyhaven.org/file/ph-assets/
uppercut-pinned-sources art/sources.txt ~/.cache/game-art --offline
python -m uppercut_pinned_sources art/sources.txt cache --base-url URL --user-agent "Game/1.0 asset build" --quiet
```

It prints one line per source, `downloaded NAME (SIZE bytes) from URL` or `cached NAME (SIZE bytes)`, unless `--quiet` is given. `--offline` never downloads: a missing file is an error. `--timeout` sets the seconds per download (default 120). The exit code is 0 on success, 1 for a mismatch, a bad manifest or a file missing offline, and 2 for a failed download or a file that cannot be read or written.

## Python

```python
from pathlib import Path
from uppercut_pinned_sources import describe, fetch_manifest

results = fetch_manifest(
    Path(__file__).with_name("sources.txt"),
    cache,
    base_url="https://dl.polyhaven.org/file/ph-assets/",
    user_agent="Game/1.0 asset build",
    report=lambda result: print(describe(result)),
)
```

| Name | What it does |
| --- | --- |
| `fetch_manifest(manifest, cache, **options)` | Reads the manifest file and fetches every source into `cache`, taking the options of `fetch_sources` |
| `fetch_sources(sources, cache, *, base_url=None, user_agent=..., timeout=120, download=None, offline=False, report=None)` | Fetches each source in order and stops at the first error. Returns a list of `FetchResult`. `report` is called with each result as it is made |
| `fetch_source(source, cache, *, base_url=None, download=None, offline=False)` | One source |
| `read_manifest(path)`, `parse_manifest(text, origin="manifest")` | A list of `PinnedSource(digest, name, location)`. `PinnedSource.url(base_url)` gives the download URL |
| `FetchResult` | `source`, `path` in the cache, `action` (`"cached"` or `"downloaded"`), `size` in bytes, and `url` for a download |
| `describe(result)` | The one-line report the command line prints |
| `url_download(*, user_agent, timeout)` | The default download function over `urllib` (HTTP, HTTPS and `file://`) |
| `sha256_file(path)` | The SHA-256 of a file, read in pieces |
| `PinnedSourceError` | A `ValueError`: a bad manifest line, a SHA-256 mismatch, or a file missing offline |
| `DownloadError` | An `OSError`: the download itself failed |

`download` is any function from a URL to its bytes. Pass one to add authentication, a mirror or a test double; the hash check still applies to whatever it returns.

## What it guarantees

- A cached file is reused only when its SHA-256 matches its pin. A cached file that does not match raises `PinnedSourceError` and is left in place for a person to look at.
- A download is checked before anything is written. It goes to a temporary file next to its final name and is renamed into place, so a failed or interrupted download leaves no partial file.
- Downloads happen one at a time, in manifest order, with the `User-Agent` and timeout given. Nothing else is fetched.

## Working on it

From the repository root:

```sh
uv sync --all-packages && uv run pytest packages/uppercut-pinned-sources
```

The tests serve made-up bytes through a download function and through `file://` URLs. They never use the network.

## License

MIT
