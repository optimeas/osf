---
title: Other languages (planned)
description: Planned OSF implementations — C, C#, MATLAB, JavaScript/TypeScript and Swift
sidebar_position: 7
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C
  - C#
  - MATLAB
  - JavaScript
  - Swift
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
---

🇩🇪 [German version](../../de/implementations/planned.md)

# Other languages (planned)

Besides the available implementations (Delphi, Rust, Python, C++) and the
[planned Java implementation](java.md), further languages are envisaged. So
far they only have placeholders in the repository, but no code yet. Because
OSF is an openly documented, deliberately simple format, each of these
bindings can be implemented standalone.

| Language | Planned focus |
|---|---|
| **C** | Lean, portable binding for embedded and systems use. In the short term the C ABI [`osf-c`](cpp.md) from the C++ implementation is already available. |
| **C#** | .NET ecosystem; desktop and backend applications. A binding via the C ABI, among others, is conceivable. |
| **MATLAB** | Import OSF data into MATLAB/Simulink workflows for engineering analysis. |
| **JavaScript / TypeScript** | Browser and Node.js environments; visualization and web tooling. |
| **Swift** | Apple platforms (macOS/iOS). |

The repository on [GitHub](https://github.com/optimeas/osf) carries the
current status — and whether work has already begun. If you are interested in
a particular language, the [specification](../osf_general.md) and the
existing implementations are worth a look as a template.
