import hashlib
import subprocess
import sys

import pytest

from uppercut_pinned_sources import (
    DownloadError,
    PinnedSource,
    PinnedSourceError,
    describe,
    fetch_manifest,
    fetch_source,
    fetch_sources,
    main,
    parse_manifest,
    read_manifest,
    url_download,
)

STONE = b"synthetic stone texture bytes"
SKY = b"synthetic sky panorama bytes" * 50


def sha(data):
    return hashlib.sha256(data).hexdigest()


def manifest_text():
    return (
        f"{sha(STONE)} stone.jpg Textures/stone%20one.jpg\n"
        f"{sha(SKY)}\tsky.jpg\tHDRIs/sky.jpg\n"
    )


class Server:
    """A download function that serves fixed bytes by URL and records each request."""

    def __init__(self, files):
        self.files = files
        self.requests = []

    def __call__(self, url):
        self.requests.append(url)
        if url not in self.files:
            raise DownloadError(f"{url}: 404")
        return self.files[url]


BASE = "https://assets.example/files/"


@pytest.fixture
def server():
    return Server({BASE + "Textures/stone%20one.jpg": STONE, BASE + "HDRIs/sky.jpg": SKY})


@pytest.fixture
def manifest(tmp_path):
    path = tmp_path / "sources.txt"
    path.write_text(manifest_text(), encoding="utf-8")
    return path


def test_parses_the_three_field_format():
    sources = parse_manifest("# comment\n\n" + manifest_text())
    assert sources == [
        PinnedSource(sha(STONE), "stone.jpg", "Textures/stone%20one.jpg"),
        PinnedSource(sha(SKY), "sky.jpg", "HDRIs/sky.jpg"),
    ]


@pytest.mark.parametrize(
    "line, message",
    [
        ("abc stone.jpg x", "is not a lowercase SHA-256"),
        (f"{sha(STONE).upper()} stone.jpg x", "is not a lowercase SHA-256"),
        (f"{sha(STONE)} stone.jpg", "expected SHA256 NAME LOCATION, found 2 fields"),
        (f"{sha(STONE)} ../stone.jpg x", "must be a plain file name"),
        (f"{sha(STONE)} sub\\stone.jpg x", "must be a plain file name"),
        (f"{sha(STONE)} a x\n{sha(SKY)} a y", "a is listed twice"),
    ],
)
def test_refuses_bad_manifest_lines(line, message):
    with pytest.raises(PinnedSourceError, match=message):
        parse_manifest(line, origin="sources.txt")


def test_bad_line_names_its_line_number():
    with pytest.raises(PinnedSourceError, match=r"^sources.txt:2: "):
        parse_manifest(manifest_text().splitlines()[0] + "\nnot a line\n", origin="sources.txt")


def test_urls_join_the_base_or_stand_alone():
    relative = PinnedSource(sha(STONE), "a", "dir/a.jpg")
    assert relative.url("https://host/root") == "https://host/root/dir/a.jpg"
    assert relative.url("https://host/root/") == "https://host/root/dir/a.jpg"
    assert PinnedSource(sha(STONE), "a", "https://other/a.jpg").url("https://host/") == "https://other/a.jpg"
    with pytest.raises(PinnedSourceError, match="no base URL"):
        relative.url(None)


def test_downloads_missing_files_then_reuses_the_cache(tmp_path, manifest, server):
    cache = tmp_path / "cache"
    first = fetch_manifest(manifest, cache, base_url=BASE, download=server)
    assert [(r.source.name, r.action, r.size) for r in first] == [
        ("stone.jpg", "downloaded", len(STONE)),
        ("sky.jpg", "downloaded", len(SKY)),
    ]
    assert (cache / "stone.jpg").read_bytes() == STONE
    assert describe(first[0]) == f"downloaded stone.jpg ({len(STONE)} bytes) from {BASE}Textures/stone%20one.jpg"
    second = fetch_manifest(manifest, cache, base_url=BASE, download=server)
    assert [r.action for r in second] == ["cached", "cached"]
    assert describe(second[1]) == f"cached sky.jpg ({len(SKY)} bytes)"
    assert len(server.requests) == 2
    assert sorted(path.name for path in cache.iterdir()) == ["sky.jpg", "stone.jpg"]


def test_an_intact_cache_works_offline_without_a_base_url(tmp_path, manifest, server):
    cache = tmp_path / "cache"
    fetch_manifest(manifest, cache, base_url=BASE, download=server)

    def no_network(url):
        raise AssertionError("the network was used")

    results = fetch_manifest(manifest, cache, download=no_network, offline=True)
    assert [r.action for r in results] == ["cached", "cached"]


def test_offline_refuses_a_missing_file(tmp_path, manifest):
    with pytest.raises(PinnedSourceError, match="missing from the cache, and downloads are off"):
        fetch_manifest(manifest, tmp_path / "cache", offline=True)


def test_a_changed_download_is_refused_and_not_cached(tmp_path, manifest, server):
    server.files[BASE + "HDRIs/sky.jpg"] = SKY + b"!"
    cache = tmp_path / "cache"
    with pytest.raises(PinnedSourceError, match="sky.jpg: the download from .* has SHA-256"):
        fetch_manifest(manifest, cache, base_url=BASE, download=server)
    assert sorted(path.name for path in cache.iterdir()) == ["stone.jpg"]


def test_a_bad_cached_file_is_refused_and_left_alone(tmp_path, manifest, server):
    cache = tmp_path / "cache"
    cache.mkdir()
    (cache / "stone.jpg").write_bytes(b"edited")
    with pytest.raises(PinnedSourceError, match="the cached file does not match"):
        fetch_manifest(manifest, cache, base_url=BASE, download=server)
    assert (cache / "stone.jpg").read_bytes() == b"edited"
    assert server.requests == []


def test_a_failed_download_leaves_no_partial_file(tmp_path, manifest):
    cache = tmp_path / "cache"
    with pytest.raises(DownloadError, match="404"):
        fetch_manifest(manifest, cache, base_url=BASE, download=Server({}))
    assert list(cache.iterdir()) == []


def test_report_is_called_per_result(tmp_path, manifest, server):
    seen = []
    fetch_sources(read_manifest(manifest), tmp_path / "c", base_url=BASE, download=server, report=seen.append)
    assert [r.source.name for r in seen] == ["stone.jpg", "sky.jpg"]


def test_url_download_reads_file_urls(tmp_path):
    source = tmp_path / "served" / "stone.jpg"
    source.parent.mkdir()
    source.write_bytes(STONE)
    result = fetch_source(PinnedSource(sha(STONE), "stone.jpg", source.as_uri()), tmp_path / "cache")
    assert result.action == "downloaded"
    assert (tmp_path / "cache" / "stone.jpg").read_bytes() == STONE
    with pytest.raises(DownloadError):
        url_download()((tmp_path / "missing.jpg").as_uri())


def test_a_directory_in_place_of_a_file_is_refused(tmp_path):
    (tmp_path / "cache" / "stone.jpg").mkdir(parents=True)
    with pytest.raises(PinnedSourceError, match="expected a file"):
        fetch_source(PinnedSource(sha(STONE), "stone.jpg", "x"), tmp_path / "cache", offline=True)


def file_manifest(tmp_path):
    served = tmp_path / "served"
    served.mkdir()
    (served / "stone.jpg").write_bytes(STONE)
    path = tmp_path / "sources.txt"
    path.write_text(f"{sha(STONE)} stone.jpg stone.jpg\n", encoding="utf-8")
    return path, served.as_uri() + "/"


def test_command_line_fetches_reports_and_exits(tmp_path, capsys):
    path, base = file_manifest(tmp_path)
    cache = tmp_path / "cache"
    assert main([str(path), str(cache), "--base-url", base]) == 0
    assert capsys.readouterr().out == f"downloaded stone.jpg ({len(STONE)} bytes) from {base}stone.jpg\n"
    assert main([str(path), str(cache), "--offline"]) == 0
    assert capsys.readouterr().out == f"cached stone.jpg ({len(STONE)} bytes)\n"
    assert main([str(path), str(cache), "--offline", "--quiet"]) == 0
    assert capsys.readouterr().out == ""


def test_command_line_exit_codes(tmp_path, capsys):
    path, base = file_manifest(tmp_path)
    assert main([str(path), str(tmp_path / "a"), "--offline"]) == 1
    assert "downloads are off" in capsys.readouterr().err
    assert main([str(path), str(tmp_path / "b"), "--base-url", base + "missing/"]) == 2
    assert capsys.readouterr().err.startswith("uppercut-pinned-sources: ")
    assert main([str(tmp_path / "none.txt"), str(tmp_path / "c")]) == 2
    assert "none.txt" in capsys.readouterr().err


def test_module_entry_point(tmp_path):
    path, base = file_manifest(tmp_path)
    run = subprocess.run(
        [sys.executable, "-m", "uppercut_pinned_sources", str(path), str(tmp_path / "cache"), "--base-url", base],
        capture_output=True,
        text=True,
    )
    assert run.returncode == 0, run.stderr
    assert run.stdout.startswith("downloaded stone.jpg")
