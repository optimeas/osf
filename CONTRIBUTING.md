# Contributing to OSF

Thank you for your interest in contributing to the Open Streaming Format project.

---

## General Guidelines

- All code, comments, commit messages, and documentation must be written in **English**.
- Keep contributions focused: one language implementation or one integration per pull request.
- Follow the existing structure and naming conventions of the repository.
- Open an issue before starting significant work so the approach can be discussed.

---

## Adding a New Language Implementation

1. Create a subdirectory under `implementations/<language>/`.
2. Add a `README.md` following the template used by existing implementations (status badge, target platform, reader/writer support, dependencies).
3. Implement at minimum a basic reader **or** writer. A partial implementation is welcome as long as it is clearly documented.
4. Add test data or link to the shared `examples/` folder.
5. Update `implementations/README.md` to reflect the new status.

### Checklist for a new implementation PR

- [ ] `implementations/<language>/README.md` is present and complete
- [ ] All source files have the MIT license header
- [ ] Code and comments are in English
- [ ] At least one example demonstrating read or write works against the files in `examples/`
- [ ] `implementations/README.md` status table is updated

---

## Adding a New Integration

1. Create a subdirectory under `integrations/<name>/`.
2. Add a `README.md` explaining the target ecosystem, why the integration is valuable, prerequisites (which OSF implementation it depends on), and status.
3. Implement the bridge code and provide at least one usage example.
4. Update `integrations/README.md`.

---

## Reporting Issues

- Use the GitHub issue tracker.
- For format specification questions, open an issue with the label `specification`.
- For implementation-specific bugs, open an issue with the label matching the language (e.g., `delphi`, `python`).
- Include a minimal reproducible example where possible.

---

## Code Style

Each language implementation should follow the idiomatic style of that language. There is no cross-language style requirement beyond English identifiers and comments.

---

## License

By contributing **source code** to this repository you agree that your
contributions will be licensed under the MIT License.

By contributing **documentation** under `docs/` you agree that your
contributions will be licensed under Creative Commons Attribution 4.0
International (CC BY 4.0), and that they are attributed to "optiMEAS GmbH und
optiMEAS Switzerland GmbH".
