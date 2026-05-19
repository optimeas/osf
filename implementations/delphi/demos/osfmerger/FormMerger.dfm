object FormMerger: TFormMerger
  Left = 0
  Top = 0
  Caption = 'OSF Merger'
  ClientHeight = 720
  ClientWidth = 1100
  Color = clBtnFace
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clWindowText
  Font.Height = -12
  Font.Name = 'Segoe UI'
  Font.Style = []
  Menu = mmMain
  Position = poScreenCenter
  OnCreate = FormCreate
  OnDestroy = FormDestroy
  TextHeight = 15
  object pcSource: TPageControl
    Left = 8
    Top = 8
    Width = 700
    Height = 270
    ActivePage = tsDirectory
    Anchors = [akLeft, akTop, akRight]
    TabOrder = 0
    object tsDirectory: TTabSheet
      Caption = 'Directory scan'
      object gbRoot: TGroupBox
        Left = 8
        Top = 4
        Width = 676
        Height = 60
        Anchors = [akLeft, akTop, akRight]
        Caption = 'Root directory'
        TabOrder = 0
        object lblRoot: TLabel
          Left = 12
          Top = 24
          Width = 16
          Height = 15
          Caption = 'Dir'
        end
        object edRootDir: TEdit
          Left = 40
          Top = 20
          Width = 540
          Height = 23
          Anchors = [akLeft, akTop, akRight]
          TabOrder = 0
        end
        object btBrowseRoot: TButton
          Left = 588
          Top = 20
          Width = 80
          Height = 25
          Anchors = [akTop, akRight]
          Caption = 'Browse...'
          TabOrder = 1
          OnClick = btBrowseRootClick
        end
      end
      object gbInterval: TGroupBox
        Left = 8
        Top = 72
        Width = 676
        Height = 60
        Anchors = [akLeft, akTop, akRight]
        Caption = 'Time interval (UTC)'
        TabOrder = 1
        object lblStart: TLabel
          Left = 12
          Top = 24
          Width = 26
          Height = 15
          Caption = 'From'
        end
        object lblTo: TLabel
          Left = 332
          Top = 24
          Width = 12
          Height = 15
          Caption = 'to'
        end
        object dtpStartDate: TDateTimePicker
          Left = 48
          Top = 20
          Width = 130
          Height = 23
          Date = 45413.0
          Time = 0
          TabOrder = 0
        end
        object dtpStartTime: TDateTimePicker
          Left = 184
          Top = 20
          Width = 130
          Height = 23
          Date = 0
          Time = 0
          Kind = dtkTime
          TabOrder = 1
        end
        object dtpEndDate: TDateTimePicker
          Left = 350
          Top = 20
          Width = 130
          Height = 23
          Date = 45413.0
          Time = 0
          TabOrder = 2
        end
        object dtpEndTime: TDateTimePicker
          Left = 486
          Top = 20
          Width = 130
          Height = 23
          Date = 0
          Time = 0.999988425925926
          Kind = dtkTime
          TabOrder = 3
        end
      end
      object gbFoundFiles: TGroupBox
        Left = 8
        Top = 140
        Width = 676
        Height = 100
        Anchors = [akLeft, akTop, akRight, akBottom]
        Caption = 'Found files'
        TabOrder = 2
        object lvFiles: TListView
          Left = 8
          Top = 20
          Width = 660
          Height = 72
          Anchors = [akLeft, akTop, akRight, akBottom]
          Columns = <
            item
              Caption = 'File'
              Width = 220
            end
            item
              Caption = 'First timestamp'
              Width = 140
            end
            item
              Caption = 'Last timestamp'
              Width = 140
            end
            item
              Caption = 'Channels'
              Width = 70
            end
            item
              Caption = 'In interval'
              Width = 80
            end>
          ReadOnly = True
          RowSelect = True
          TabOrder = 0
          ViewStyle = vsReport
        end
      end
    end
    object tsFileList: TTabSheet
      Caption = 'File list'
      ImageIndex = 1
      object lbFiles: TListBox
        Left = 8
        Top = 8
        Width = 480
        Height = 230
        Anchors = [akLeft, akTop, akRight, akBottom]
        ItemHeight = 15
        MultiSelect = True
        TabOrder = 0
      end
      object pnlFileButtons: TPanel
        Left = 496
        Top = 8
        Width = 180
        Height = 230
        Anchors = [akTop, akRight, akBottom]
        BevelOuter = bvNone
        TabOrder = 1
        object btFilesAdd: TButton
          Left = 0
          Top = 0
          Width = 180
          Height = 30
          Caption = 'Add files...'
          TabOrder = 0
          OnClick = btFilesAddClick
        end
        object btFilesAddDir: TButton
          Left = 0
          Top = 36
          Width = 180
          Height = 30
          Caption = 'Add directory...'
          TabOrder = 1
          OnClick = btFilesAddDirClick
        end
        object btFilesRemove: TButton
          Left = 0
          Top = 72
          Width = 180
          Height = 30
          Caption = 'Remove selected'
          TabOrder = 2
          OnClick = btFilesRemoveClick
        end
      end
    end
  end
  object gbChannelFilter: TGroupBox
    Left = 716
    Top = 8
    Width = 376
    Height = 130
    Anchors = [akTop, akRight]
    Caption = 'Channel filter'
    TabOrder = 1
    object lblChannelFilter: TLabel
      Left = 12
      Top = 20
      Width = 287
      Height = 15
      Caption = 'One channel name per line — empty for all channels'
    end
    object mChannelFilter: TMemo
      Left = 12
      Top = 40
      Width = 352
      Height = 80
      Anchors = [akLeft, akTop, akRight, akBottom]
      ScrollBars = ssVertical
      TabOrder = 0
    end
  end
  object gbOptions: TGroupBox
    Left = 716
    Top = 148
    Width = 376
    Height = 130
    Anchors = [akTop, akRight]
    Caption = 'Options'
    TabOrder = 2
    object lblOverlap: TLabel
      Left = 12
      Top = 28
      Width = 92
      Height = 15
      Caption = 'Overlap strategy'
    end
    object lblOutputFmt: TLabel
      Left = 12
      Top = 64
      Width = 73
      Height = 15
      Caption = 'Output format'
    end
    object cbOverlap: TComboBox
      Left = 120
      Top = 24
      Width = 240
      Height = 23
      Style = csDropDownList
      TabOrder = 0
    end
    object cbOutputFmt: TComboBox
      Left = 120
      Top = 60
      Width = 240
      Height = 23
      Style = csDropDownList
      TabOrder = 1
    end
  end
  object pnlActions: TPanel
    Left = 8
    Top = 284
    Width = 1084
    Height = 40
    Anchors = [akLeft, akTop, akRight]
    BevelOuter = bvNone
    TabOrder = 3
    object btScan: TButton
      Left = 0
      Top = 6
      Width = 130
      Height = 28
      Caption = 'Scan'
      TabOrder = 0
      OnClick = btScanClick
    end
    object btMerge: TButton
      Left = 140
      Top = 6
      Width = 180
      Height = 28
      Caption = 'Merge to DataManager'
      TabOrder = 1
      OnClick = btMergeClick
    end
    object btSave: TButton
      Left = 330
      Top = 6
      Width = 200
      Height = 28
      Caption = 'Merge and Save OSF...'
      TabOrder = 2
      OnClick = btSaveClick
    end
  end
  object gbResult: TGroupBox
    Left = 8
    Top = 328
    Width = 1084
    Height = 180
    Anchors = [akLeft, akTop, akRight]
    Caption = 'Merge result'
    TabOrder = 4
    object lvResult: TListView
      Left = 8
      Top = 20
      Width = 1068
      Height = 150
      Anchors = [akLeft, akTop, akRight, akBottom]
      Columns = <
        item
          Caption = 'Channel'
          Width = 240
        end
        item
          Caption = 'Data type'
          Width = 90
        end
        item
          Caption = 'Unit'
          Width = 80
        end
        item
          Caption = 'Samples'
          Width = 100
        end
        item
          Caption = 'First timestamp'
          Width = 200
        end
        item
          Caption = 'Last timestamp'
          Width = 200
        end>
      ReadOnly = True
      RowSelect = True
      TabOrder = 0
      ViewStyle = vsReport
    end
  end
  object pnlBottom: TPanel
    Left = 8
    Top = 516
    Width = 1084
    Height = 180
    Anchors = [akLeft, akRight, akBottom]
    BevelOuter = bvNone
    TabOrder = 5
    object cbDebug: TCheckBox
      Left = 0
      Top = 4
      Width = 200
      Height = 21
      Caption = 'Show debug output'
      TabOrder = 0
      OnClick = cbDebugClick
    end
    object mLog: TMemo
      Left = 0
      Top = 30
      Width = 1084
      Height = 150
      Anchors = [akLeft, akTop, akRight, akBottom]
      Font.Charset = ANSI_CHARSET
      Font.Color = clWindowText
      Font.Height = -12
      Font.Name = 'Consolas'
      Font.Style = []
      ParentFont = False
      ReadOnly = True
      ScrollBars = ssBoth
      TabOrder = 1
      WordWrap = False
    end
  end
  object sbStatus: TStatusBar
    Left = 0
    Top = 701
    Width = 1100
    Height = 19
    Panels = <>
    SimplePanel = True
  end
  object mmMain: TMainMenu
    Left = 16
    Top = 16
    object miFile: TMenuItem
      Caption = '&File'
      object miExit: TMenuItem
        Caption = 'E&xit'
        OnClick = miExitClick
      end
    end
    object miActions: TMenuItem
      Caption = '&Actions'
      object miActScan: TMenuItem
        Caption = '&Scan'
        OnClick = btScanClick
      end
      object miActMerge: TMenuItem
        Caption = '&Merge to DataManager'
        OnClick = btMergeClick
      end
      object miActSave: TMenuItem
        Caption = 'Merge and Save &OSF...'
        OnClick = btSaveClick
      end
    end
  end
  object OpenFilesDialog: TOpenDialog
    Filter = 'OSF files (*.osf;*.osfz)|*.osf;*.osfz|All files (*.*)|*.*'
    Options = [ofHideReadOnly, ofAllowMultiSelect, ofEnableSizing]
    Left = 80
    Top = 16
  end
  object SaveOSFDialog: TSaveDialog
    DefaultExt = 'osf'
    Filter = 'OSF files (*.osf)|*.osf'
    Options = [ofOverwritePrompt, ofHideReadOnly, ofEnableSizing]
    Left = 144
    Top = 16
  end
end
