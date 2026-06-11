// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import com.optimeas.osf.internal.Block;
import com.optimeas.osf.internal.BlockReader;
import com.optimeas.osf.internal.ChannelAssembler;
import com.optimeas.osf.internal.OsfzInputStream;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

/**
 * High-level read-only view of an OSF file: the file-level metadata, the typed
 * channel list, and the reader telemetry.
 *
 * <p>{@code DataManager} is the top tier of the read API and the documented
 * entry point. It drives the full pipeline:
 * <pre>
 *   magic header → metablock → block stream → typed channels
 * </pre>
 * mirroring the Rust reference {@code DataManager}
 * ({@code implementations/rust/osf-core/src/manager.rs}) and the C++
 * {@code osf::DataManager}.
 *
 * <p>Construct with {@link #loadFromFile(Path)} for the convenience case (open
 * by path) or {@link #load(InputStream)} for streaming sources. Both detect
 * gzip/zlib-compressed OSFZ input transparently via
 * {@link com.optimeas.osf.internal.OsfzInputStream} before parsing the magic
 * header; {@link ReaderStats#compressed()} and
 * {@link ReaderStats#compressionFormat()} reflect the detected format.
 */
public final class DataManager {

    private final Map<String, String> metadata;
    private final List<DataChannel> channels;
    private final Map<String, DataChannel> byName;
    private final Map<Integer, DataChannel> byIndex;
    private final ReaderStats stats;

    private DataManager(Map<String, String> metadata, List<DataChannel> channels,
                        ReaderStats stats) {
        this.metadata = metadata;
        this.channels = channels;
        this.stats = stats;
        Map<String, DataChannel> nameMap = new LinkedHashMap<>();
        Map<Integer, DataChannel> indexMap = new LinkedHashMap<>();
        for (DataChannel ch : channels) {
            // First definition wins on a duplicate name (matches the reference).
            nameMap.putIfAbsent(ch.name(), ch);
            indexMap.put(ch.index(), ch);
        }
        this.byName = nameMap;
        this.byIndex = indexMap;
    }

    /**
     * Load an OSF file from an arbitrary stream.
     *
     * <p>The stream is consumed fully: the magic header is parsed (advancing the
     * stream to the metablock), exactly {@code metablockLength} bytes are read
     * and parsed, and the remaining bytes are handed to the best-effort block
     * reader. A truncated/garbled trailing block stops the read and sets
     * {@link ReaderStats#truncationSeen()} rather than throwing.
     *
     * @param in the input stream (plain or gzip/zlib-compressed)
     * @return the assembled manager
     * @throws OsfException.MalformedFile on a malformed header/metablock, or an
     *         I/O error, or a metablock length exceeding {@code Integer.MAX_VALUE}
     */
    public static DataManager load(InputStream in) {
        try {
            ReaderStats stats = new ReaderStats();
            // Detect and transparently decompress gzip/zlib (OSFZ) input
            // before the magic-header parse. The magic-header parser stays
            // non-decompressing — it sees the decompressed bytes.
            InputStream decompressed = OsfzInputStream.wrap(in, stats::setCompression);

            MagicHeader header = MagicHeaderParser.parse(decompressed);

            long metaLen = header.metablockLength();
            if (metaLen > Integer.MAX_VALUE) {
                throw new OsfException.MalformedFile(
                        "metablock length " + Long.toUnsignedString(metaLen)
                        + " exceeds the supported maximum of " + Integer.MAX_VALUE);
            }
            byte[] metaBytes = readExactly(decompressed, (int) metaLen);
            Metablock meta = MetablockParser.parse(header.version(), metaBytes);

            byte[] rest = decompressed.readAllBytes();

            Map<Integer, ChannelDef> channelsByIndex = new HashMap<>();
            for (ChannelDef def : meta.channels()) {
                channelsByIndex.put(def.index(), def);
            }

            List<Block> blocks = BlockReader.readAll(rest, header.version(), channelsByIndex, stats);
            List<DataChannel> channels = ChannelAssembler.assemble(meta.channels(), blocks);

            return new DataManager(meta.metadata(), channels, stats);
        } catch (IOException e) {
            throw new OsfException.MalformedFile("I/O error reading OSF stream: " + e.getMessage(), e);
        }
    }

    /**
     * Load an OSF file from a filesystem path.
     *
     * @param path the file to read
     * @return the assembled manager
     * @throws OsfException.MalformedFile on a malformed file or I/O error
     */
    public static DataManager loadFromFile(Path path) {
        try (InputStream in = Files.newInputStream(path)) {
            return load(in);
        } catch (IOException e) {
            throw new OsfException.MalformedFile(
                    "I/O error opening OSF file " + path + ": " + e.getMessage(), e);
        }
    }

    /** All channels in metablock order ({@code UNSUPPORTED} channels dropped). */
    public List<DataChannel> channels() {
        return channels;
    }

    /** Look up a channel by its fully-qualified name. */
    public Optional<DataChannel> channelByName(String name) {
        return Optional.ofNullable(byName.get(name));
    }

    /** Look up a channel by its on-disk metablock index. */
    public Optional<DataChannel> channelByIndex(int index) {
        return Optional.ofNullable(byIndex.get(index));
    }

    /** File-level metadata from the metablock {@code "file"} block. */
    public Map<String, String> metadata() {
        return metadata;
    }

    /** Block-reader telemetry (blocks read, truncation, compression). */
    public ReaderStats stats() {
        return stats;
    }

    /**
     * Read exactly {@code n} bytes, or throw if the stream ends first. Needed
     * because {@code InputStream.read(byte[])} may return a short count even
     * when more bytes are available later.
     */
    private static byte[] readExactly(InputStream in, int n) throws IOException {
        byte[] buf = new byte[n];
        int off = 0;
        while (off < n) {
            int read = in.read(buf, off, n - off);
            if (read < 0) {
                throw new OsfException.MalformedFile(
                        "unexpected end of input: metablock declared " + n
                        + " bytes but only " + off + " were available");
            }
            off += read;
        }
        return buf;
    }
}
