# Releasing `osfdata`

`osfdata` publishes to **production PyPI** (`pip install osfdata`) from
this repository's release workflow via Trusted Publishing (OIDC — no API
tokens). This document covers the one-time publisher setup plus the
recurring release process. Read it end-to-end before the first PyPI
release; subsequent releases are the "Releasing a new version" section
onwards.

## Trusted Publisher Setup (one-time, manual — already done)

Trusted Publishing connects this repository's release workflow to PyPI
via OIDC, eliminating the need for API tokens.

**This is configured and active** — it was registered on 2026-07-29 and
activated by the `osfdata 1.1.0` upload. The steps below are kept as a
record of *what* is configured (and for the next package that needs the
same setup); you do not need to repeat them for `osfdata` releases.

1. Sign in to <https://pypi.org> with the `optiMEAS` account.
2. Go to **Account settings → Publishing → Add a new pending publisher**,
   provider tab **GitHub**.
3. Fill in:
   - **PyPI Project Name:** `osfdata`
   - **Owner:** `optimeas`
   - **Repository name:** `osf`
   - **Workflow name:** `release.yml` — the bare filename, no path
   - **Environment name:** **leave empty** (see the warning below)
4. Click **Add**.

A publisher registered before the project exists is called *pending*; it
activates on the first successful upload, which is also what claims the
project name.

> **The environment field must stay empty.** The `publish-pypi` job in
> `release.yml` declares no `environment:` key, so its OIDC token carries
> the claim `environment: MISSING`. PyPI matches claims exactly, so any
> value entered in that field makes every upload fail. If you ever add a
> GitHub Environment gate to the publish job, the publisher entry has to
> be updated in the same change — the two are one setting in two places.

### Recovering from `invalid-publisher`

If the publish step fails with
`invalid-publisher: valid token, but no corresponding publisher`, the
OIDC claims did not match a registered publisher — typically because the
publisher does not exist yet, or because of the environment mismatch
above. The build jobs are unaffected and **nothing is uploaded**, so the
failure is free and repeatable.

Fix the publisher, then re-run the failed job. **Do not create a new
tag** — the artefacts are already built and attached to the existing run:

```bash
gh run rerun <run-id> --failed
```

> **Historical note.** Until the 1.1.0 release the workflow published to
> the separate test index instead; the `0.1.0` pre-releases there served
> their purpose during stabilization and are no longer referenced. The
> workflow now targets production PyPI directly and nothing in this repo
> depends on the test index any more.

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
5. Tag and push the tag once CI is green:
   ```bash
   git tag vX.Y.Z
   git push origin vX.Y.Z
   ```
   The tag trigger is repo-wide `v*`. Since the `osfdata` version is
   decoupled from the repo CHANGELOG line, use the **package** version as
   the tag (e.g. `v1.1.0` for `osfdata` 1.1.0). Pushing a `v*` tag whose
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
