// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// OSF-UP3 — zero-length data blocks.
//
// A data block whose length field reads 0 is a non-conforming writer artefact:
// a conforming block always carries at least its control byte. Readers MUST NOT
// treat it as truncation and MUST NOT abort — they skip the block (the frame is
// only the channel index and the length field, both already consumed), count it
// under the canonical reason ZeroLengthBlock, and keep scanning.
//
// The fixture drives the shared corpus file
// examples/generated/malformed/osf5_zero_length_block.osf — one Sensor/Double
// channel, ten samples, with a four-byte zero-length frame sitting between the
// fifth and the sixth sample — from both surfaces: the high-level data manager
// (all ten samples must survive) and the low-level filer (two well-formed
// blocks delivered, one zero-length block counted, no truncation reported).
unit Test.OSF.Filer.ZeroLengthBlock;

interface

uses
  System.SysUtils,
  OSF.Types,
  DUnitX.TestFramework;

type
  [TestFixture]
  TFilerZeroLengthBlockTests = class
  strict private
    function ExamplesDir: string;
    function CorpusFile: string;
  public
    [Test] procedure ManagerReadsAllSamplesAcrossZeroLengthBlock;
    [Test] procedure FilerSkipsAndCountsZeroLengthBlock;
    [Test] procedure CounterIsIndependentOfTheChannelFilter;
  end;

implementation

uses
  System.IOUtils,
  OSF.Data.Channels,
  OSF.Data.Manager,
  OSF.Filer;

function TFilerZeroLengthBlockTests.ExamplesDir: string;
begin
  Result := TPath.GetFullPath(TPath.Combine(ExtractFilePath(ParamStr(0)),
    '..\..\..\examples'));
end;

function TFilerZeroLengthBlockTests.CorpusFile: string;
begin
  Result := TPath.Combine(ExamplesDir, 'generated\malformed\osf5_zero_length_block.osf');
end;

// High level: the samples on both sides of the bad frame must be delivered.
// A reader that stopped at the frame would report 5; one that aborts raises.
procedure TFilerZeroLengthBlockTests.ManagerReadsAllSamplesAcrossZeroLengthBlock;
var
  Mgr: TOSFDataManager;
  Ch: TOSFDataChannel;
begin
  Assert.IsTrue(TFile.Exists(CorpusFile), 'corpus file present: ' + CorpusFile);
  Mgr := TOSFDataManager.Create;
  try
    Mgr.LoadFromFile(CorpusFile);
    Assert.AreEqual(1, Mgr.ChannelCount, 'channel count');
    Ch := Mgr.ChannelByIndex(0);
    Assert.IsNotNull(Ch, 'channel 0');
    Assert.AreEqual('Sensor/Double', Ch.Name, 'channel name');
    Assert.AreEqual('double', OSFDataTypeToString(Ch.OriginalDataType), 'dataType');
    Assert.AreEqual(10, Ch.SampleCount,
      'all 10 samples must survive the zero-length block (5 before + 5 after)');
  finally
    Mgr.Free;
  end;
end;

// Low level: the bad frame is consumed and skipped, never delivered, counted
// under its own counter, and must not be mistaken for a truncation.
procedure TFilerZeroLengthBlockTests.FilerSkipsAndCountsZeroLengthBlock;
var
  F: TOSFFile;
  Block: TOSFDataBlock;
  Blocks, Samples: Integer;
begin
  Assert.IsTrue(TFile.Exists(CorpusFile), 'corpus file present: ' + CorpusFile);
  Blocks := 0;
  Samples := 0;
  F := TOSFFile.Create;
  try
    F.OpenForRead(CorpusFile);
    while F.ReadNextBlock(Block) do
      if not Block.IsInfoBlock then
      begin
        Inc(Blocks);
        Inc(Samples, Block.SampleCount);
      end;
    Assert.AreEqual(2, Blocks, 'only the 2 well-formed blocks are delivered');
    Assert.AreEqual(10, Samples, 'samples across both delivered blocks');
    Assert.AreEqual(UInt32(1), F.BlocksZeroLengthSkipped, 'zero-length blocks skipped');
    Assert.IsFalse(F.TruncationSeen,
      'a zero-length block must not be reported as truncation');
  finally
    F.Free;
  end;
end;

// The anomaly belongs to the file, not to the caller's channel selection: with
// a ChannelFilter active the bad frame is consumed by the filter's skip path
// instead of ReadDataBlock, and must still be logged and counted. Otherwise a
// diagnostic run - which is exactly when a filter tends to be set - would
// report a clean file. The filter here excludes every channel, so no block is
// delivered at all and the counter is the only surface the anomaly reaches.
procedure TFilerZeroLengthBlockTests.CounterIsIndependentOfTheChannelFilter;
var
  F: TOSFFile;
  Block: TOSFDataBlock;
  Blocks: Integer;
begin
  Assert.IsTrue(TFile.Exists(CorpusFile), 'corpus file present: ' + CorpusFile);
  Blocks := 0;
  F := TOSFFile.Create;
  try
    F.OpenForRead(CorpusFile);
    F.ChannelFilter := ['Not/AChannelInThisFile'];
    Assert.IsFalse(F.IsChannelIncluded(0), 'the only channel must be filtered out');
    while F.ReadNextBlock(Block) do
      if not Block.IsInfoBlock then
        Inc(Blocks);
    Assert.AreEqual(0, Blocks, 'every block is filtered out');
    Assert.AreEqual(UInt32(1), F.BlocksZeroLengthSkipped,
      'the zero-length block must be counted on the filtered path too');
    Assert.IsFalse(F.TruncationSeen,
      'a zero-length block must not be reported as truncation');
  finally
    F.Free;
  end;
end;

initialization
  TDUnitX.RegisterTestFixture(TFilerZeroLengthBlockTests);

end.
