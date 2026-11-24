# game-kit

Small, focused packages from Uppercut Labs for making, porting, testing and capturing games. Each package lives in its own folder under `packages/`, is versioned on its own, and has a README with its API and install line. The general-purpose kits are separate: [js-kit](https://github.com/uppercut-labs/js-kit), [py-kit](https://github.com/uppercut-labs/py-kit), [ps-kit](https://github.com/uppercut-labs/ps-kit) and [c-kit](https://github.com/uppercut-labs/c-kit).

## Packages

| Package | Language | What it does |
| --- | --- | --- |

## Installing

- **npm packages** are published as `@uppercut-labs/<name>`.
- **Python packages** are published on PyPI as `uppercut-<name>`.
- **C packages** have no package index. A release is a git tag `<name>@<version>`; take it with CMake `FetchContent` and `SOURCE_SUBDIR packages/<name>`, or vendor the folder and `add_subdirectory` it.
- **PowerShell modules** are imported from their folder or copied onto `$env:PSModulePath`.

## Working in this repo

| Language | Full check |
| --- | --- |
| TypeScript and JavaScript | `npm install && npm run check` |
| Python | `uv sync --all-packages && uv run pytest` |
| C | `./tools/check-c.sh` on macOS and Linux, `.\tools\check-c.ps1` on Windows |
| PowerShell | `Invoke-Pester` in each module's `tests` folder, with Pester 5 or later |

A package's tests must run without a display, a GPU, game data or hardware, so they run in a plain container. Fixtures are synthetic: no game ROMs, discs, dumps or copyrighted assets live in this repository.

See [RELEASING.md](RELEASING.md) for how a version is released.

## Licence

MIT
