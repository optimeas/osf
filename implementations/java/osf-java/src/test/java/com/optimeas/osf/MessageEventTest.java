// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import com.optimeas.osf.testutil.ExamplesDir;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.List;
import java.util.stream.Collectors;

import static org.assertj.core.api.Assertions.assertThat;

/**
 * Integration tests for {@code bcMessageEvent} (control byte 4) decoding
 * (OSF-UP4, DECISIONS §26).
 *
 * <p>Loads the committed conformance pair, mirroring the Rust reference
 * {@code implementations/rust/osf-core/tests/message_event_test.rs} and the
 * C++ reference {@code
 * implementations/cpp/tests/integration/test_message_event_examples.cpp}:
 * <ul>
 *   <li>{@code examples/generated/osf4_message_event_string.osf} —
 *       {@code Demo.Message} written as {@code bcMessageEvent} (the
 *       deployed-firmware encoding).</li>
 *   <li>{@code examples/generated/osf4_message_event_string_equivalent.osf}
 *       — the same channel content, but {@code Demo.Message} written as
 *       {@code bcAbsTimeStampData} instead.</li>
 * </ul>
 *
 * <p>Both files carry {@code Demo.Counter} ({@code uint32}, 5 samples) and
 * {@code Demo.Message} ({@code string}, 5 samples) at the same five
 * timestamps. The two files use <b>different block ordering</b> (legacy is
 * channel-major, equivalent is round-robin) — deliberate, per the
 * generator's doc comment
 * ({@code implementations/rust/osf-core/examples/gen_message_event_refs.rs}).
 * Tests here must never assume matching block order between the two files;
 * only decoded content is compared.
 *
 * <p>Uses {@link ExamplesDir} (like {@link ManagerExamplesTest}) so a
 * checkout without the corpus skips these tests rather than failing.
 */
class MessageEventTest {

    private static final long BASE_TIMESTAMP_NS = 1_768_478_400_000_000_000L;
    private static final long TIMESTAMP_STEP_NS = 5_000_000_000L;

    private static final String LONG_MESSAGE = "A".repeat(300);

    private static final String[] MESSAGE_TEXTS = {
            "OSF-DEMO-0001",
            "no signal",
            "",
            "Grüße aus Säckingen ✓",
            LONG_MESSAGE,
    };

    private static final long[] COUNTER_VALUES = {10, 20, 30, 40, 50};

    private static Path messageEventPath() {
        return ExamplesDir.resolve().resolve("generated").resolve("osf4_message_event_string.osf");
    }

    private static Path equivalentPath() {
        return ExamplesDir.resolve().resolve("generated")
                .resolve("osf4_message_event_string_equivalent.osf");
    }

    private static long[] expectedTimestamps() {
        long[] ts = new long[5];
        for (int i = 0; i < 5; i++) {
            ts[i] = BASE_TIMESTAMP_NS + i * TIMESTAMP_STEP_NS;
        }
        return ts;
    }

    /**
     * The legacy {@code bcMessageEvent} file must decode {@code Demo.Message}
     * to all five samples in order, and the terminator guard must hold — a
     * reader that wrongly reused {@code bcAbsTimeStampData}'s null-terminator
     * strip would lose the last character/byte of every value, which four of
     * the five samples would still "look plausible" after (only the empty
     * string and the truncated long string would look obviously wrong), so
     * exact last-character / exact-length assertions are pinned rather than
     * mere presence.
     */
    @Test
    void messageEventChannelDecodesAllFiveSamples() {
        DataManager mgr = DataManager.loadFromFile(messageEventPath());

        DataChannel messageCh = mgr.channelByName("Demo.Message")
                .orElseThrow(() -> new AssertionError("Demo.Message declared"));
        assertThat(messageCh.kind()).isEqualTo(DataChannel.Kind.VARIABLE);

        String[] texts = messageCh.asStrings();
        long[] timestamps = messageCh.timestampsNs();

        assertThat(timestamps).as("Demo.Message timestamps").hasSize(5);
        assertThat(texts).as("Demo.Message samples").hasSize(5);
        assertThat(timestamps).as("Demo.Message timestamps").isEqualTo(expectedTimestamps());
        assertThat(texts).as("Demo.Message samples").isEqualTo(MESSAGE_TEXTS);

        // Terminator guard: stripOsf4Terminator must NOT have been applied to
        // this path. If it had, every sample here would be missing its last
        // byte/char.
        assertThat(texts[0]).as("sample 0 must end with '1' (\"OSF-DEMO-0001\")").endsWith("1");
        assertThat(texts[3]).as("sample 3 must end with the checkmark").endsWith("✓");
        assertThat(texts[4]).as("sample 4 (long message) must keep all 300 bytes").hasSize(300);
        assertThat(texts[2]).as("sample 2 must be the empty string, not truncated to nothing else")
                .isEqualTo("");

        // Demo.Counter must still decode fully — sanity check that the fix
        // did not disturb the other channel.
        DataChannel counterCh = mgr.channelByName("Demo.Counter")
                .orElseThrow(() -> new AssertionError("Demo.Counter declared"));
        assertThat(counterCh.timestampsNs()).as("Demo.Counter samples").hasSize(5);
    }

    /**
     * Block-count invariants. {@code Java has no blocksTotal aggregate}
     * (pre-existing asymmetry vs. the Rust/C++ references — stays as-is), so
     * this asserts {@code blocksRead} directly instead of a recomputed sum.
     */
    @Test
    void messageEventCountsAsReadNotSkipped() {
        DataManager mgr = DataManager.loadFromFile(messageEventPath());
        ReaderStats stats = mgr.stats();

        assertThat(stats.blocksRead())
                .as("all 10 blocks (5 counter + 5 message) must be read")
                .isEqualTo(10);
        assertThat(stats.truncationSeen()).isFalse();
    }

    /**
     * The two encodings must decode to the same channel-for-channel content.
     * The two files use different block orderings on disk (channel-major vs.
     * round-robin) — this test compares decoded content only, never raw
     * block sequence or byte layout.
     */
    @Test
    void bothEncodingsDecodeIdentically() {
        DataManager legacy = DataManager.loadFromFile(messageEventPath());
        DataManager equivalent = DataManager.loadFromFile(equivalentPath());

        List<String> legacyNames = legacy.channels().stream().map(DataChannel::name)
                .collect(Collectors.toList());
        List<String> equivalentNames = equivalent.channels().stream().map(DataChannel::name)
                .collect(Collectors.toList());
        assertThat(equivalentNames).as("same channel names in both files").isEqualTo(legacyNames);

        // Demo.Counter: same sample count + timestamp sequence in both.
        DataChannel legacyCounter = legacy.channelByName("Demo.Counter")
                .orElseThrow(() -> new AssertionError("Demo.Counter declared"));
        DataChannel equivalentCounter = equivalent.channelByName("Demo.Counter")
                .orElseThrow(() -> new AssertionError("Demo.Counter declared"));
        assertThat(equivalentCounter.timestampsNs()).isEqualTo(legacyCounter.timestampsNs());
        assertThat(legacyCounter.asLongs()).isEqualTo(COUNTER_VALUES);
        assertThat(equivalentCounter.asLongs()).isEqualTo(COUNTER_VALUES);

        // Demo.Message: same sample count, same timestamp sequence, same
        // decoded values.
        DataChannel legacyMessage = legacy.channelByName("Demo.Message")
                .orElseThrow(() -> new AssertionError("Demo.Message declared"));
        DataChannel equivalentMessage = equivalent.channelByName("Demo.Message")
                .orElseThrow(() -> new AssertionError("Demo.Message declared"));
        assertThat(equivalentMessage.timestampsNs()).isEqualTo(legacyMessage.timestampsNs());
        assertThat(equivalentMessage.asStrings())
                .as("Demo.Message must decode to identical values from both encodings")
                .isEqualTo(legacyMessage.asStrings());
        assertThat(legacyMessage.asStrings()).isEqualTo(MESSAGE_TEXTS);
    }
}
