// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

/**
 * Shared multi-sample block-chunking arithmetic used by both writers.
 *
 * <p>{@link com.optimeas.osf.BlockWriter} (accumulating) and
 * {@link com.optimeas.osf.StreamingWriter} (power-loss-safe) pack
 * timestamped-numeric and equidistant runs into multi-sample blocks, splitting
 * at the largest count that still fits the channel's length field. Both writers
 * call <em>these</em> functions so they chunk byte-for-byte identically — which
 * is what makes the §7 "on-disk-identical OSF5" guarantee real (see
 * {@code WriterIdentityTest}).
 *
 * <p>Faithful port of the reference sizing functions
 * {@code implementations/cpp/src/writer_common.cpp}
 * ({@code max_samples_per_timestamped_block} / {@code _start_block} /
 * {@code _continued_block}) and the inline chunking in
 * {@code implementations/cpp/src/block_writer.cpp}.
 */
public final class BlockChunking {

    /** Largest payload (control byte + body) that fits a 2-byte length field. */
    public static final int MAX_PAYLOAD_U16 = 0xFFFF;

    /**
     * Soft cap for the 4-byte length field — a single ~2&nbsp;GB block is already
     * enormous; pinning it just below {@code Integer.MAX_VALUE} avoids overflow on
     * the body-length conversion. Mirrors the Rust {@code MAX_BLOCK_PAYLOAD_U32}.
     */
    public static final int MAX_PAYLOAD_U32 = Integer.MAX_VALUE - 1024;

    /** GPS wire size per sample: 3 little-endian f64 (lat/lon/alt). */
    public static final int GPS_VALUE_SIZE = 24;

    // bcAbsTimeStampData per-block overhead: control byte + u32 sample count.
    private static final int TIMESTAMPED_OVERHEAD = 1 + 4;
    // bcStartData per-block overhead: control + i64 start ts + f64 rate + u32 N.
    private static final int START_OVERHEAD = 1 + 8 + 8 + 4;
    // bcContinuedData per-block overhead: control + u32 N.
    private static final int CONTINUED_OVERHEAD = 1 + 4;

    private BlockChunking() {}

    /**
     * Bytes reserved at the end of every block for the frame CRC32C when the
     * integrity profile is active — the CRC is counted in the length field, so
     * it eats into each block's payload budget.
     */
    public static final int FRAME_CRC_RESERVE = 4;

    /** Largest block payload (control + body) for the given length-field width. */
    public static int maxPayload(int sizeOfLengthValue) {
        return (sizeOfLengthValue == 2) ? MAX_PAYLOAD_U16 : MAX_PAYLOAD_U32;
    }

    /** Payload budget less the frame-CRC reserve when {@code frameCrc} is set. */
    private static int budget(int sizeOfLengthValue, boolean frameCrc) {
        int max = maxPayload(sizeOfLengthValue);
        return frameCrc ? Math.max(0, max - FRAME_CRC_RESERVE) : max;
    }

    /**
     * Max samples per {@code bcAbsTimeStampData} block: each sample is an i64
     * timestamp plus one {@code valueSize}-byte value.
     */
    public static int maxSamplesPerTimestamped(int valueSize, int sizeOfLengthValue) {
        return maxSamplesPerTimestamped(valueSize, sizeOfLengthValue, false);
    }

    /** Frame-CRC-aware variant: reserves 4 bytes per block when {@code frameCrc}. */
    public static int maxSamplesPerTimestamped(int valueSize, int sizeOfLengthValue,
                                               boolean frameCrc) {
        int perSample = 8 + valueSize;
        return Math.max(1, (budget(sizeOfLengthValue, frameCrc) - TIMESTAMPED_OVERHEAD) / perSample);
    }

    /** Max samples in the opening {@code bcStartData} block of an equidistant segment. */
    public static int maxSamplesPerStart(int valueSize, int sizeOfLengthValue) {
        return maxSamplesPerStart(valueSize, sizeOfLengthValue, false);
    }

    /** Frame-CRC-aware variant: reserves 4 bytes per block when {@code frameCrc}. */
    public static int maxSamplesPerStart(int valueSize, int sizeOfLengthValue, boolean frameCrc) {
        return Math.max(1, (budget(sizeOfLengthValue, frameCrc) - START_OVERHEAD) / valueSize);
    }

    /** Max samples in a {@code bcContinuedData} continuation block. */
    public static int maxSamplesPerContinued(int valueSize, int sizeOfLengthValue) {
        return maxSamplesPerContinued(valueSize, sizeOfLengthValue, false);
    }

    /** Frame-CRC-aware variant: reserves 4 bytes per block when {@code frameCrc}. */
    public static int maxSamplesPerContinued(int valueSize, int sizeOfLengthValue,
                                             boolean frameCrc) {
        return Math.max(1, (budget(sizeOfLengthValue, frameCrc) - CONTINUED_OVERHEAD) / valueSize);
    }

    /** Max GPS samples per {@code bcAbsTimeStampData} block. */
    public static int maxSamplesPerTimestampedGps(int sizeOfLengthValue) {
        return maxSamplesPerTimestamped(GPS_VALUE_SIZE, sizeOfLengthValue);
    }

    /** Frame-CRC-aware variant: reserves 4 bytes per block when {@code frameCrc}. */
    public static int maxSamplesPerTimestampedGps(int sizeOfLengthValue, boolean frameCrc) {
        return maxSamplesPerTimestamped(GPS_VALUE_SIZE, sizeOfLengthValue, frameCrc);
    }
}
