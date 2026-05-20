// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Standalone smoke test for the HDF5 DLL binding (Hdf5.Types / Hdf5.Api /
// Hdf5.Wrapper). Run before relying on the binding: it loads hdf5.dll,
// checks the library version and the _g globals, then writes a real
// compound dataset with chunking, shuffle and deflate into a temp .h5 file
// and confirms the result carries the HDF5 file signature.
//
// Exit code 0 = all checks passed, 1 = at least one check failed.
program Hdf5SmokeTest;

{$APPTYPE CONSOLE}

uses
  System.SysUtils,
  System.IOUtils,
  System.Classes,
  Hdf5.Types  in '..\Hdf5.Types.pas',
  Hdf5.Api    in '..\Hdf5.Api.pas',
  Hdf5.Wrapper in '..\Hdf5.Wrapper.pas';

type
  // Compound record {int64 timestamp_ns; double value} — must be packed so
  // the field offsets match the H5Tinsert offsets (0 and 8).
  TSampleRec = packed record
    TimestampNs: Int64;
    Value: Double;
  end;

var
  Failures: Integer = 0;

procedure Check(Condition: Boolean; const Description: string);
begin
  if Condition then
    Writeln('  ok    ', Description)
  else
  begin
    Writeln('  FAIL  ', Description);
    Inc(Failures);
  end;
end;

// Confirms File starts with the 8-byte HDF5 superblock signature.
function HasHdf5Signature(const FileName: string): Boolean;
const
  HDF5_SIGNATURE: array[0..7] of Byte = ($89, $48, $44, $46, $0D, $0A, $1A, $0A);
var
  Stream: TFileStream;
  Head: array[0..7] of Byte;
  I: Integer;
begin
  Result := False;
  Stream := TFileStream.Create(FileName, fmOpenRead or fmShareDenyWrite);
  try
    if Stream.Size < Length(Head) then
      Exit;
    Stream.ReadBuffer(Head, Length(Head));
  finally
    Stream.Free;
  end;
  for I := 0 to High(Head) do
    if Head[I] <> HDF5_SIGNATURE[I] then
      Exit;
  Result := True;
end;

procedure WriteSampleFile(const FileName: string);
var
  H5File: THdf5File;
  CompoundType: THdf5Datatype;
  Space: THdf5Dataspace;
  Dcpl, Lcpl: THdf5PropertyList;
  Dataset: THdf5Dataset;
  Group: THdf5Group;
  Batch: array of TSampleRec;
  I: Integer;
begin
  H5File := THdf5File.Create(UTF8String(FileName), H5F_ACC_TRUNC);
  try
    // Compound storage type {timestamp_ns: i64; value: f64}.
    CompoundType := THdf5Datatype.CreateCompound(SizeOf(TSampleRec));
    Space := THdf5Dataspace.CreateUnlimited1D(0);
    Dcpl := THdf5PropertyList.Create(H5P_CLS_DATASET_CREATE_ID);
    Lcpl := THdf5PropertyList.Create(H5P_CLS_LINK_CREATE_ID);
    try
      CompoundType.Insert('timestamp_ns', 0, H5T_STD_I64LE);
      CompoundType.Insert('value', 8, H5T_IEEE_F64LE);

      Dcpl.SetChunk(4);
      Dcpl.SetShuffle;
      Dcpl.SetDeflate(4);

      Lcpl.SetCharEncoding(H5T_CSET_UTF8);
      Lcpl.SetCreateIntermediateGroup(True);

      // Dataset under an auto-created intermediate group, UTF-8 path.
      Dataset := THdf5Dataset.Create(H5File.Handle, 'Motor/Messstelle',
        CompoundType.Handle, Space.Handle, Dcpl.Handle, Lcpl.Handle);
      try
        SetLength(Batch, 10);
        for I := 0 to High(Batch) do
        begin
          Batch[I].TimestampNs := Int64(1700000000000000000) + I * 1000000;
          Batch[I].Value := 20.0 + I * 0.25;
        end;
        // Two appends exercise the extend / hyperslab path across chunks.
        Dataset.AppendChunk(@Batch[0], 6, CompoundType.Handle);
        Dataset.AppendChunk(@Batch[6], 4, CompoundType.Handle);

        Check(Dataset.RowCount = 10, 'dataset row count is 10 after two appends');

        // Attributes, including a UTF-8 string with non-ASCII content.
        THdf5Attribute.WriteUtf8String(Dataset.Handle, 'units', UTF8String('°C'));
        THdf5Attribute.WriteInt64(Dataset.Handle, 'sample_period_ns', 1000000);
        THdf5Attribute.WriteDouble(Dataset.Handle, 'scale', 1.0);
        THdf5Attribute.WriteBoolean(Dataset.Handle, 'verified', True);
        THdf5Attribute.WriteUInt16(Dataset.Handle, 'osf_channel_index', 7);
      finally
        Dataset.Free;
      end;

      // A standalone group exercises THdf5Group.
      Group := THdf5Group.Create(H5File.Handle, 'info', Lcpl.Handle);
      try
        THdf5Attribute.WriteUtf8String(Group.Handle, 'creator', UTF8String('OsfTool'));
      finally
        Group.Free;
      end;
    finally
      Lcpl.Free;
      Dcpl.Free;
      Space.Free;
      CompoundType.Free;
    end;
  finally
    H5File.Free;
  end;
end;

var
  Major, Minor, Release: Cardinal;
  TempFile: string;
begin
  Writeln('HDF5 binding smoke test');
  Writeln('-----------------------');
  try
    TH5Lib.LoadDll(TH5Lib.Resolve);
    Check(TH5Lib.IsLoaded, 'hdf5.dll loaded from ' + TH5Lib.LoadedPath);

    Major := 0; Minor := 0; Release := 0;
    CheckH5(H5get_libversion(@Major, @Minor, @Release), 'H5get_libversion');
    Writeln(Format('  info  HDF5 library version %d.%d.%d', [Major, Minor, Release]));
    Check((Major = 1) and (Minor = 14) and (Release = 4),
      'library version is 1.14.4');

    Check(H5T_NATIVE_INT64 <> 0, 'H5T_NATIVE_INT64 resolved (non-zero)');
    Check(H5T_STD_I64LE <> 0, 'H5T_STD_I64LE resolved (non-zero)');
    Check(H5T_IEEE_F64LE <> 0, 'H5T_IEEE_F64LE resolved (non-zero)');
    Check(H5T_C_S1 <> 0, 'H5T_C_S1 resolved (non-zero)');
    Check(H5P_CLS_DATASET_CREATE_ID <> 0, 'H5P_CLS_DATASET_CREATE_ID resolved (non-zero)');
    Check(H5P_CLS_LINK_CREATE_ID <> 0, 'H5P_CLS_LINK_CREATE_ID resolved (non-zero)');

    TempFile := TPath.Combine(TPath.GetTempPath, 'osf_hdf5_smoketest.h5');
    if TFile.Exists(TempFile) then
      TFile.Delete(TempFile);
    WriteSampleFile(TempFile);
    Check(TFile.Exists(TempFile), 'output file created');
    Check(TFile.Exists(TempFile) and (TFile.GetSize(TempFile) > 0),
      'output file is non-empty');
    Check(TFile.Exists(TempFile) and HasHdf5Signature(TempFile),
      'output file carries the HDF5 signature');
    Writeln('  info  wrote ', TempFile);
  except
    on E: Exception do
    begin
      Writeln('  FAIL  unhandled ', E.ClassName, ': ', E.Message);
      Inc(Failures);
    end;
  end;

  Writeln('-----------------------');
  if Failures = 0 then
  begin
    Writeln('SMOKE TEST PASSED');
    ExitCode := 0;
  end
  else
  begin
    Writeln(Format('SMOKE TEST FAILED (%d failure(s))', [Failures]));
    ExitCode := 1;
  end;
end.
