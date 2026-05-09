# Changelog

All notable changes to the C++ implementation of OSF will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.0.1] - 2026-05-08

### Added

- Initial CMake skeleton with `osf::osf` and `osf::headers` targets.
- Vendored `tl::expected` for `Result<T>` support.
- Error and Result types as foundation API.
- GoogleTest integration via `FetchContent`.
