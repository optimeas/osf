// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

import com.optimeas.osf.ChannelDef;
import com.optimeas.osf.ChannelType;
import com.optimeas.osf.DataType;
import com.optimeas.osf.GpsLocation;
import com.optimeas.osf.IntegrityProfile;
import com.optimeas.osf.OsfException;
import com.optimeas.osf.OsfVersion;
import com.optimeas.osf.ReaderStats;

import java.nio.BufferUnderflowException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.zip.CRC32C;

/**
 * Best-effort decoder for the OSF binary block stream that follows the
 * metablock.
 *
 * <p>This is a faithful port of the Rust reference
 * {@code implementations/rust/osf-core/src/reader.rs} and the C++ reference
 * {@code implementations/cpp/src/reader.cpp}; control-byte values, the bit-7
 * multi-sample rule, {@code sizeoflengthvalue} handling, and the
 * version-deterministic null-terminator rule are taken verbatim from those and
 * from {@code docs/en/osf_general.md}.
 *
 * <h2>Per-block framing (all little-endian)</h2>
 * <pre>
 *   [u16 channelIndex][length field (sizeOfLengthValue bytes)][u8 control][body…]
 * </pre>
 * {@code length} is the byte count of {@code control + body}. The length-field
 * width is the channel's {@code sizeOfLengthValue} (2 or 4 bytes).
 *
 * <h2>Control byte</h2>
 * Bit 7 = multi-sample flag; bits 0–6 = block type:
 * <table>
 *   <tr><th>value</th><th>meaning</th><th>handling</th></tr>
 *   <tr><td>0</td><td>bcReserved</td><td>skip (reserved)</td></tr>
 *   <tr><td>1</td><td>bcTrustedTimestamp</td><td>skip (deprecated)</td></tr>
 *   <tr><td>2</td><td>bcTimebaseRealign</td><td>skip (reserved)</td></tr>
 *   <tr><td>3</td><td>bcStatusEvent</td><td>skip (deprecated)</td></tr>
 *   <tr><td>4</td><td>bcMessageEvent</td><td>skip (deprecated)</td></tr>
 *   <tr><td>5</td><td>bcContinuedData</td><td>decode (numeric)</td></tr>
 *   <tr><td>6</td><td>bcStartData</td><td>decode (numeric + ts + rate)</td></tr>
 *   <tr><td>7</td><td>bcContinuedRelStampData</td><td>decode (OSF4-era)</td></tr>
 *   <tr><td>8</td><td>bcAbsTimeStampData</td><td>decode (per-sample ts)</td></tr>
 *   <tr><td>≥9</td><td>unknown</td><td>skip (reserved)</td></tr>
 * </table>
 *
 * <h2>Sample-count rule</h2>
 * When bit 7 is clear, exactly one sample follows (implicit N=1, no prefix).
 * When bit 7 is set, the payload begins with a {@code uint32} sample count N.
 * For {@code string}/{@code binary} {@code bcAbsTimeStampData} both forms are
 * accepted on input (writers emit the bit-7-clear compact form).
 *
 * <h2>Best-effort truncation</h2>
 * A short/garbled trailing block stops the read: the reader returns everything
 * decoded so far and sets {@link ReaderStats#markTruncated()}. It never throws
 * on truncation — short reads are caught via {@link BufferUnderflowException}
 * and bounds checks.
 */
public final class BlockReader {

    /** Special channel index introducing the optional OSF4 trailer block. */
    private static final int TRAILER_CHANNEL_INDEX = 0xFFFF;
    /** Reserved file-wide channel carrying integrity signature blocks. */
    private static final int SIGNATURE_CHANNEL_INDEX = 0xFFFE;
    /** Length of the OSF4 magic trailer string, padded to 40 bytes. */
    private static final int MAGIC_TRAILER_LEN = 40;

    private static final System.Logger LOG = System.getLogger(BlockReader.class.getName());

    private BlockReader() {}

    /**
     * Decode the whole block stream best-effort.
     *
     * @param afterMetablock raw bytes positioned at the first byte after the
     *                       metablock (already decompressed for OSFZ inputs)
     * @param version        OSF version — drives the null-terminator rule
     * @param channels       channel definitions keyed by channel index
     * @param stats          mutable counters; {@code incBlocksRead} is bumped
     *                       per decoded block and {@code markTruncated} on a
     *                       partial trailing block
     * @return the blocks decoded before EOF / truncation, in stream order
     */
    public static List<Block> readAll(byte[] afterMetablock, OsfVersion version,
                                      Map<Integer, ChannelDef> channels,
                                      ReaderStats stats) {
        return readAll(afterMetablock, version, channels, stats, IntegrityProfile.NONE);
    }

    /**
     * Decode the whole block stream best-effort under an integrity profile.
     *
     * <p>When {@code integrity} is {@link IntegrityProfile#NONE} this behaves
     * exactly like {@link #readAll(byte[], OsfVersion, Map, ReaderStats)}. Under
     * an active profile:
     * <ul>
     *   <li>the last four bytes of every block's data area are a CRC32C over the
     *       whole frame (channel index, length field, control byte, payload);
     *       they are verified and stripped <em>before</em> the typed parse
     *       (fail-closed framing, effective payload = {@code LEN − 4}). A
     *       mismatch skips the block and bumps
     *       {@link ReaderStats#blocksCrcFailed()} (best-effort, read continues);
     *   </li>
     *   <li>signature blocks on the reserved channel {@code 0xFFFE} (control
     *       byte 9, u32 length) are skipped and counted
     *       ({@link ReaderStats#blocksSignatureSkipped()}) so a signed file
     *       stays readable.</li>
     * </ul>
     *
     * @param integrity the file's declared integrity profile
     */
    public static List<Block> readAll(byte[] afterMetablock, OsfVersion version,
                                      Map<Integer, ChannelDef> channels,
                                      ReaderStats stats, IntegrityProfile integrity) {
        List<Block> out = new ArrayList<>();
        ByteBuffer buf = LittleEndian.wrap(afterMetablock);
        boolean integrityActive = integrity != IntegrityProfile.NONE;

        while (buf.hasRemaining()) {
            int blockStart = buf.position();
            try {
                // Step 1: 2-byte channel index. A clean stop here (fewer than
                // 2 bytes left) is the regular end of the data section — NOT a
                // truncation. We detect that explicitly before reading.
                if (buf.remaining() < 2) {
                    // Leftover stray byte(s): treat as clean EOF (mirrors the
                    // reference's "unexpected EOF on channel index = silent
                    // end"). Only a *partial mid-block* read flags truncation.
                    break;
                }
                int channelIndex = Short.toUnsignedInt(buf.getShort());

                // Step 2: optional 0xFFFF trailer block. Consume and stop.
                if (channelIndex == TRAILER_CHANNEL_INDEX) {
                    consumeTrailer(buf, stats);
                    break;
                }

                // Step 2b: integrity signature block. Channel 0xFFFE is the
                // reserved file-wide integrity channel (not declared in the
                // metablock); it always uses a u32 length field. This crate
                // reads level crc but does not verify signatures, so the block
                // is skipped and counted — a signed file stays readable.
                if (integrityActive && channelIndex == SIGNATURE_CHANNEL_INDEX) {
                    if (buf.remaining() < 4) {
                        stats.markTruncated();
                        break;
                    }
                    long sigLen = Integer.toUnsignedLong(buf.getInt());
                    if (buf.remaining() < sigLen) {
                        stats.markTruncated();
                        break;
                    }
                    buf.position(buf.position() + (int) sigLen);
                    stats.incBlocksSignatureSkipped();
                    out.add(new Block.Skipped(channelIndex,
                            Block.SkipReason.SIGNATURE_BLOCK, sigLen));
                    continue;
                }

                // Step 3: channel lookup. Unknown index = corruption; we cannot
                // know the length-field width. Stop best-effort (flag like the
                // reference treats a hard error: end of useful decoding).
                ChannelDef def = channels.get(channelIndex);
                if (def == null) {
                    stats.markTruncated();
                    break;
                }

                int sizeofLen = def.sizeOfLengthValue();
                if (sizeofLen != 2 && sizeofLen != 4) {
                    // Validated in the metablock parser; defensive guard.
                    throw new OsfException.MalformedFile(
                            "channel " + channelIndex + " sizeoflengthvalue="
                            + sizeofLen + " reached the block reader; must be 2 or 4");
                }

                // Step 4: per-channel length prefix. A short read here is a
                // truncated trailing block.
                if (buf.remaining() < sizeofLen) {
                    stats.markTruncated();
                    break;
                }
                long length = (sizeofLen == 2)
                        ? Short.toUnsignedInt(buf.getShort())
                        : Integer.toUnsignedLong(buf.getInt());

                // Zero-length block: skip (reserved). bytesSkipped = 0.
                if (length == 0) {
                    out.add(new Block.Skipped(channelIndex,
                            Block.SkipReason.RESERVED_BLOCK_TYPE, 0));
                    continue;
                }

                int lengthI = (int) length;

                // Step 5: forward-compat skip — Unsupported channel. Consume
                // the payload bytes by length without parsing.
                Block.SkipReason chanSkip = unsupportedReason(def);
                if (chanSkip != null) {
                    if (buf.remaining() < lengthI) {
                        stats.markTruncated();
                        break;
                    }
                    buf.position(buf.position() + lengthI);
                    out.add(new Block.Skipped(channelIndex, chanSkip, length));
                    continue;
                }

                // Step 6: pull the full payload (control byte + body).
                if (buf.remaining() < lengthI) {
                    stats.markTruncated();
                    break;
                }
                byte[] payload = new byte[lengthI];
                buf.get(payload);

                // Step 6b: frame CRC (integrity level crc). The last four bytes
                // of the data area are a CRC32C over the whole frame; verify and
                // strip them before the typed parse (fail-closed framing — a
                // residual "fully consumed" check after decoding is insufficient
                // for variable-length payloads). A mismatch skips the block.
                if (integrityActive) {
                    if (payload.length < 5 || !verifyFrameCrc(channelIndex, sizeofLen, length, payload)) {
                        stats.incBlocksCrcFailed();
                        out.add(new Block.Skipped(channelIndex, Block.SkipReason.CRC_FAILED, length));
                        continue;
                    }
                    payload = Arrays.copyOf(payload, payload.length - 4); // CRC verified — drop it
                }

                // Step 7: decode control byte and route.
                Block block = decodeBlock(channelIndex, def, version, payload, length, integrityActive);
                out.add(block);
                if (!(block instanceof Block.Skipped)) {
                    stats.incBlocksRead();
                }
            } catch (BufferUnderflowException | IndexOutOfBoundsException
                    | OsfException.MalformedFile | NegativeArraySizeException
                    | IllegalArgumentException ex) {
                // A short/garbled block body whose decode would otherwise escape
                // readAll: best-effort stop. Covers buffer underruns, out-of-bounds
                // accesses, non-numeric dataType reaching a numeric decoder
                // (OsfException.MalformedFile from newValueArray), u32 sample-count
                // overflow (NegativeArraySizeException / MalformedFile from
                // readSampleCount), and any illegal argument from array allocation.
                stats.markTruncated();
                buf.position(blockStart); // leave the buffer at the bad block
                break;
            }
        }
        return out;
    }

    /** Map an unsupported channel to its skip reason, or {@code null}. */
    private static Block.SkipReason unsupportedReason(ChannelDef def) {
        if (def.dataType() == DataType.UNSUPPORTED) {
            return Block.SkipReason.UNSUPPORTED_DATA_TYPE;
        }
        if (def.channelType() == ChannelType.UNSUPPORTED) {
            return Block.SkipReason.UNSUPPORTED_CHANNEL_TYPE;
        }
        return null;
    }

    /**
     * Consume the optional {@code 0xFFFF} info-data trailer block plus the
     * optional 40-byte magic trailer. Best-effort: a short read just stops.
     * The length here is always {@code u32}, NOT the per-channel
     * {@code sizeoflengthvalue}.
     */
    private static void consumeTrailer(ByteBuffer buf, ReaderStats stats) {
        if (buf.remaining() < 4) {
            stats.markTruncated();
            return;
        }
        long length = Integer.toUnsignedLong(buf.getInt());
        if (buf.remaining() < length) {
            stats.markTruncated();
            return;
        }
        buf.position(buf.position() + (int) length);
        // Best-effort magic trailer — present in OSF4 files, absent otherwise.
        if (buf.remaining() >= MAGIC_TRAILER_LEN) {
            buf.position(buf.position() + MAGIC_TRAILER_LEN);
        }
    }

    /**
     * Verify the trailing 4-byte frame CRC over the whole frame: the 2-byte
     * channel index, the length field (as it appears on disk, LE, {@code sizeofLen}
     * bytes) and the payload up to (but excluding) the CRC. {@code payload} still
     * includes the 4 CRC bytes; {@code length} is the on-disk length field value.
     */
    private static boolean verifyFrameCrc(int channelIndex, int sizeofLen, long length,
                                          byte[] payload) {
        int split = payload.length - 4;
        long stored = (payload[split] & 0xFFL)
                | ((payload[split + 1] & 0xFFL) << 8)
                | ((payload[split + 2] & 0xFFL) << 16)
                | ((payload[split + 3] & 0xFFL) << 24);
        CRC32C crc = new CRC32C();
        crc.update(channelIndex & 0xFF);
        crc.update((channelIndex >>> 8) & 0xFF);
        for (int i = 0; i < sizeofLen; i++) {
            crc.update((int) ((length >>> (8 * i)) & 0xFF));
        }
        crc.update(payload, 0, split);
        return crc.getValue() == stored;
    }

    // ---------------------------------------------------------------
    // Block decoding. `payload` is control byte + body; a short read inside
    // throws BufferUnderflowException, caught by readAll for truncation.
    // ---------------------------------------------------------------

    private static Block decodeBlock(int channelIndex, ChannelDef def,
                                     OsfVersion version, byte[] payload, long length,
                                     boolean integrityActive) {
        int control = payload[0] & 0xFF;
        boolean multi = (control & 0x80) != 0;
        int kind = control & 0x7F;

        ByteBuffer body = LittleEndian.wrap(payload);
        body.position(1); // skip control byte

        switch (kind) {
            case 0:  // bcReserved
            case 2:  // bcTimebaseRealign
                return new Block.Skipped(channelIndex,
                        Block.SkipReason.RESERVED_BLOCK_TYPE, length);
            case 1:  // bcTrustedTimestamp
            case 3:  // bcStatusEvent
            case 4:  // bcMessageEvent
                return new Block.Skipped(channelIndex,
                        Block.SkipReason.DEPRECATED_BLOCK_TYPE, length);
            case 5:  // bcContinuedData
                return checkNumericConsumed(
                        parseContinuedData(channelIndex, def.dataType(), multi, body),
                        channelIndex, body, integrityActive);
            case 6:  // bcStartData
                return checkNumericConsumed(
                        parseStartData(channelIndex, def.dataType(), multi, body),
                        channelIndex, body, integrityActive);
            case 7:  // bcContinuedRelStampData
                return checkNumericConsumed(
                        parseContinuedRelStampData(channelIndex, def.dataType(), multi, body),
                        channelIndex, body, integrityActive);
            case 8:  // bcAbsTimeStampData
                // Numeric / GPS AbsTs is fixed-width and must fully consume the
                // body; string/binary AbsTs is greedy by design, so it is not
                // subject to the full-consume diagnostic.
                DataType dt = def.dataType();
                Block absBlock = parseAbsTimestampData(channelIndex, dt, multi, version, body);
                if (dt != DataType.STRING && dt != DataType.BINARY) {
                    return checkNumericConsumed(absBlock, channelIndex, body, integrityActive);
                }
                return absBlock;
            default: // unknown / reserved ≥ 9
                return new Block.Skipped(channelIndex,
                        Block.SkipReason.RESERVED_BLOCK_TYPE, length);
        }
    }

    /**
     * Strict full-consume diagnostic for a fixed-width numeric block: after the
     * declared samples are read the body must be empty. Trailing bytes signal a
     * writer/sample-count mismatch. The frame CRC (when a profile is active)
     * already guarantees the frame's integrity end-to-end, so — matching the
     * Rust/C++ reference readers, which do not reject on this — the surplus is
     * logged as a warning rather than dropping the block. Conformant files never
     * trip it.
     */
    private static Block checkNumericConsumed(Block block, int channelIndex, ByteBuffer body,
                                              boolean integrityActive) {
        if (body.hasRemaining()) {
            LOG.log(System.Logger.Level.WARNING, () -> "channel " + channelIndex
                    + ": numeric block left " + body.remaining() + " unconsumed byte(s)"
                    + (integrityActive ? " under an active integrity profile" : ""));
        }
        return block;
    }

    private static int readSampleCount(boolean multi, ByteBuffer body) {
        if (!multi) return 1;
        int raw = body.getInt(); // read as signed int (reinterpret of u32)
        if (raw < 0) {
            // The u32 value is in the range 2^31..2^32-1 — not supported.
            throw new OsfException.MalformedFile(
                    "sample count exceeds supported maximum");
        }
        return raw;
    }

    // --- bcStartData (numeric only) ---

    private static Block parseStartData(int channelIndex, DataType dt, boolean multi,
                                        ByteBuffer body) {
        long startTs = body.getLong();
        double rate = body.getDouble();
        int n = readSampleCount(multi, body);
        Block.Values values = readNumericRun(dt, n, body);
        return new Block.StartData(channelIndex, startTs, rate, values);
    }

    // --- bcContinuedData (numeric only) ---

    private static Block parseContinuedData(int channelIndex, DataType dt, boolean multi,
                                            ByteBuffer body) {
        int n = readSampleCount(multi, body);
        Block.Values values = readNumericRun(dt, n, body);
        return new Block.ContinuedData(channelIndex, values);
    }

    // --- bcContinuedRelStampData (numeric only, OSF4-era) ---

    private static Block parseContinuedRelStampData(int channelIndex, DataType dt,
                                                    boolean multi, ByteBuffer body) {
        int n = readSampleCount(multi, body);
        long[] deltas = new long[n];
        // Read deltas interleaved with values into parallel arrays.
        Object scratch = newValueArray(dt, n);
        for (int i = 0; i < n; i++) {
            deltas[i] = Integer.toUnsignedLong(body.getInt());
            readOneNumericInto(dt, scratch, i, body);
        }
        return new Block.RelTimestampData(channelIndex, deltas, wrapValueArray(dt, scratch));
    }

    // --- bcAbsTimeStampData ---

    private static Block parseAbsTimestampData(int channelIndex, DataType dt, boolean multi,
                                               OsfVersion version, ByteBuffer body) {
        if (dt == DataType.STRING || dt == DataType.BINARY) {
            return parseAbsTsStringOrBinary(channelIndex, dt, multi, version, body);
        }
        if (dt == DataType.GPS_LOCATION) {
            int n = readSampleCount(multi, body);
            long[] ts = new long[n];
            GpsLocation[] vals = new GpsLocation[n];
            for (int i = 0; i < n; i++) {
                ts[i] = body.getLong();
                double lat = body.getDouble();
                double lon = body.getDouble();
                double alt = body.getDouble();
                vals[i] = new GpsLocation(lat, lon, alt);
            }
            return new Block.AbsTimestampData(channelIndex, ts, new Block.GpsValues(vals));
        }
        // Numeric AbsTs: interleaved (i64 ts, value) pairs.
        int n = readSampleCount(multi, body);
        long[] ts = new long[n];
        Object scratch = newValueArray(dt, n);
        for (int i = 0; i < n; i++) {
            ts[i] = body.getLong();
            readOneNumericInto(dt, scratch, i, body);
        }
        return new Block.AbsTimestampData(channelIndex, ts, wrapValueArray(dt, scratch));
    }

    /**
     * {@code bcAbsTimeStampData} for {@code string}/{@code binary}. Per spec the
     * multi-sample bit is set; we also tolerate the bit-7-clear implicit-N=1
     * form. With N&gt;1 the spec mandates equal-length segments; we split the
     * rest equally. The version-deterministic null-terminator rule is applied
     * per sample (OSF4 strips the last byte; OSF5 keeps verbatim).
     */
    private static Block parseAbsTsStringOrBinary(int channelIndex, DataType dt, boolean multi,
                                                  OsfVersion version, ByteBuffer body) {
        int n;
        if (multi) {
            long raw = Integer.toUnsignedLong(body.getInt());
            if (raw == 0) {
                return buildStringOrBinary(channelIndex, dt, new long[0], new byte[0][]);
            }
            n = (int) raw;
        } else {
            n = 1;
        }

        int rest = body.remaining();

        if (n == 1) {
            long ts = body.getLong();
            byte[] payload = new byte[body.remaining()];
            body.get(payload);
            payload = stripOsf4Terminator(payload, version);
            return buildStringOrBinary(channelIndex, dt,
                    new long[]{ts}, new byte[][]{payload});
        }

        // N > 1: equal-length segments. If not divisible, fall back to a single
        // sample (mirrors the reference warn-and-degrade path).
        if (rest % n != 0) {
            long ts = body.getLong();
            byte[] payload = new byte[body.remaining()];
            body.get(payload);
            payload = stripOsf4Terminator(payload, version);
            return buildStringOrBinary(channelIndex, dt,
                    new long[]{ts}, new byte[][]{payload});
        }

        int perSample = rest / n;
        int minPerSample = (version == OsfVersion.OSF4) ? 9 : 8;
        if (perSample < minPerSample) {
            throw new OsfException.MalformedFile(
                    "bcAbsTimeStampData for " + dt + " N=" + n + ": per-sample size "
                    + perSample + " is less than " + minPerSample);
        }
        long[] ts = new long[n];
        byte[][] payloads = new byte[n][];
        for (int i = 0; i < n; i++) {
            ts[i] = body.getLong();
            byte[] payload = new byte[perSample - 8];
            body.get(payload);
            payloads[i] = stripOsf4Terminator(payload, version);
        }
        return buildStringOrBinary(channelIndex, dt, ts, payloads);
    }

    private static Block buildStringOrBinary(int channelIndex, DataType dt,
                                             long[] ts, byte[][] payloads) {
        if (dt == DataType.STRING) {
            String[] strs = new String[payloads.length];
            for (int i = 0; i < payloads.length; i++) {
                strs[i] = new String(payloads[i], StandardCharsets.UTF_8);
            }
            return new Block.AbsTimestampData(channelIndex, ts,
                    new Block.StringValues(strs));
        }
        return new Block.AbsTimestampData(channelIndex, ts,
                new Block.BinaryValues(payloads));
    }

    /**
     * Version-deterministic null-terminator rule (spec rev 2026-05-24):
     * OSF4 strips the last byte unconditionally; OSF5 keeps the payload verbatim.
     */
    private static byte[] stripOsf4Terminator(byte[] bytes, OsfVersion version) {
        if (version == OsfVersion.OSF4 && bytes.length > 0) {
            byte[] out = new byte[bytes.length - 1];
            System.arraycopy(bytes, 0, out, 0, out.length);
            return out;
        }
        return bytes;
    }

    // ---------------------------------------------------------------
    // Numeric run helpers. Equidistant + numeric-AbsTs share these.
    // ---------------------------------------------------------------

    /** Read `n` contiguous numeric values of `dt` into a fresh Values record. */
    private static Block.Values readNumericRun(DataType dt, int n, ByteBuffer body) {
        Object arr = newValueArray(dt, n);
        for (int i = 0; i < n; i++) readOneNumericInto(dt, arr, i, body);
        return wrapValueArray(dt, arr);
    }

    /** Allocate the primitive array matching `dt`. Numeric types only. */
    private static Object newValueArray(DataType dt, int n) {
        return switch (dt) {
            case BOOL -> new boolean[n];
            case INT8, UINT8 -> new byte[n];
            case INT16, UINT16 -> new short[n];
            case INT32, UINT32 -> new int[n];
            case INT64, UINT64 -> new long[n];
            case FLOAT -> new float[n];
            case DOUBLE -> new double[n];
            default -> throw new OsfException.MalformedFile(
                    "equidistant / numeric block does not support datatype " + dt);
        };
    }

    /** Read one value of `dt` from `body` into slot `i` of the typed array. */
    private static void readOneNumericInto(DataType dt, Object arr, int i, ByteBuffer body) {
        switch (dt) {
            case BOOL -> ((boolean[]) arr)[i] = body.get() != 0;
            case INT8, UINT8 -> ((byte[]) arr)[i] = body.get();
            case INT16, UINT16 -> ((short[]) arr)[i] = body.getShort();
            case INT32, UINT32 -> ((int[]) arr)[i] = body.getInt();
            case INT64, UINT64 -> ((long[]) arr)[i] = body.getLong();
            case FLOAT -> ((float[]) arr)[i] = body.getFloat();
            case DOUBLE -> ((double[]) arr)[i] = body.getDouble();
            default -> throw new OsfException.MalformedFile(
                    "numeric block does not support datatype " + dt);
        }
    }

    /** Wrap the filled primitive array in the matching Values record. */
    private static Block.Values wrapValueArray(DataType dt, Object arr) {
        return switch (dt) {
            case BOOL -> new Block.BoolValues((boolean[]) arr);
            case INT8 -> new Block.ByteValues((byte[]) arr);
            case UINT8 -> new Block.UByteValues((byte[]) arr);
            case INT16 -> new Block.ShortValues((short[]) arr);
            case UINT16 -> new Block.UShortValues((short[]) arr);
            case INT32 -> new Block.IntValues((int[]) arr);
            case UINT32 -> new Block.UIntValues((int[]) arr);
            case INT64 -> new Block.LongValues((long[]) arr);
            case UINT64 -> new Block.ULongValues((long[]) arr);
            case FLOAT -> new Block.FloatValues((float[]) arr);
            case DOUBLE -> new Block.DoubleValues((double[]) arr);
            default -> throw new OsfException.MalformedFile(
                    "numeric block does not support datatype " + dt);
        };
    }
}
