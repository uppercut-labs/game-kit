# Releasing

Every package is versioned and released on its own. A release only goes out after the release gate passes on the source tree, the git history and the exact artifact that the release carries.

## The release gate

```bash
python tools/release_gate.py --artifact <the packed artifact>
```

- The gate needs `RELEASE_GATE_DENYLIST` (a private file of strings that must never ship, kept outside this repo) and `gitleaks` on `PATH` or in `GITLEAKS`.
- The gate also needs `RELEASE_GATE_ALLOWED_EMAILS`, a comma-separated list of the commit author and committer emails it accepts. Any other address in the history fails the gate. For this repository: `packages@uppercut-labs.com,127160473+devin-thomas@users.noreply.github.com,noreply@github.com` (the last is the committer GitHub records on merged pull requests).
- Exit code 0 is the only passing result.

## npm packages

1. Bump `version` in `packages/<name>/package.json`.
2. `npm install && npm run check`
3. `npm pack --workspace packages/<name> --pack-destination dist-pack`
4. Run the gate with `--artifact dist-pack/<tarball>.tgz`.
5. `npm publish dist-pack/<tarball>.tgz --access public`
6. Tag the commit `<name>@<version>` and push the tag.

## Python packages

1. Bump `version` in `packages/<name>/pyproject.toml`.
2. `uv sync --all-packages && uv run pytest`
3. `uv build --package <name> --out-dir dist/<name>`
4. Run the gate with `--artifact dist/<name>/<wheel>.whl --artifact dist/<name>/<sdist>.tar.gz`.
5. `uv publish dist/<name>/*` with the PyPI API token in `UV_PUBLISH_TOKEN`.
6. Tag the commit `<name>@<version>` and push the tag. A Python tag must not contain a second `@`.

## C packages

1. Bump the version in the package's header, its README (the install snippet's `GIT_TAG`) and `examples/consumer/CMakeLists.txt`. Commit.
2. Run the full C check (`./tools/check-c.sh` or `.\tools\check-c.ps1`) on every system the README lists.
3. Pack the source from the committed tree:

   ```bash
   mkdir -p dist
   git -c core.autocrlf=false archive --format=tar.gz --prefix=<name>-<version>/ -o dist/<name>-<version>.tar.gz HEAD:packages/<name>
   ```

   Extract it into a new empty folder and run its own `build-and-test` script there. A package that only builds inside this repository is not releasable.
4. Run the gate with `--artifact dist/<name>-<version>.tar.gz`.
5. Tag the commit `<name>@<version>`, push the tag, and optionally attach the tarball to a GitHub release.

## PowerShell modules

1. Bump `ModuleVersion` in the module's `.psd1`.
2. Run `Invoke-Pester` in the module's `tests` folder.
3. Pack the module folder into a zip from the committed tree and run the gate on it.
4. Tag the commit `<name>@<version>` and push the tag.

Releases are made from a maintainer machine. This repository does not use hosted CI.
