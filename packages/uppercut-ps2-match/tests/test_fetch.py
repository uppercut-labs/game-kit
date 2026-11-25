import hashlib
import io
import tarfile
import zipfile

import pytest

from uppercut_ps2_match import fetch
from uppercut_ps2_match.errors import ConfigError, FetchError


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def make_tar(files, top="pkg", compress="gz", extra=None):
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode=f"w:{compress}") as t:
        for rel, data in files.items():
            info = tarfile.TarInfo(f"{top}/{rel}" if top else rel)
            info.size = len(data)
            t.addfile(info, io.BytesIO(data))
        if extra:
            extra(t)
    return buffer.getvalue()


FILES = {"bin/as": b"assembler bytes", "lib/cc1": b"compiler bytes"}


def package_config(archive, **overrides):
    spec = {"name": "compiler", "url": "https://example.invalid/compiler.tar.gz", "sha256": sha(archive),
            "files": {"lib/cc1": sha(FILES["lib/cc1"]), "bin/as": sha(FILES["bin/as"])}}
    spec.update(overrides)
    return {"packages": [spec]}


def test_fetch_installs_and_checks_every_pin(tmp_path):
    archive = make_tar(FILES)
    seen = []
    packages = fetch.load_packages(package_config(archive))
    installed = fetch.fetch_toolchain(packages, tmp_path / "tools", download=lambda url: seen.append(url) or archive, log=lambda _: None)
    assert installed == ["compiler"] and seen == ["https://example.invalid/compiler.tar.gz"]
    assert (tmp_path / "tools" / "compiler" / "lib" / "cc1").read_bytes() == FILES["lib/cc1"]
    assert not list((tmp_path / "tools").glob("tmp*"))  # nothing left over


def test_a_package_already_present_is_checked_and_left_alone(tmp_path):
    archive = make_tar(FILES)
    packages = fetch.load_packages(package_config(archive))
    fetch.fetch_toolchain(packages, tmp_path, download=lambda url: archive, log=lambda _: None)
    assert fetch.fetch_toolchain(packages, tmp_path, download=lambda url: pytest.fail("downloaded again"), log=lambda _: None) == []
    (tmp_path / "compiler" / "lib" / "cc1").write_bytes(b"tampered")
    with pytest.raises(FetchError, match="lib/cc1"):
        fetch.fetch_toolchain(packages, tmp_path, download=lambda url: archive, log=lambda _: None)


def test_wrong_archive_digest_stops_before_unpacking(tmp_path):
    archive = make_tar(FILES)
    packages = fetch.load_packages(package_config(archive, sha256="0" * 64))
    with pytest.raises(FetchError, match="archive SHA-256"):
        fetch.fetch_toolchain(packages, tmp_path, download=lambda url: archive, log=lambda _: None)
    assert not (tmp_path / "compiler").exists()


def test_wrong_file_digest_leaves_nothing_behind(tmp_path):
    archive = make_tar(FILES)
    packages = fetch.load_packages(package_config(archive, files={"lib/cc1": "1" * 64}))
    with pytest.raises(FetchError, match="expected"):
        fetch.fetch_toolchain(packages, tmp_path, download=lambda url: archive, log=lambda _: None)
    assert not (tmp_path / "compiler").exists()


def test_missing_file_in_the_archive(tmp_path):
    archive = make_tar({"bin/as": b"x"})
    packages = fetch.load_packages(package_config(archive, files={"lib/cc1": sha(b"y")}))
    with pytest.raises(FetchError, match="not found"):
        fetch.fetch_toolchain(packages, tmp_path, download=lambda url: archive, log=lambda _: None)


def test_symlinks_and_escaping_paths_are_refused(tmp_path):
    def add_link(t):
        info = tarfile.TarInfo("pkg/link")
        info.type = tarfile.SYMTYPE
        info.linkname = "/etc/passwd"
        t.addfile(info)

    for archive in (make_tar(FILES, extra=add_link), make_tar({"../escape": b"x"}, top=None)):
        packages = fetch.load_packages(package_config(archive))
        with pytest.raises(FetchError, match="unsafe"):
            fetch.fetch_toolchain(packages, tmp_path, download=lambda url, a=archive: a, log=lambda _: None)
    assert not (tmp_path.parent / "escape").exists()


def test_zip_archives_and_a_root_without_a_subfolder(tmp_path):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as z:
        for rel, data in FILES.items():
            z.writestr(rel, data)
    archive = buffer.getvalue()
    packages = fetch.load_packages(package_config(archive, url="https://example.invalid/compiler.zip"))
    assert fetch.fetch_toolchain(packages, tmp_path, download=lambda url: archive, log=lambda _: None) == ["compiler"]
    assert (tmp_path / "compiler" / "bin" / "as").is_file()


def test_zip_with_an_escaping_path_is_refused(tmp_path):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as z:
        z.writestr("../escape", b"x")
    archive = buffer.getvalue()
    packages = fetch.load_packages(package_config(archive, url="https://example.invalid/c.zip", files={}))
    with pytest.raises(FetchError, match="unsafe"):
        fetch.fetch_toolchain(packages, tmp_path, download=lambda url: archive, log=lambda _: None)


def test_dry_run_prints_the_plan_and_downloads_nothing(tmp_path):
    archive = make_tar(FILES)
    lines = []
    packages = fetch.load_packages(package_config(archive))
    assert fetch.fetch_toolchain(packages, tmp_path / "t", dry_run=True, download=lambda url: pytest.fail("downloaded"), log=lines.append) == []
    assert len(lines) == 1 and "compiler" in lines[0] and "fetch" in lines[0] and not (tmp_path / "t").exists()


def test_host_specific_builds():
    spec = {"packages": [{"name": "b", "hosts": {"linux-x86-64": {"url": "https://x.invalid/l.tgz", "sha256": "a" * 64},
                                                 "macos-arm64": {"url": "https://x.invalid/m.tgz", "sha256": "b" * 64}}}]}
    assert fetch.load_packages(spec, "macos-arm64")[0].url.endswith("m.tgz")
    assert fetch.load_packages(spec, "linux-x86-64")[0].sha256 == "a" * 64
    with pytest.raises(ConfigError, match="no build for host"):
        fetch.load_packages(spec, "windows-x86-64")


@pytest.mark.parametrize("bad", [
    {}, {"packages": []}, {"packages": [{"url": "u", "sha256": "a" * 64}]},
    {"packages": [{"name": "n", "sha256": "a" * 64}]}, {"packages": [{"name": "n", "url": "u"}]},
    {"packages": [{"name": "n", "url": "u", "sha256": "xyz"}]},
])
def test_bad_toolchain_files(bad):
    with pytest.raises(ConfigError):
        fetch.load_packages(bad)


def test_load_packages_from_a_file(tmp_path):
    path = tmp_path / "toolchain.json"
    path.write_text('{"packages": [{"name": "n", "url": "https://x.invalid/n.tgz", "sha256": "' + "C" * 64 + '"}]}')
    (package,) = fetch.load_packages(path)
    assert package.sha256 == "c" * 64 and package.files == {}
