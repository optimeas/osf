// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// Integration tests for `bcMessageEvent` (control byte 4) decoding
// (OSF-UP4, DECISIONS §26).
//
// Loads the committed conformance pair:
// - `examples/generated/osf4_message_event_string.osf` — `Demo.Message`
//   written as `bcMessageEvent` (the deployed-firmware encoding).
// - `examples/generated/osf4_message_event_string_equivalent.osf` — the
//   same channel content, but `Demo.Message` written as
//   `bcAbsTimeStampData` instead.
//
// Both files carry `Demo.Counter` (`uint32`, 5 samples) and `Demo.Message`
// (`string`, 5 samples) at the same five timestamps. The two files use
// **different block ordering** (legacy is channel-major, equivalent is
// round-robin) — deliberate, per the generator's doc comment
// (implementations/rust/osf-core/examples/gen_message_event_refs.rs).
// Tests here must never assume matching block order between the two
// files; only decoded content is compared.

#include <gtest/gtest.h>

#include <osf/osf.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr std::int64_t BASE_TIMESTAMP_NS = 1'768'478'400'000'000'000LL;
constexpr std::int64_t TIMESTAMP_STEP_NS = 5'000'000'000LL;

std::string longMessage() {
    return std::string(300, 'A');
}

std::vector<std::string> messageTexts() {
    // "Grüße aus Säckingen ✓" in UTF-8. Each \xNN escape is split into its
    // own string-literal segment (adjacent-literal concatenation) so the
    // greedy hex-escape parser cannot gobble a following literal hex digit
    // (e.g. "\x9F" immediately followed by 'e' would otherwise be parsed
    // as the single out-of-range escape "\x9Fe").
    std::string const grussSaeckingen =
        "Gr" "\xC3" "\xBC" "\xC3" "\x9F" "e aus S" "\xC3" "\xA4"
        "ckingen " "\xE2" "\x9C" "\x93";
    return {
        "OSF-DEMO-0001",
        "no signal",
        "",
        grussSaeckingen,
        longMessage(),
    };
}

std::vector<std::uint32_t> const COUNTER_VALUES = {10, 20, 30, 40, 50};

std::vector<std::int64_t> expectedTimestamps() {
    std::vector<std::int64_t> v;
    v.reserve(5);
    for (int i = 0; i < 5; ++i) {
        v.push_back(BASE_TIMESTAMP_NS + static_cast<std::int64_t>(i) * TIMESTAMP_STEP_NS);
    }
    return v;
}

class MessageEventExamplesTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        auto dir = std::filesystem::path{OSF_EXAMPLES_DIR};
        ASSERT_TRUE(std::filesystem::exists(dir))
            << "OSF_EXAMPLES_DIR does not exist: " << dir;
    }

    static std::filesystem::path examplesDir() {
        return std::filesystem::path{OSF_EXAMPLES_DIR};
    }

    static std::filesystem::path messageEventPath() {
        return examplesDir() / "generated" / "osf4_message_event_string.osf";
    }

    static std::filesystem::path equivalentPath() {
        return examplesDir() / "generated" /
               "osf4_message_event_string_equivalent.osf";
    }
};

}  // namespace

// ---------------------------------------------------------------------
// Test 1: the legacy bcMessageEvent file must decode Demo.Message to all
// five samples in order, and the terminator guard must hold — a reader
// that wrongly reused bcAbsTimeStampData's null-terminator strip would
// lose the last character/byte of every value, which four of the five
// samples would still "look plausible" after (only the empty string and
// the truncated long string would look obviously wrong), so exact
// last-character / exact-length assertions are pinned rather than mere
// presence.
// ---------------------------------------------------------------------

TEST_F(MessageEventExamplesTest, message_event_channel_decodes_all_five_samples) {
    auto mgr = osf::DataManager::loadFromFile(messageEventPath());
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;

    auto const* ch = mgr->channel("Demo.Message");
    ASSERT_NE(ch, nullptr) << "Demo.Message declared";
    auto const* vc = std::get_if<osf::VariableChannel>(ch);
    ASSERT_NE(vc, nullptr) << "Demo.Message unexpectedly not a variable channel";

    auto textsR = vc->asStrings();
    ASSERT_TRUE(textsR.has_value()) << "Demo.Message is a string channel";
    auto const& texts = **textsR;
    auto const& timestamps = vc->timestampsNs;

    ASSERT_EQ(timestamps.size(), 5u) << "expected 5 Demo.Message samples";
    ASSERT_EQ(texts.size(), 5u) << "expected 5 Demo.Message samples";

    auto const expectedTs = expectedTimestamps();
    auto const expectedTexts = messageTexts();
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(timestamps[i], expectedTs[i]) << "timestamp mismatch at sample " << i;
        EXPECT_EQ(texts[i], expectedTexts[i]) << "text mismatch at sample " << i;
    }

    // Terminator guard: stripOsf4Terminator must NOT have been applied to
    // this path. If it had, every sample here would be missing its last
    // byte/char.
    EXPECT_TRUE(texts[0].size() > 0 && texts[0].back() == '1')
        << "sample 0 must end with '1' (\"OSF-DEMO-0001\"); got " << texts[0];
    // Sample 3 ("Grüße aus Säckingen ✓") ends with the UTF-8 encoding of
    // '✓' (U+2713 = 0xE2 0x9C 0x93); check the trailing byte sequence
    // rather than a single char (the checkmark is multi-byte in UTF-8).
    ASSERT_GE(texts[3].size(), 3u);
    EXPECT_EQ(static_cast<unsigned char>(texts[3][texts[3].size() - 3]), 0xE2u);
    EXPECT_EQ(static_cast<unsigned char>(texts[3][texts[3].size() - 2]), 0x9Cu);
    EXPECT_EQ(static_cast<unsigned char>(texts[3][texts[3].size() - 1]), 0x93u);
    EXPECT_EQ(texts[4].size(), 300u)
        << "sample 4 (long message) must keep all 300 bytes";
    EXPECT_EQ(texts[2], "")
        << "sample 2 must be the empty string, not truncated to nothing else";

    // Demo.Counter must still decode fully — sanity check that the fix
    // did not disturb the other channel.
    auto const* counterCh = mgr->channel("Demo.Counter");
    ASSERT_NE(counterCh, nullptr) << "Demo.Counter declared";
    auto const* tc = std::get_if<osf::TimestampedChannel>(counterCh);
    ASSERT_NE(tc, nullptr) << "Demo.Counter unexpectedly not a timestamped channel";
    EXPECT_EQ(tc->timestampsNs.size(), 5u) << "expected 5 Demo.Counter samples";
}

// ---------------------------------------------------------------------
// Test 2: block-count invariants. This is the counter-bookkeeping guard —
// blocksTotal is a recomputed sum in BlockReader::stats(), and omitting a
// new term from that sum silently went wrong before. These assertions go
// in from the outset, not bolted on after the fact.
// ---------------------------------------------------------------------

TEST_F(MessageEventExamplesTest, message_event_counts_as_read_not_skipped) {
    auto mgr = osf::DataManager::loadFromFile(messageEventPath());
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    auto const& stats = mgr->stats;

    EXPECT_EQ(stats.blocksRead, 10u)
        << "all 10 blocks (5 counter + 5 message) must be read";
    EXPECT_EQ(stats.blocksSkippedDeprecatedType, 0u)
        << "bcMessageEvent blocks must no longer be counted as deprecated skips";
    EXPECT_EQ(stats.blocksTotal, 10u) << "blocksTotal must equal blocksRead here";
    EXPECT_EQ(stats.blocksTruncated, 0u);
}

// ---------------------------------------------------------------------
// Test 3: the two encodings must decode to the same channel-for-channel
// content. The two files use different block orderings on disk
// (channel-major vs. round-robin) — this test compares decoded content
// only, never raw block sequence or byte layout.
// ---------------------------------------------------------------------

TEST_F(MessageEventExamplesTest, both_encodings_decode_identically) {
    auto legacy = osf::DataManager::loadFromFile(messageEventPath());
    ASSERT_TRUE(legacy.has_value()) << legacy.error().message;
    auto equivalent = osf::DataManager::loadFromFile(equivalentPath());
    ASSERT_TRUE(equivalent.has_value()) << equivalent.error().message;

    ASSERT_EQ(legacy->channels().size(), equivalent->channels().size());
    for (std::size_t i = 0; i < legacy->channels().size(); ++i) {
        EXPECT_EQ(osf::channelName(legacy->channels()[i]),
                  osf::channelName(equivalent->channels()[i]))
            << "same channel names in both files, position " << i;
    }

    // Demo.Counter: same sample count + timestamp sequence in both.
    auto const* legacyCounterCh = legacy->channel("Demo.Counter");
    auto const* equivCounterCh = equivalent->channel("Demo.Counter");
    ASSERT_NE(legacyCounterCh, nullptr);
    ASSERT_NE(equivCounterCh, nullptr);
    auto const* legacyCounter = std::get_if<osf::TimestampedChannel>(legacyCounterCh);
    auto const* equivCounter = std::get_if<osf::TimestampedChannel>(equivCounterCh);
    ASSERT_NE(legacyCounter, nullptr);
    ASSERT_NE(equivCounter, nullptr);
    EXPECT_EQ(legacyCounter->timestampsNs.size(), equivCounter->timestampsNs.size());
    EXPECT_EQ(legacyCounter->timestampsNs, equivCounter->timestampsNs);

    auto legacyCounterR = osf::asUint32Flat(*legacyCounter);
    ASSERT_TRUE(legacyCounterR.has_value()) << "Demo.Counter is uint32";
    auto equivCounterR = osf::asUint32Flat(*equivCounter);
    ASSERT_TRUE(equivCounterR.has_value()) << "Demo.Counter is uint32";

    std::vector<std::uint32_t> legacyCounterValues;
    for (auto const& [ts, v] : *legacyCounterR) {
        static_cast<void>(ts);
        legacyCounterValues.push_back(v);
    }
    std::vector<std::uint32_t> equivCounterValues;
    for (auto const& [ts, v] : *equivCounterR) {
        static_cast<void>(ts);
        equivCounterValues.push_back(v);
    }
    EXPECT_EQ(legacyCounterValues, equivCounterValues);
    EXPECT_EQ(legacyCounterValues, COUNTER_VALUES);

    // Demo.Message: same sample count, same timestamp sequence, same
    // decoded values.
    auto const* legacyMessageCh = legacy->channel("Demo.Message");
    auto const* equivMessageCh = equivalent->channel("Demo.Message");
    ASSERT_NE(legacyMessageCh, nullptr);
    ASSERT_NE(equivMessageCh, nullptr);
    auto const* legacyMessage = std::get_if<osf::VariableChannel>(legacyMessageCh);
    auto const* equivMessage = std::get_if<osf::VariableChannel>(equivMessageCh);
    ASSERT_NE(legacyMessage, nullptr);
    ASSERT_NE(equivMessage, nullptr);
    EXPECT_EQ(legacyMessage->timestampsNs.size(), equivMessage->timestampsNs.size());
    EXPECT_EQ(legacyMessage->timestampsNs, equivMessage->timestampsNs);

    auto legacyTextsR = legacyMessage->asStrings();
    ASSERT_TRUE(legacyTextsR.has_value()) << "Demo.Message is string";
    auto equivTextsR = equivMessage->asStrings();
    ASSERT_TRUE(equivTextsR.has_value()) << "Demo.Message is string";
    EXPECT_EQ(**legacyTextsR, **equivTextsR)
        << "Demo.Message must decode to identical values from both encodings";
    EXPECT_EQ(**legacyTextsR, messageTexts());
}
