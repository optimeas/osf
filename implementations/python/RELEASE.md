# Releasing `osfdata`

This document covers the one-time Trusted Publisher setup plus the
recurring release process. Read it end-to-end before the first
TestPyPI release; subsequent releases are step 3 onwards.

## Trusted Publisher Setup (one-time, manual)

Trusted Publishing connects this repository's release workflow to
TestPyPI / PyPI via OIDC, eliminating the need for API tokens. This
must be configured once per index, before the first release.

### TestPyPI

1. Sign in to <https://test.pypi.org> with the `optiMEAS` account.
2. Go to **Account Settings → Publishing → Add a new pending
   publisher**.
3. Fill in:
   - **PyPI Project Name:** `osfdata`
   - **Owner:** `optimeas`
   - **Repository name:** `osf`
   - **Workflow name:** `release.yml`
   - **Environment name:** leave empty (or use `testpypi` if added
     to the workflow as an environment gate later)
4. Click **Add**.

The publisher is now registered as "pending" — it activates on the
first successful upload from the configured workflow.

### PyPI (production, later)

Same procedure on <https://pypi.org> once `osfdata` is stable enough
for production release. PyPI and TestPyPI accounts are separate;
this step must be repeated on production PyPI when we're ready.

## Activating the publish step

`release.yml` ships with the `publish-testpypi` job guarded by
`if: false` so the workflow can land safely while CI proves stable.
Once both conditions below are met, activate it.

**Preconditions:**

1. `ci.yml` has run green on at least 2–3 successive `main` pushes.
2. The TestPyPI Pending Publisher has been configured (above).

**Activation:**

In `.github/workflows/release.yml`, find the publish job and replace:

```yaml
    if: false
```

with:

```yaml
    if: startsWith(github.ref, 'refs/tags/v')
```

Commit, push. The next `v*` tag push will trigger an actual upload.

## Releasing a new version

1. Confirm CI is green on `main`.
2. Bump version in **both** files synchronously (the version string
   must agree or maturin emits a warning):
   - `implementations/python/Cargo.toml` → `[package].version`
   - `implementations/python/pyproject.toml` → `[project].version`
3. Update `CHANGELOG.md` if you keep one (none yet for this crate).
4. Commit and push to `main`:
   ```bash
   git add implementations/python/Cargo.toml implementations/python/pyproject.toml
   git commit -m "release: osfdata 0.x.y"
   git push origin main
   ```
5. Tag and push the tag:
   ```bash
   git tag v0.x.y
   git push origin v0.x.y
   ```
6. The release workflow runs automatically: builds wheels for all
   five (os, target) pairs, builds the sdist, publishes to
   TestPyPI.

## Verifying a TestPyPI release

In a fresh virtual environment:

```bash
python -m venv .verify
.verify/Scripts/activate           # Windows
# source .verify/bin/activate      # Linux / macOS

pip install --index-url https://test.pypi.org/simple/ \
            --extra-index-url https://pypi.org/simple/ \
            osfdata

python -c "import osf; print(osf.__version__)"
python -c "import osf; mgr = osf.load('examples/steam_loco.osf'); print(mgr)"
```

`--extra-index-url https://pypi.org/simple/` is required because
TestPyPI does not host all of `osfdata`'s runtime dependencies
(e.g. `numpy`); the extra index lets pip pull those from
production PyPI.

## Yanking a bad release

TestPyPI / PyPI both support "yank" — the version remains
downloadable for users who explicitly request it but is no longer
the default for new installs.

```bash
# On the project page on test.pypi.org or pypi.org:
# Manage → Releases → Options → Yank
```

Bumping the version and re-releasing is preferable; yank is a
last-resort tool.
