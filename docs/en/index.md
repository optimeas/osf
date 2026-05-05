---
title: OSF Format
description: Introduction to the Open Streaming Format
#hide_title: true/false
#hide_table_of_contents: true/false
#sidebar_label: Label shown in the sidebar
#sidebar_position: Position relative to its parent if needed
image: "/img/om_social_card.png"
keywords:
  - File format
  - OSF4
  - OSF5
  - OSF
last_update:
  date: 2026-05-04
  author: Optimeas GmbH
---

🇩🇪 [German version](../de/index.md)

## The OSF format — Introduction

OSF is designed as a **universal streaming format** for measurement and process data. It answers the central questions of modern data acquisition: How can a continuous stream of data be stored without loss? How does the format stay open and extensible without unnecessary complexity? And how do you bridge the requirements of embedded systems with the efficiency of powerful analysis platforms?

OSF is currently available in version 4, while version 5 is being implemented. Even though OSF4 and OSF5 differ in only a few points, we deliberately decided to bump the major version: above all the switch to a new header format (JSON as the default) and the simplified handling of the control byte in the data justify this new major version. At the same time OSF5 remains fully backward-compatible with OSF4 and can read and process its files — apart from a few details — without restrictions.

### What OSF delivers in practice

OSF is built for the reality of modern measurement systems: data streams that flow continuously, that need to be captured quickly and stored reliably. One thing is at the center of all this: gap-free recording without data loss, even when systems shut down abruptly or the power supply is interrupted.

But OSF does not stop at writing — it is **equally optimized for downstream processing.** Instead of storing individual values one after the other, OSF lays data down in blocks. This enables:

-   **efficient and fast storage** even at high data rates,
-   **fast loading and analysis** of large data sets,
-   consistent structures that perform well on both embedded systems and analysis platforms.

### Advantages at a glance:

-   **Loss-free, continuous data recording** on embedded devices.
-   **Robustness** against power loss or unexpected shutdown.
-   **Efficient block structure** for fast writing and reading.
-   **Flexible** for all time-related data: single values, vectors, matrices, images, and audio.
-   **Open and easy to implement** with a clear structure.
-   **Uniform** — one format for the edge, the lab, and the cloud.

OSF is therefore not only a storage format but a tool that **escorts data streams without loss and efficiently throughout their entire lifecycle** — from the very first measurement to analysis.


## An open solution for time-related data

The OSF answer to these requirements is a clear, open structure that applies equally to **OSF4** and **OSF5**:

-   **Magic header** for identification and fast processing.
-   **Metablock (XML or JSON)** describing channels, data types, and context information.
-   **Binary data blocks** for continuous, robust streaming.
-   **Block-oriented storage** for efficient writing and fast reloading of large data sets.

### Basic OSF file structure

The core architecture allows all time-related data to be stored in a single, consistent format. OSF is **openly documented**, easy to implement, and scales from edge devices to the cloud.

![OSF file structure.](media/dfa207f25f79c84ec79e2e2f6c2c42cc.jpg)

### What is different in OSF5

-   JSON as the default for the header (with backward compatibility to OSF4/XML).
-   Simplified use of the control byte in the data block for lower implementation effort.
-   Trailer dropped in favor of a cleaner file structure.
-   Flexible magic headers (`OSF4`, `OSF5`, `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`) with automatic detection of XML (`<`) or JSON (`{`).

The following describes what makes OSF as a format — **applicable equally to OSF4 and OSF5**. Details about specific features and about vector or matrix channels are available in dedicated documents.
