// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

/**
 * Mutable running counters produced by the block-stream reader.
 *
 * <p>The fields are intentionally minimal for the J5 reader: a count of
 * fully-decoded blocks, a truncation flag, and the compression fields that
 * the J8 transparent-OSFZ read path fills in later (defaulting to "not
 * compressed" here so a plain reader leaves them untouched).
 *
 * <p>Mirrors the relevant subset of the Rust {@code ReaderStats} /
 * C++ {@code osf::ReaderStats}; the richer per-channel statistics those carry
 * are out of scope for the Java core's first reader pass.
 */
public final class ReaderStats {

    private int blocksRead = 0;
    private boolean truncationSeen = false;
    private boolean compressed = false;
    private String compressionFormat = "none";
    private IntegrityProfile integrity = IntegrityProfile.NONE;
    private long blocksCrcFailed = 0;
    private long blocksSignatureSkipped = 0;

    /** Number of blocks the reader fully decoded into a typed {@code Block}. */
    public int blocksRead() {
        return blocksRead;
    }

    /**
     * {@code true} once the reader hit a short/garbled trailing block and
     * stopped best-effort. Logically capped at one such event — no useful
     * block can follow a partial one.
     */
    public boolean truncationSeen() {
        return truncationSeen;
    }

    /** {@code true} if the source stream was gzip/zlib-compressed (set by J8). */
    public boolean compressed() {
        return compressed;
    }

    /**
     * Compression format label: {@code "none"} by default, otherwise the
     * format the decompressor detected (set by J8 via {@link #setCompression}).
     */
    public String compressionFormat() {
        return compressionFormat;
    }

    /** Increment the fully-decoded-block counter by one. */
    public void incBlocksRead() {
        blocksRead++;
    }

    /** Flag that the stream ended on a partial/garbled block. Idempotent. */
    public void markTruncated() {
        truncationSeen = true;
    }

    /**
     * Record that the stream was compressed with the given format. Sets
     * {@link #compressed()} to {@code true} and stores the format label.
     *
     * @param format the detected compression format, e.g. {@code "gzip"}
     */
    public void setCompression(String format) {
        this.compressed = true;
        this.compressionFormat = format;
    }

    /**
     * Integrity profile declared by the file's magic-header tokens
     * ({@link IntegrityProfile#NONE} for a plain file). Set from the header at
     * load time.
     */
    public IntegrityProfile integrity() {
        return integrity;
    }

    /**
     * Number of data blocks whose frame CRC32C did not verify (skipped on read).
     * Always {@code 0} unless the file declared level {@code crc}.
     */
    public long blocksCrcFailed() {
        return blocksCrcFailed;
    }

    /**
     * Number of signature blocks (reserved channel {@code 0xFFFE}) skipped on
     * read. Non-zero only for signed files read through this crc-level library.
     */
    public long blocksSignatureSkipped() {
        return blocksSignatureSkipped;
    }

    /**
     * Overall integrity verification status, per the spec 1.6 vocabulary:
     * <ul>
     *   <li>{@code "none"} — no integrity profile;</li>
     *   <li>{@code "crc_valid"} — level crc, every block CRC verified;</li>
     *   <li>{@code "invalid"} — level crc, at least one block failed its CRC;</li>
     *   <li>{@code "signature_unverifiable"} — a signed file whose signatures
     *       this crc-level reader cannot verify.</li>
     * </ul>
     */
    public String verificationStatus() {
        return switch (integrity) {
            case NONE -> "none";
            case ED25519 -> "signature_unverifiable";
            case CRC32C -> (blocksCrcFailed > 0) ? "invalid" : "crc_valid";
        };
    }

    /** Record the file's declared integrity profile (set from the header). */
    public void setIntegrity(IntegrityProfile profile) {
        this.integrity = profile;
    }

    /** Increment the failed-frame-CRC counter by one. */
    public void incBlocksCrcFailed() {
        blocksCrcFailed++;
    }

    /** Increment the skipped-signature-block counter by one. */
    public void incBlocksSignatureSkipped() {
        blocksSignatureSkipped++;
    }
}
