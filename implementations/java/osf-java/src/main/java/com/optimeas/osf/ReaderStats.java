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
    private long blocksSkippedZeroLength = 0;
    private long blocksSkippedStatusEvent = 0;
    private long blocksSkippedReservedType = 0;
    private long blocksSkippedDeprecatedType = 0;

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

    /**
     * Number of data blocks skipped because their length field read {@code 0}
     * — a non-conforming writer artefact (OSF-UP3). A conforming block always
     * carries at least its control byte.
     */
    public long blocksSkippedZeroLength() {
        return blocksSkippedZeroLength;
    }

    /** Increment the skipped-zero-length-block counter by one. */
    public void incBlocksSkippedZeroLength() {
        blocksSkippedZeroLength++;
    }

    /**
     * Number of {@code bcStatusEvent} blocks (control byte 3) skipped on read.
     * Counted separately from the generic deprecated-skip bucket (OSF-UP4,
     * DECISIONS §26): its payload is a fixed status word rather than a value
     * of the channel's declared datatype, so it can never become a sample —
     * this counter keeps an occurrence visible in the field.
     */
    public long blocksSkippedStatusEvent() {
        return blocksSkippedStatusEvent;
    }

    /** Increment the skipped-status-event-block counter by one. */
    public void incBlocksSkippedStatusEvent() {
        blocksSkippedStatusEvent++;
    }

    /**
     * Number of blocks skipped because the control byte identified a reserved
     * block type (0 = {@code bcReserved}, 2 = {@code bcTimebaseRealign}, or
     * any value ≥ 9 the spec does not currently define), or because a
     * {@code bcMessageEvent} block hit one of its two unspecified shapes: the
     * multi-sample bit set, or a channel {@code dataType} other than
     * {@code STRING}/{@code BINARY} (OSF-UP4, DECISIONS §26). §26 requires
     * this occurrence to be counted, not silently dropped — before this
     * counter existed, it was invisible to every Java consumer: {@code
     * ChannelAssembler} discards {@code Block.Skipped}, and {@code
     * com.optimeas.osf.internal} is not exported from the JPMS module, so
     * {@code ReaderStats} is the only surface a reserved-type skip can ever
     * reach.
     */
    public long blocksSkippedReservedType() {
        return blocksSkippedReservedType;
    }

    /** Increment the skipped-reserved-type-block counter by one. */
    public void incBlocksSkippedReservedType() {
        blocksSkippedReservedType++;
    }

    /**
     * Number of blocks skipped because the control byte identified a
     * deprecated block type that newer writers no longer emit but readers
     * must tolerate (1 = {@code bcTrustedTimestamp}). {@code bcStatusEvent}
     * (3) has its own counter ({@link #blocksSkippedStatusEvent()});
     * {@code bcMessageEvent} (4) is decoded rather than skipped in its
     * specified cases (OSF-UP4, DECISIONS §26). Added alongside {@link
     * #blocksSkippedReservedType()} for the same visibility reason and to
     * keep the skip-reason counter set coherent — a bare deprecated-skip
     * bucket without its two siblings would leave one third of the
     * previously-uncounted family still invisible.
     */
    public long blocksSkippedDeprecatedType() {
        return blocksSkippedDeprecatedType;
    }

    /** Increment the skipped-deprecated-type-block counter by one. */
    public void incBlocksSkippedDeprecatedType() {
        blocksSkippedDeprecatedType++;
    }
}
