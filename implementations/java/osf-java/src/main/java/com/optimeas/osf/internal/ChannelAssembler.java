// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

import com.optimeas.osf.ChannelDef;
import com.optimeas.osf.ChannelType;
import com.optimeas.osf.DataChannel;
import com.optimeas.osf.DataType;
import com.optimeas.osf.GpsLocation;
import com.optimeas.osf.OsfException;

import java.util.ArrayList;
import java.util.List;

/**
 * Folds the flat {@code List<Block>} from the {@link BlockReader} together with
 * the metablock channel definitions into typed {@link DataChannel}s.
 *
 * <p>Faithful port of the Rust reference manager layer
 * ({@code implementations/rust/osf-core/src/manager.rs}, function
 * {@code build_channels} + the {@code ChannelBuilder} state machine) and the
 * channel model ({@code data_channel.rs}). Channels are returned in metablock
 * order; {@code UNSUPPORTED} channels (data type or channel type) are dropped,
 * since the reader has already emitted only {@code Skipped} blocks for them.
 *
 * <h2>Assembly rules (mirroring {@code manager.rs})</h2>
 * <ul>
 *   <li>{@code StartData} opens an equidistant segment at its absolute start
 *       timestamp and sample rate; the channel locks into the
 *       {@link DataChannel.Kind#EQUIDISTANT} layout. A second {@code StartData}
 *       opens a second segment.</li>
 *   <li>{@code ContinuedData} extends the channel's most recent segment.</li>
 *   <li>{@code AbsTimestampData} on a numeric/GPS channel appends to a
 *       {@link DataChannel.Kind#TIMESTAMPED} layout with explicit per-sample
 *       timestamps; on a string/binary channel it appends to a
 *       {@link DataChannel.Kind#VARIABLE} layout.</li>
 *   <li>{@code RelTimestampData} extends a timestamped channel: each delta is
 *       added cumulatively to the last absolute timestamp seen on the channel
 *       (the anchor) — {@code last = last + delta}, matching
 *       {@code extend_rel_timestamped}.</li>
 *   <li>Equidistant timestamps are reconstructed per segment as
 *       {@code start + (long)(i * 1e9 / sample_rate_hz)} (truncated toward
 *       zero, saturating add), matching {@code segment_timestamp}. Gaps between
 *       segments are not interpolated.</li>
 * </ul>
 *
 * <p>This assembler is intentionally permissive about block-type mixing: the
 * Java reader is best-effort and the corpus never mixes block families on one
 * channel, so a stray off-family block is ignored rather than raising the
 * reference's {@code ChannelMixedBlockTypes}. The first typed block decides the
 * channel's {@link DataChannel.Kind}.
 */
public final class ChannelAssembler {

    private ChannelAssembler() {}

    /**
     * Assemble typed channels in metablock order.
     *
     * @param channelDefs channel definitions in on-disk order
     * @param blocks      decoded blocks from the {@link BlockReader}
     * @return the typed channels, {@code UNSUPPORTED} ones dropped
     */
    public static List<DataChannel> assemble(List<ChannelDef> channelDefs, List<Block> blocks) {
        // One builder per channel, keyed by index for block routing.
        java.util.Map<Integer, Builder> byIndex = new java.util.HashMap<>();
        List<Builder> order = new ArrayList<>(channelDefs.size());
        for (ChannelDef def : channelDefs) {
            Builder b = new Builder(def);
            byIndex.put(def.index(), b);
            order.add(b);
        }

        for (Block block : blocks) {
            Builder b = byIndex.get(block.channelIndex());
            if (b != null) {
                b.apply(block);
            }
        }

        List<DataChannel> out = new ArrayList<>(order.size());
        for (Builder b : order) {
            DataChannel ch = b.finish();
            if (ch != null) {
                out.add(ch);
            }
        }
        return out;
    }

    // -----------------------------------------------------------
    // Per-channel builder + state machine.
    // -----------------------------------------------------------

    private enum State { PENDING, EQUIDISTANT, TIMESTAMPED, VARIABLE, UNSUPPORTED }

    private static final class Builder {
        final ChannelDef def;
        State state;

        // Accumulated value chunks (one per contributing block), concatenated
        // at finish(). Keeps assembly O(total samples) without growable
        // primitive buffers per type.
        final List<Block.Values> valueChunks = new ArrayList<>();
        // Timestamped/variable: explicit timestamps, one chunk per block.
        final List<long[]> timestampChunks = new ArrayList<>();
        // Equidistant: one segment per StartData; the last one is extended by
        // ContinuedData.
        final List<DataChannel.Segment> segments = new ArrayList<>();
        int equidistantTotal = 0; // running sample count across segments
        // Anchor for rel-stamp deltas: last absolute timestamp observed.
        long lastTimestampNs = 0;
        boolean haveAnchor = false;

        Builder(ChannelDef def) {
            this.def = def;
            this.state = initialState(def);
        }

        private static State initialState(ChannelDef def) {
            if (def.dataType() == DataType.UNSUPPORTED
                    || def.channelType() == ChannelType.UNSUPPORTED) {
                return State.UNSUPPORTED;
            }
            return switch (def.dataType()) {
                case STRING, BINARY -> State.VARIABLE;
                default -> State.PENDING;
            };
        }

        void apply(Block block) {
            if (state == State.UNSUPPORTED) {
                return;
            }
            switch (block) {
                case Block.Skipped ignored -> { /* alignment-only */ }
                case Block.StartData s -> applyStart(s);
                case Block.ContinuedData c -> applyContinued(c);
                case Block.AbsTimestampData a -> applyAbs(a);
                case Block.RelTimestampData r -> applyRel(r);
            }
        }

        private void applyStart(Block.StartData s) {
            // First typed block decides EQUIDISTANT; off-family blocks afterward
            // are ignored by the switch in apply().
            if (state == State.TIMESTAMPED || state == State.VARIABLE) {
                return; // mixed families: ignore (best-effort)
            }
            state = State.EQUIDISTANT;
            int startIndex = equidistantTotal;
            int count = s.values().length();
            valueChunks.add(s.values());
            equidistantTotal += count;
            segments.add(new DataChannel.Segment(
                    s.startTimestampNs(), s.sampleRateHz(), startIndex, count));
            updateAnchorFromSegment(s.startTimestampNs(), s.sampleRateHz(), count);
        }

        private void applyContinued(Block.ContinuedData c) {
            if (state != State.EQUIDISTANT || segments.isEmpty()) {
                return; // continued-without-start: ignore (best-effort)
            }
            int count = c.values().length();
            valueChunks.add(c.values());
            equidistantTotal += count;
            DataChannel.Segment last = segments.remove(segments.size() - 1);
            DataChannel.Segment extended = new DataChannel.Segment(
                    last.startTimestampNs(), last.sampleRateHz(),
                    last.startIndex(), last.sampleCount() + count);
            segments.add(extended);
            updateAnchorFromSegment(
                    extended.startTimestampNs(), extended.sampleRateHz(), extended.sampleCount());
        }

        private void applyAbs(Block.AbsTimestampData a) {
            if (state == State.VARIABLE) {
                timestampChunks.add(a.timestamps());
                valueChunks.add(a.values());
            } else if (state == State.PENDING || state == State.TIMESTAMPED) {
                state = State.TIMESTAMPED;
                timestampChunks.add(a.timestamps());
                valueChunks.add(a.values());
            } else {
                return; // EQUIDISTANT already: ignore off-family
            }
            long[] ts = a.timestamps();
            if (ts.length > 0) {
                lastTimestampNs = ts[ts.length - 1];
                haveAnchor = true;
            }
        }

        private void applyRel(Block.RelTimestampData r) {
            if (state != State.TIMESTAMPED || !haveAnchor) {
                return; // rel-stamp without anchor / wrong family: ignore
            }
            long[] deltas = r.deltasNs();
            long[] abs = new long[deltas.length];
            long last = lastTimestampNs;
            for (int i = 0; i < deltas.length; i++) {
                last = saturatingAdd(last, deltas[i]);
                abs[i] = last;
            }
            timestampChunks.add(abs);
            valueChunks.add(r.values());
            if (deltas.length > 0) {
                lastTimestampNs = last;
            }
        }

        /** Anchor = last reconstructed equidistant timestamp of the segment. */
        private void updateAnchorFromSegment(long start, double rate, int count) {
            if (count == 0) {
                return;
            }
            long last;
            if (rate > 0.0) {
                long offset = (long) ((double) (count - 1) * 1.0e9 / rate);
                last = saturatingAdd(start, offset);
            } else {
                last = start;
            }
            lastTimestampNs = last;
            haveAnchor = true;
        }

        DataChannel finish() {
            return switch (state) {
                case UNSUPPORTED -> null;
                case EQUIDISTANT -> {
                    Block.Values merged = concatNumeric(def.dataType(), valueChunks, equidistantTotal);
                    long[] ts = reconstructEquidistantTimestamps();
                    yield new DataChannel(def.index(), def.name(), def.dataType(),
                            def.channelType(), def.physicalUnit(),
                            DataChannel.Kind.EQUIDISTANT, ts, merged, List.copyOf(segments));
                }
                case TIMESTAMPED -> {
                    long[] ts = concatTimestamps();
                    Block.Values merged = concatNumeric(def.dataType(), valueChunks, ts.length);
                    yield new DataChannel(def.index(), def.name(), def.dataType(),
                            def.channelType(), def.physicalUnit(),
                            DataChannel.Kind.TIMESTAMPED, ts, merged, List.of());
                }
                case VARIABLE -> {
                    long[] ts = concatTimestamps();
                    Block.Values merged = concatVariable(def.dataType(), valueChunks, ts.length);
                    yield new DataChannel(def.index(), def.name(), def.dataType(),
                            def.channelType(), def.physicalUnit(),
                            DataChannel.Kind.VARIABLE, ts, merged, List.of());
                }
                case PENDING -> {
                    // Channel exists in metadata but received no typed block:
                    // emit an empty channel of the right family/kind.
                    Block.Values empty = emptyNumeric(def.dataType());
                    yield new DataChannel(def.index(), def.name(), def.dataType(),
                            def.channelType(), def.physicalUnit(),
                            DataChannel.Kind.EQUIDISTANT, new long[0], empty, List.of());
                }
            };
        }

        private long[] reconstructEquidistantTimestamps() {
            long[] ts = new long[equidistantTotal];
            int pos = 0;
            for (DataChannel.Segment seg : segments) {
                for (int i = 0; i < seg.sampleCount(); i++) {
                    long t;
                    if (seg.sampleRateHz() > 0.0 && i > 0) {
                        long offset = (long) ((double) i * 1.0e9 / seg.sampleRateHz());
                        t = saturatingAdd(seg.startTimestampNs(), offset);
                    } else {
                        t = seg.startTimestampNs();
                    }
                    ts[pos++] = t;
                }
            }
            return ts;
        }

        private long[] concatTimestamps() {
            int total = 0;
            for (long[] chunk : timestampChunks) {
                total += chunk.length;
            }
            long[] out = new long[total];
            int pos = 0;
            for (long[] chunk : timestampChunks) {
                System.arraycopy(chunk, 0, out, pos, chunk.length);
                pos += chunk.length;
            }
            return out;
        }
    }

    private static long saturatingAdd(long a, long b) {
        long r = a + b;
        // Overflow iff the sign of both inputs is the same and differs from r.
        if (((a ^ r) & (b ^ r)) < 0) {
            return (b < 0) ? Long.MIN_VALUE : Long.MAX_VALUE;
        }
        return r;
    }

    // -----------------------------------------------------------
    // Value-chunk concatenation, one path per Java storage primitive.
    // -----------------------------------------------------------

    private static Block.Values emptyNumeric(DataType dt) {
        return concatNumeric(dt, List.of(), 0);
    }

    private static Block.Values concatNumeric(DataType dt, List<Block.Values> chunks, int total) {
        return switch (dt) {
            case BOOL -> {
                boolean[] a = new boolean[total];
                int p = 0;
                for (Block.Values v : chunks) {
                    boolean[] s = ((Block.BoolValues) v).values();
                    System.arraycopy(s, 0, a, p, s.length); p += s.length;
                }
                yield new Block.BoolValues(a);
            }
            case INT8 -> {
                byte[] a = new byte[total];
                int p = 0;
                for (Block.Values v : chunks) {
                    byte[] s = ((Block.ByteValues) v).values();
                    System.arraycopy(s, 0, a, p, s.length); p += s.length;
                }
                yield new Block.ByteValues(a);
            }
            case UINT8 -> {
                byte[] a = new byte[total];
                int p = 0;
                for (Block.Values v : chunks) {
                    byte[] s = ((Block.UByteValues) v).values();
                    System.arraycopy(s, 0, a, p, s.length); p += s.length;
                }
                yield new Block.UByteValues(a);
            }
            case INT16 -> {
                short[] a = new short[total];
                int p = 0;
                for (Block.Values v : chunks) {
                    short[] s = ((Block.ShortValues) v).values();
                    System.arraycopy(s, 0, a, p, s.length); p += s.length;
                }
                yield new Block.ShortValues(a);
            }
            case UINT16 -> {
                short[] a = new short[total];
                int p = 0;
                for (Block.Values v : chunks) {
                    short[] s = ((Block.UShortValues) v).values();
                    System.arraycopy(s, 0, a, p, s.length); p += s.length;
                }
                yield new Block.UShortValues(a);
            }
            case INT32 -> {
                int[] a = new int[total];
                int p = 0;
                for (Block.Values v : chunks) {
                    int[] s = ((Block.IntValues) v).values();
                    System.arraycopy(s, 0, a, p, s.length); p += s.length;
                }
                yield new Block.IntValues(a);
            }
            case UINT32 -> {
                int[] a = new int[total];
                int p = 0;
                for (Block.Values v : chunks) {
                    int[] s = ((Block.UIntValues) v).values();
                    System.arraycopy(s, 0, a, p, s.length); p += s.length;
                }
                yield new Block.UIntValues(a);
            }
            case INT64 -> {
                long[] a = new long[total];
                int p = 0;
                for (Block.Values v : chunks) {
                    long[] s = ((Block.LongValues) v).values();
                    System.arraycopy(s, 0, a, p, s.length); p += s.length;
                }
                yield new Block.LongValues(a);
            }
            case UINT64 -> {
                long[] a = new long[total];
                int p = 0;
                for (Block.Values v : chunks) {
                    long[] s = ((Block.ULongValues) v).values();
                    System.arraycopy(s, 0, a, p, s.length); p += s.length;
                }
                yield new Block.ULongValues(a);
            }
            case FLOAT -> {
                float[] a = new float[total];
                int p = 0;
                for (Block.Values v : chunks) {
                    float[] s = ((Block.FloatValues) v).values();
                    System.arraycopy(s, 0, a, p, s.length); p += s.length;
                }
                yield new Block.FloatValues(a);
            }
            case DOUBLE -> {
                double[] a = new double[total];
                int p = 0;
                for (Block.Values v : chunks) {
                    double[] s = ((Block.DoubleValues) v).values();
                    System.arraycopy(s, 0, a, p, s.length); p += s.length;
                }
                yield new Block.DoubleValues(a);
            }
            case GPS_LOCATION -> {
                GpsLocation[] a = new GpsLocation[total];
                int p = 0;
                for (Block.Values v : chunks) {
                    GpsLocation[] s = ((Block.GpsValues) v).values();
                    System.arraycopy(s, 0, a, p, s.length); p += s.length;
                }
                yield new Block.GpsValues(a);
            }
            default -> throw new OsfException.MalformedFile(
                    "cannot assemble numeric/GPS channel for datatype " + dt);
        };
    }

    private static Block.Values concatVariable(DataType dt, List<Block.Values> chunks, int total) {
        if (dt == DataType.STRING) {
            String[] a = new String[total];
            int p = 0;
            for (Block.Values v : chunks) {
                String[] s = ((Block.StringValues) v).values();
                System.arraycopy(s, 0, a, p, s.length); p += s.length;
            }
            return new Block.StringValues(a);
        }
        // BINARY (and bytearray alias, already mapped to BINARY by DataType)
        byte[][] a = new byte[total][];
        int p = 0;
        for (Block.Values v : chunks) {
            byte[][] s = ((Block.BinaryValues) v).values();
            System.arraycopy(s, 0, a, p, s.length); p += s.length;
        }
        return new Block.BinaryValues(a);
    }
}
