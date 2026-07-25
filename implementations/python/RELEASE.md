# Releasing `osfdata`

`osfdata` publishes to **production PyPI** (`pip install osfdata`) from
this repository's release workflow via Trusted Publishing (OIDC — no API
tokens). This document covers the one-time publisher setup plus the
recurring release process. Read it end-to-end before the first PyPI
release; subsequent releases are the "Releasing a new version" section
onwards.

## Trusted Publisher Setup (one-time, manual)

Trusted Publishing connects this repository's release workflow to PyPI
via OIDC, eliminating the need for API tokens. It **must be configured
once, before the first upload** — the first tag push will otherwise fail
at the publish step with an OIDC error.

### PyPI (production)

1. Sign in to <https://pypi.org> with the `optiMEAS` account.
2. Go to **Account settings → Publishing → Add a new pending publisher**.
3. Fill in:
   - **PyPI Project Name:** `osfdata`
   - **Owner:** `optimeas`
   - **Repository name:** `osf`
   - **Workflow name:** `release.yml`
   - **Environment name:** leave empty (or `pypi` if you later add a
     GitHub Environment gate to the publish job).
4. Click **Add**.

The publisher is registered as "pending" and activates on the first
successful upload from the configured workflow — which claims the
`osfdata` project name on production PyPI.

> **TestPyPI note.** The workflow no longer publishes to TestPyPI; it
> targets production PyPI directly. The earlier TestPyPI pre-releases
> (`0.1.0`) served their purpose during stabilization. If you ever want a
> dry run again, temporarily add `repository-url:
> https://test.pypi.org/legacy/` back to the publish step and register a
> matching pending publisher on <https://test.pypi.org>.

## Releasing a new version

1. Confirm CI is green on `main`.
2. Bump the version in **both** files synchronously (the version strings
   must agree or maturin emits a warning):
   - `implementations/python/Cargo.toml` → `[package].version`
   - `implementations/python/pyproject.toml` → `[project].version`
   - Keep `Cargo.lock`'s `osf-python` entry in step (a local
     `maturin build` / `cargo build` refreshes it).
3. Move the `[Unreleased]` section of `CHANGELOG.md` into a dated
   `[X.Y.Z]` heading and update the compare links at the bottom.
4. Commit and push to `main`:
   ```bash
   git add implementations/python/Cargo.toml \
           implementations/python/pyproject.toml \
           implementations/python/Cargo.lock \
           implementations/python/CHANGELOG.md
   git commit -m "release: osfdata X.Y.Z"
   git push origin main
   ```
5. Tag and push the tag — **only after** the PyPI pending publisher
   exists (first release) and CI is green:
   ```bash
   git tag vX.Y.Z
   git push origin vX.Y.Z
   ```
   The tag trigger is repo-wide `v*`. Since the `osfdata` version is
   decoupled from the repo CHANGELOG line, use the **package** version as
   the tag (e.g. `v1.0.0` for `osfdata` 1.0.0). Pushing a `v*` tag whose
   `osfdata` version already exists on PyPI is harmless — PyPI rejects the
   duplicate and the job fails without side effects.
6. The release workflow runs automatically: builds wheels for the four
   `(os, target)` pairs (Linux x86_64 + aarch64, macOS arm64, Windows
   x64), builds the sdist, and publishes to production PyPI.

## Verifying a release

In a fresh virtual environment:

```bash
python -m venv .verify
.verify/Scripts/activate           # Windows
# source .verify/bin/activate      # Linux / macOS

pip install osfdata

python -c "import osf; print(osf.__version__)"
python -c "import osf; mgr = osf.load('examples/steam_loco.osf'); print(mgr)"
```

No `--extra-index-url` is needed on production PyPI: `numpy` and every
other runtime dependency resolve from the same index.

## Yanking a bad release

PyPI supports "yank" — the version stays downloadable for users who pin
it explicitly but is no longer selected for new installs.

```
# On the osfdata project page on pypi.org:
# Manage → Releases → Options → Yank
```

A PyPI **version can never be re-uploaded or overwritten**, even after a
delete. Bumping the version and re-releasing is always the right fix;
yank is a last-resort signal, not a redo.
