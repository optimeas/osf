// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// OSF-UP4 — bcMessageEvent (control byte 4) is read-mandatory.
//
// Deployed device firmware writes OSF4 `string` channels as bcMessageEvent.
// The codec here has always delivered those blocks, but the data manager
// dispatched on bcAbsTimeStampData alone, so the samples never reached a
// channel: the channel arrived empty, with no error and no statistic
// (DECISIONS §26).
//
// Wire format (per block, one sample):
//   [u16 channel index][length field][ctrl 0x04][int64 ts][uint32 N][N bytes]
// with length = 1 + 8 + 4 + N.
//
// THE TRAP this fixture guards: OSF4's "a variable-length payload carries a
// trailing 0x00" rule governs bcAbsTimeStampData ONLY. A bcMessageEvent
// payload is length-prefixed and ends exactly after N bytes. A reader that
// routes control byte 4 through the bcAbsTimeStampData path inherits the
// terminator strip and silently loses the last character of every value —
// and four of the five corpus samples would still decode to *something*, so
// it would not fail loudly. Every high-level test below therefore pins a
// value's LAST character, not just its prefix.
//
// The fixture drives the shared corpus pair
//   examples/generated/osf4_message_event_string.osf            (byte 4)
//   examples/generated/osf4_message_event_string_equivalent.osf (byte 8)
// which carry identical channel content in the two encodings. Their block
// ordering deliberately differs (channel-major vs. round-robin), so nothing
// here may assume matching block order across the two files.
unit Test.OSF.Filer.MessageEvent;

interface

uses
  System.SysUtils,
  System.Classes,
  OSF.Types,
  DUnitX.TestFramework;

type
  [TestFixture]
  TFilerMessageEventTests = class
  strict private
    function ExamplesDir: string;
    function LegacyFile: string;
    function EquivalentFile: string;
    // Expected Demo.Message sample texts, in stream order. Index 3 is spelled
    // out by code point so the fixture does not depend on the encoding this
    // source file happens to be saved in.
    function ExpectedMessage(Index: Integer): string;
    // Builds a synthetic single-channel OSF4 file in memory: a real header
    // written by the writer, followed verbatim by ABlocks. The data blocks
    // are assembled by hand because no writer in this repository emits
    // control byte 3 or 4 - and per DECISIONS §26 none ever should.
    function BuildSyntheticFile(ADataType: TOSFDataType;
      ALFS: TOSFLengthFieldSize; const ABlocks: TBytes): TBytesStream;
    // [u16 channel 0][length field][CtrlByte][int64 ts][uint32 N][N bytes]
    function MakeMessageEventBlock(ALFS: TOSFLengthFieldSize; CtrlByte: Byte;
      TimestampNs: Int64; const Payload: TBytes): TBytes;
    // [u16 channel 0][length field][0x03][int64 ts][uint32 status word]
    function MakeStatusEventBlock(ALFS: TOSFLengthFieldSize;
      TimestampNs: Int64; StatusWord: UInt32): TBytes;
  public
    [Test] procedure ManagerDecodesLegacyMessageEventChannel;
    [Test] procedure LegacyAndEquivalentDecodeIdentically;
    [Test] procedure FilerSurfacesTimestampAndPayloadOfMessageEventBlocks;
    [Test] procedure ZeroLengthMessagePayloadDecodesToAnEmptyValue;
    [Test] procedure UnspecifiedShapesAndStatusEventsAreSkippedAndCounted;
    [Test] procedure MetaCacheAgreesWithTheDataManager;
  end;

const
  MESSAGE_SAMPLE_COUNT = 5;
  BASE_TIMESTAMP_NS: Int64 = 1768478400000000000;
  TIMESTAMP_STEP_NS: Int64 = 5000000000;
  COUNTER_VALUES: array[0..4] of UInt32 = (10, 20, 30, 40, 50);
  // The fifth message is 300 × 'A' — long enough that the channel needs
  // sizeoflengthvalue=4, and long enough that an off-by-one terminator strip
  // is visible as a length, not only as a character.
  LONG_MESSAGE_LEN = 300;

implementation

uses
  System.IOUtils,
  OSF.Channel,
  OSF.Data.Channels,
  OSF.Data.Manager,
  OSF.Meta.Cache,
  OSF.Filer;

function TFilerMessageEventTests.ExamplesDir: string;
begin
  Result := TPath.GetFullPath(TPath.Combine(ExtractFilePath(ParamStr(0)),
    '..\..\..\examples'));
end;

function TFilerMessageEventTests.LegacyFile: string;
begin
  Result := TPath.Combine(ExamplesDir, 'generated\osf4_message_event_string.osf');
end;

function TFilerMessageEventTests.EquivalentFile: string;
begin
  Result := TPath.Combine(ExamplesDir,
    'generated\osf4_message_event_string_equivalent.osf');
end;

function TFilerMessageEventTests.ExpectedMessage(Index: Integer): string;
begin
  case Index of
    0: Result := 'OSF-DEMO-0001';
    1: Result := 'no signal';
    2: Result := '';                                // N = 0 on the wire
    // "Gruesse aus Saeckingen <check mark>" with every non-ASCII letter
    // written as a code point, so the expected value cannot be corrupted by
    // whatever encoding this source file is saved in:
    // U+00FC u-umlaut, U+00DF sharp s, U+00E4 a-umlaut, U+2713 check mark.
    3: Result := 'Gr' + Char($00FC) + Char($00DF) + 'e aus S' + Char($00E4) +
                 'ckingen ' + Char($2713);
    4: Result := StringOfChar('A', LONG_MESSAGE_LEN);
  else
    Result := '';
  end;
end;

// Writes the frame header ([u16 channel index][length field]) plus a control
// byte and returns the stream the body still has to be written to.
procedure WriteFrameHeader(MS: TStream; ALFS: TOSFLengthFieldSize;
  BodyLen: UInt32; CtrlByte: Byte);
var
  ChannelIndex: Word;
  Len16: Word;
  Len32: UInt32;
begin
  ChannelIndex := 0;
  MS.WriteBuffer(ChannelIndex, SizeOf(ChannelIndex));
  if ALFS = lfs2 then
  begin
    Len16 := Word(1 + BodyLen); // control byte + body
    MS.WriteBuffer(Len16, SizeOf(Len16));
  end
  else
  begin
    Len32 := 1 + BodyLen;
    MS.WriteBuffer(Len32, SizeOf(Len32));
  end;
  MS.WriteBuffer(CtrlByte, SizeOf(CtrlByte));
end;

function TFilerMessageEventTests.MakeMessageEventBlock(ALFS: TOSFLengthFieldSize;
  CtrlByte: Byte; TimestampNs: Int64; const Payload: TBytes): TBytes;
var
  MS: TBytesStream;
  PayloadLen: UInt32;
begin
  PayloadLen := Length(Payload);
  MS := TBytesStream.Create;
  try
    WriteFrameHeader(MS, ALFS, 8 + 4 + PayloadLen, CtrlByte);
    MS.WriteBuffer(TimestampNs, SizeOf(TimestampNs));
    MS.WriteBuffer(PayloadLen, SizeOf(PayloadLen));
    if PayloadLen > 0 then
      MS.WriteBuffer(Payload[0], PayloadLen);
    Result := Copy(MS.Bytes, 0, MS.Size);
  finally
    MS.Free;
  end;
end;

function TFilerMessageEventTests.MakeStatusEventBlock(ALFS: TOSFLengthFieldSize;
  TimestampNs: Int64; StatusWord: UInt32): TBytes;
var
  MS: TBytesStream;
begin
  MS := TBytesStream.Create;
  try
    WriteFrameHeader(MS, ALFS, 8 + 4, 3); // bcStatusEvent
    MS.WriteBuffer(TimestampNs, SizeOf(TimestampNs));
    MS.WriteBuffer(StatusWord, SizeOf(StatusWord));
    Result := Copy(MS.Bytes, 0, MS.Size);
  finally
    MS.Free;
  end;
end;

function TFilerMessageEventTests.BuildSyntheticFile(ADataType: TOSFDataType;
  ALFS: TOSFLengthFieldSize; const ABlocks: TBytes): TBytesStream;
var
  F: TOSFFile;
  Def: TOSFChannelDef;
begin
  Result := TBytesStream.Create;
  try
    F := TOSFFile.Create;
    try
      F.CreateForWrite(Result, False, osvOSF4);
      Def := TOSFChannelDef.Create(0, 'Demo.Message', ctScalar, ADataType);
      Def.LengthFieldSize := ALFS;
      F.AddChannel(Def);
      F.WriteHeader;
      F.Close;
    finally
      F.Free;
    end;
    if Length(ABlocks) > 0 then
      Result.WriteBuffer(ABlocks[0], Length(ABlocks));
    Result.Position := 0;
  except
    Result.Free;
    raise;
  end;
end;

// High level: the whole point of OSF-UP4. Before the fix Demo.Message
// arrived with zero samples. The last-character assertions are the
// terminator guard - see the trap note at the top of this unit.
procedure TFilerMessageEventTests.ManagerDecodesLegacyMessageEventChannel;
var
  Mgr: TOSFDataManager;
  Msg, Counter: TOSFDataChannel;
  I: Integer;
  S: string;
begin
  Assert.IsTrue(TFile.Exists(LegacyFile), 'corpus file present: ' + LegacyFile);
  Mgr := TOSFDataManager.Create;
  try
    Mgr.LoadFromFile(LegacyFile);
    Assert.AreEqual(2, Mgr.ChannelCount, 'channel count');

    Msg := Mgr.ChannelByName('Demo.Message');
    Assert.IsNotNull(Msg, 'Demo.Message present');
    Assert.AreEqual('string', OSFDataTypeToString(Msg.OriginalDataType),
      'Demo.Message datatype');
    Assert.AreEqual(MESSAGE_SAMPLE_COUNT, Msg.SampleCount,
      'every bcMessageEvent block must become one sample');

    for I := 0 to MESSAGE_SAMPLE_COUNT - 1 do
    begin
      Assert.AreEqual(ExpectedMessage(I), Msg.ValueAsString(I),
        Format('Demo.Message value %d', [I]));
      Assert.AreEqual(BASE_TIMESTAMP_NS + I * TIMESTAMP_STEP_NS,
        Msg.TimestampNsAt(I), Format('Demo.Message timestamp %d', [I]));
    end;

    // Terminator guard, stated as its own assertions so a regression names
    // itself: a strip would turn '...0001' into '...000', cut the last byte
    // off the UTF-8 check mark, and shorten the 300-byte sample to 299.
    S := Msg.ValueAsString(0);
    Assert.AreEqual('1', string(S[Length(S)]),
      'last character of sample 0 - a trailing-byte strip would drop it');
    S := Msg.ValueAsString(3);
    Assert.AreEqual(string(Char($2713)), string(S[Length(S)]),
      'last character of sample 3 - a strip would break the UTF-8 check mark');
    Assert.AreEqual(LONG_MESSAGE_LEN, Length(Msg.ValueAsString(4)),
      'length of sample 4 - a strip would report 299');
    Assert.AreEqual('', Msg.ValueAsString(2),
      'sample 2 carries N = 0 and decodes to the empty string');

    Counter := Mgr.ChannelByName('Demo.Counter');
    Assert.IsNotNull(Counter, 'Demo.Counter present');
    Assert.AreEqual(MESSAGE_SAMPLE_COUNT, Counter.SampleCount,
      'Demo.Counter sample count');
    for I := 0 to MESSAGE_SAMPLE_COUNT - 1 do
      Assert.AreEqual(Double(COUNTER_VALUES[I]), Counter.ValueAsDouble(I), 0.0,
        Format('Demo.Counter value %d', [I]));
  finally
    Mgr.Free;
  end;
end;

// The two files carry the same channel content in the two encodings and must
// decode to the same result. Compared by decoded content only - the block
// ordering on disk differs deliberately (channel-major vs. round-robin).
procedure TFilerMessageEventTests.LegacyAndEquivalentDecodeIdentically;
var
  Legacy, Equivalent: TOSFDataManager;
  LMsg, EMsg, LCnt, ECnt: TOSFDataChannel;
  I: Integer;
begin
  Assert.IsTrue(TFile.Exists(LegacyFile), 'corpus file present: ' + LegacyFile);
  Assert.IsTrue(TFile.Exists(EquivalentFile),
    'corpus file present: ' + EquivalentFile);
  Legacy := TOSFDataManager.Create;
  try
    Equivalent := TOSFDataManager.Create;
    try
      Legacy.LoadFromFile(LegacyFile);
      Equivalent.LoadFromFile(EquivalentFile);
      Assert.AreEqual(Legacy.ChannelCount, Equivalent.ChannelCount,
        'channel counts must match');

      LMsg := Legacy.ChannelByName('Demo.Message');
      EMsg := Equivalent.ChannelByName('Demo.Message');
      Assert.IsNotNull(LMsg, 'Demo.Message in the bcMessageEvent file');
      Assert.IsNotNull(EMsg, 'Demo.Message in the bcAbsTimeStampData file');
      Assert.AreEqual(EMsg.SampleCount, LMsg.SampleCount,
        'Demo.Message sample counts must match');
      for I := 0 to EMsg.SampleCount - 1 do
      begin
        Assert.AreEqual(EMsg.ValueAsString(I), LMsg.ValueAsString(I),
          Format('Demo.Message value %d must match across encodings', [I]));
        Assert.AreEqual(EMsg.TimestampNsAt(I), LMsg.TimestampNsAt(I),
          Format('Demo.Message timestamp %d must match across encodings', [I]));
      end;

      LCnt := Legacy.ChannelByName('Demo.Counter');
      ECnt := Equivalent.ChannelByName('Demo.Counter');
      Assert.IsNotNull(LCnt, 'Demo.Counter in the bcMessageEvent file');
      Assert.IsNotNull(ECnt, 'Demo.Counter in the bcAbsTimeStampData file');
      Assert.AreEqual(ECnt.SampleCount, LCnt.SampleCount,
        'Demo.Counter sample counts must match');
      for I := 0 to ECnt.SampleCount - 1 do
      begin
        Assert.AreEqual(ECnt.ValueAsDouble(I), LCnt.ValueAsDouble(I), 0.0,
          Format('Demo.Counter value %d must match across encodings', [I]));
        Assert.AreEqual(ECnt.TimestampNsAt(I), LCnt.TimestampNsAt(I),
          Format('Demo.Counter timestamp %d must match across encodings', [I]));
      end;
    finally
      Equivalent.Free;
    end;
  finally
    Legacy.Free;
  end;
end;

// Low level: a consumer draining ReadNextBlock must get the message-event
// timestamp and the payload without parsing the wire format itself. The
// codec owns the [int64 ts][uint32 N][N bytes] framing - if the block handed
// the raw frame to the caller instead, every downstream consumer would have
// to re-derive a layout whose length prefix the specification itself only
// documented as of OSF-UP4.
//
// Blocks are collected in stream order and filtered by block type, so the
// test does not assume where the message-event blocks sit in the file.
procedure TFilerMessageEventTests.FilerSurfacesTimestampAndPayloadOfMessageEventBlocks;
var
  F: TOSFFile;
  Block: TOSFDataBlock;
  Timestamps: TArray<Int64>;
  Payloads: TArray<TBytes>;
  Total, N, I: Integer;
  S: string;
begin
  Assert.IsTrue(TFile.Exists(LegacyFile), 'corpus file present: ' + LegacyFile);
  Total := 0;
  N := 0;
  SetLength(Timestamps, MESSAGE_SAMPLE_COUNT);
  SetLength(Payloads, MESSAGE_SAMPLE_COUNT);
  F := TOSFFile.Create;
  try
    F.OpenForRead(LegacyFile);
    while F.ReadNextBlock(Block) do
      if not Block.IsInfoBlock then
      begin
        Inc(Total);
        if Block.BlockType = bcMessageEvent then
        begin
          Assert.IsTrue(N < MESSAGE_SAMPLE_COUNT,
            'no more than 5 bcMessageEvent blocks are expected');
          Assert.AreEqual(UInt32(1), Block.SampleCount,
            'a bcMessageEvent block carries exactly one sample');
          Timestamps[N] := Block.StartTimestampNs;
          Payloads[N] := Block.RawPayload;
          Inc(N);
        end;
      end;
    Assert.IsFalse(F.TruncationSeen, 'the corpus file is complete');
    Assert.AreEqual(10, Total, '5 counter blocks + 5 message blocks');
    Assert.AreEqual(MESSAGE_SAMPLE_COUNT, N, 'bcMessageEvent blocks delivered');

    for I := 0 to MESSAGE_SAMPLE_COUNT - 1 do
    begin
      Assert.AreEqual(BASE_TIMESTAMP_NS + I * TIMESTAMP_STEP_NS, Timestamps[I],
        Format('block %d must surface a usable timestamp', [I]));
      S := TEncoding.UTF8.GetString(Payloads[I]);
      Assert.AreEqual(ExpectedMessage(I), S,
        Format('block %d must surface the decoded payload', [I]));
    end;
    // The payload ends exactly after N bytes: no terminator to strip, and
    // none present.
    // Length() on a dynamic array is NativeInt, so the casts keep the
    // comparison unambiguous under Win64 as well as Win32.
    Assert.AreEqual(LONG_MESSAGE_LEN, Integer(Length(Payloads[4])),
      'payload length of the 300-byte sample');
    Assert.AreEqual(Ord('1'), Integer(Payloads[0][High(Payloads[0])]),
      'last payload byte of sample 0');
    Assert.AreEqual(0, Integer(Length(Payloads[2])),
      'sample 2 declares N = 0 and surfaces an empty payload');
  finally
    F.Free;
  end;
end;

// N = 0 is legal and decodes to an empty value - explicitly stated by the
// specification, and not the zero-length-block anomaly of OSF-UP3: there the
// block's length field itself is 0 and no control byte is ever read, here the
// length field reflects the true frame size and only the payload is empty.
// Synthetic, because the corpus pair's own empty sample sits in the middle of
// a file and this contract deserves to fail on its own.
procedure TFilerMessageEventTests.ZeroLengthMessagePayloadDecodesToAnEmptyValue;
var
  MS: TBytesStream;
  Mgr: TOSFDataManager;
  Ch: TOSFDataChannel;
  Empty: TBytes;
begin
  SetLength(Empty, 0);
  MS := BuildSyntheticFile(dtString, lfs4,
    MakeMessageEventBlock(lfs4, 4, BASE_TIMESTAMP_NS, Empty));
  try
    Mgr := TOSFDataManager.Create;
    try
      Mgr.LoadFromStream(MS);
      Assert.AreEqual(1, Mgr.ChannelCount, 'channel count');
      Ch := Mgr.ChannelByIndex(0);
      Assert.IsNotNull(Ch, 'channel 0');
      Assert.AreEqual(1, Ch.SampleCount,
        'N = 0 is a legal sample, not a dropped block');
      Assert.AreEqual('', Ch.ValueAsString(0), 'the value is the empty string');
      Assert.AreEqual(BASE_TIMESTAMP_NS, Ch.TimestampNsAt(0),
        'the timestamp is still carried');
    finally
      Mgr.Free;
    end;
  finally
    MS.Free;
  end;
end;

// The three shapes that must never become a sample and must never raise:
//   - bcStatusEvent (control byte 3): a fixed status word, not a value of the
//     channel's datatype. Skipped under a counter of its own so an occurrence
//     stays visible instead of folding into a generic bucket.
//   - bcMessageEvent with bit 7 set: no multi-sample layout is specified for
//     this block type, so it is treated as an unrecognised shape.
//   - bcMessageEvent on a datatype other than string/binary: outside the
//     block type's defined scope.
// All three are skipped via the length field, counted, and the scan continues
// - a following well-formed block must still be delivered.
procedure TFilerMessageEventTests.UnspecifiedShapesAndStatusEventsAreSkippedAndCounted;
var
  MS: TBytesStream;
  F: TOSFFile;
  Block: TOSFDataBlock;
  Blocks: Integer;
  Stream: TBytes;
  Good: TBytes;
begin
  Good := TEncoding.UTF8.GetBytes('kept');
  // string channel: status event, then a bit-7 message event, then a valid one.
  Stream := MakeStatusEventBlock(lfs4, BASE_TIMESTAMP_NS, 7);
  Stream := Stream + MakeMessageEventBlock(lfs4, $84, // bcMessageEvent | bit 7
    BASE_TIMESTAMP_NS + TIMESTAMP_STEP_NS, Good);
  Stream := Stream + MakeMessageEventBlock(lfs4, 4,
    BASE_TIMESTAMP_NS + 2 * TIMESTAMP_STEP_NS, Good);

  MS := BuildSyntheticFile(dtString, lfs4, Stream);
  try
    Blocks := 0;
    F := TOSFFile.Create;
    try
      F.OpenForRead(MS);
      while F.ReadNextBlock(Block) do
        if not Block.IsInfoBlock then
        begin
          Inc(Blocks);
          Assert.AreEqual('kept', TEncoding.UTF8.GetString(Block.RawPayload),
            'only the well-formed block is delivered');
        end;
      Assert.AreEqual(1, Blocks, 'the two unusable blocks must not be delivered');
      Assert.AreEqual(UInt32(1), F.BlocksStatusEventSkipped,
        'bcStatusEvent needs a counter of its own');
      Assert.AreEqual(UInt32(1), F.BlocksUnknownTypeSkipped,
        'a bcMessageEvent with bit 7 set is counted, never guessed at');
      Assert.IsFalse(F.TruncationSeen, 'neither shape is a truncation');
    finally
      F.Free;
    end;
  finally
    MS.Free;
  end;

  // double channel: bcMessageEvent is not defined for it - skipped and
  // counted, and explicitly NOT an error that fails the file.
  MS := BuildSyntheticFile(dtDouble, lfs2,
    MakeMessageEventBlock(lfs2, 4, BASE_TIMESTAMP_NS, Good));
  try
    Blocks := 0;
    F := TOSFFile.Create;
    try
      F.OpenForRead(MS);
      while F.ReadNextBlock(Block) do
        if not Block.IsInfoBlock then
          Inc(Blocks);
      Assert.AreEqual(0, Blocks, 'an undefined datatype yields no sample');
      Assert.AreEqual(UInt32(1), F.BlocksUnknownTypeSkipped,
        'an undefined datatype is counted');
      Assert.AreEqual(UInt32(0), F.BlocksStatusEventSkipped,
        'and does not touch the status-event counter');
      Assert.IsFalse(F.TruncationSeen, 'the file stays readable');
    finally
      F.Free;
    end;
  finally
    MS.Free;
  end;
end;

// The meta cache is a third surface onto the same file - osftool's info,
// channels and cache commands are served from it - and it has its own block
// dispatch. If that dispatch does not know bcMessageEvent, the tool
// contradicts itself: export decodes five Demo.Message samples while
// cache-backed info reports zero, and a saved sidecar makes the wrong answer
// persist across runs. The contract is therefore an equality: whatever the
// cache records must equal what TOSFDataManager reports for the same file.
//
// Built with BuildFromFile, which scans the OSF file and returns the cache in
// memory - it neither reads nor writes a .json sidecar (that is EnsureCache's
// job), so no stale sidecar from an earlier run can be mistaken for a pass.
//
// Both sides are also pinned against the absolute expectation, so the test
// still fails if cache and manager ever agree on the same wrong answer.
procedure TFilerMessageEventTests.MetaCacheAgreesWithTheDataManager;
var
  Builder: TOSFMetaCacheBuilder;
  Cache: TOSFMetaCache;
  Mgr: TOSFDataManager;
  MsgChan, CntChan: TOSFDataChannel;
  CachedMsg, CachedCnt: TOSFCacheChannel;
  LastTs: Int64;
begin
  Assert.IsTrue(TFile.Exists(LegacyFile), 'corpus file present: ' + LegacyFile);
  LastTs := BASE_TIMESTAMP_NS + (MESSAGE_SAMPLE_COUNT - 1) * TIMESTAMP_STEP_NS;

  Mgr := TOSFDataManager.Create;
  try
    Mgr.LoadFromFile(LegacyFile);
    MsgChan := Mgr.ChannelByName('Demo.Message');
    CntChan := Mgr.ChannelByName('Demo.Counter');
    Assert.IsNotNull(MsgChan, 'Demo.Message decoded by the manager');
    Assert.IsNotNull(CntChan, 'Demo.Counter decoded by the manager');

    Builder := TOSFMetaCacheBuilder.Create;
    try
      Cache := Builder.BuildFromFile(LegacyFile);
      try
        Assert.IsTrue(Cache.HasChannel('Demo.Message'),
          'Demo.Message present in the cache');
        CachedMsg := Cache.ChannelByName('Demo.Message');

        Assert.AreEqual(Int64(MESSAGE_SAMPLE_COUNT), CachedMsg.SampleCount,
          'the cache must count bcMessageEvent blocks as samples');
        Assert.AreEqual(Int64(MsgChan.SampleCount), CachedMsg.SampleCount,
          'cache and data manager must report the same sample count');
        Assert.AreEqual(BASE_TIMESTAMP_NS, CachedMsg.FirstTimestampNs,
          'cached first timestamp');
        Assert.AreEqual(MsgChan.StartTimestampNs, CachedMsg.FirstTimestampNs,
          'cache and data manager must report the same first timestamp');
        Assert.AreEqual(LastTs, CachedMsg.LastTimestampNs,
          'cached last timestamp');
        Assert.AreEqual(MsgChan.EndTimestampNs, CachedMsg.LastTimestampNs,
          'cache and data manager must report the same last timestamp');

        // The untouched channel of the same file, as a control: the arm added
        // for bcMessageEvent must not have disturbed the bcAbsTimeStampData
        // accounting next to it.
        Assert.IsTrue(Cache.HasChannel('Demo.Counter'),
          'Demo.Counter present in the cache');
        CachedCnt := Cache.ChannelByName('Demo.Counter');
        Assert.AreEqual(Int64(CntChan.SampleCount), CachedCnt.SampleCount,
          'Demo.Counter sample count must still agree');
        Assert.AreEqual(CntChan.StartTimestampNs, CachedCnt.FirstTimestampNs,
          'Demo.Counter first timestamp must still agree');
        Assert.AreEqual(CntChan.EndTimestampNs, CachedCnt.LastTimestampNs,
          'Demo.Counter last timestamp must still agree');

        // The file-wide range is derived from the per-channel ranges, so it
        // would silently shrink if a channel contributed nothing.
        Assert.AreEqual(BASE_TIMESTAMP_NS, Cache.FirstTimestampNs,
          'file-wide first timestamp');
        Assert.AreEqual(LastTs, Cache.LastTimestampNs,
          'file-wide last timestamp');
        Assert.IsFalse(Cache.Truncated, 'the corpus file is complete');
      finally
        Cache.Free;
      end;
    finally
      Builder.Free;
    end;
  finally
    Mgr.Free;
  end;
end;

initialization
  TDUnitX.RegisterTestFixture(TFilerMessageEventTests);

end.
