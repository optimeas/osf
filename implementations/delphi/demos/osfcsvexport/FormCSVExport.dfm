object FormCSVExport: TFormCSVExport
  Left = 0
  Top = 0
  Caption = 'OSF to CSV Exporter'
  ClientHeight = 600
  ClientWidth = 760
  Color = clBtnFace
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clWindowText
  Font.Height = -11
  Font.Name = 'Segoe UI'
  Font.Style = []
  Menu = MainMenu1
  Position = poScreenCenter
  OnCreate = FormCreate
  OnDestroy = FormDestroy
  TextHeight = 13
  object gbSource: TGroupBox
    Left = 8
    Top = 8
    Width = 744
    Height = 60
    Anchors = [akLeft, akTop, akRight]
    Caption = 'Source file'
    TabOrder = 0
    object edSourceFile: TEdit
      Left = 12
      Top = 24
      Width = 633
      Height = 21
      Anchors = [akLeft, akTop, akRight]
      ReadOnly = True
      TabOrder = 0
    end
    object btBrowse: TButton
      Left = 655
      Top = 22
      Width = 75
      Height = 25
      Anchors = [akTop, akRight]
      Caption = 'Browse...'
      TabOrder = 1
      OnClick = btBrowseClick
    end
  end
  object gbExportOptions: TGroupBox
    Left = 8
    Top = 76
    Width = 744
    Height = 92
    Anchors = [akLeft, akTop, akRight]
    Caption = 'Export options'
    TabOrder = 1
    object lblDecimal: TLabel
      Left = 380
      Top = 26
      Width = 91
      Height = 13
      Caption = 'Decimal separator'
    end
    object lblEncoding: TLabel
      Left = 380
      Top = 56
      Width = 47
      Height = 13
      Caption = 'Encoding'
    end
    object cbExcludeEmpty: TCheckBox
      Left = 12
      Top = 24
      Width = 250
      Height = 17
      Caption = 'Exclude channels with 0 samples'
      Checked = True
      State = cbChecked
      TabOrder = 0
      OnClick = cbExcludeEmptyClick
    end
    object cbAbsoluteTimestamp: TCheckBox
      Left = 12
      Top = 54
      Width = 250
      Height = 17
      Caption = 'Absolute timestamps'
      Checked = True
      State = cbChecked
      TabOrder = 1
    end
    object cbDecimalSep: TComboBox
      Left = 500
      Top = 23
      Width = 230
      Height = 21
      Style = csDropDownList
      TabOrder = 2
    end
    object cbEncoding: TComboBox
      Left = 500
      Top = 53
      Width = 230
      Height = 21
      Style = csDropDownList
      TabOrder = 3
    end
  end
  object gbChannels: TGroupBox
    Left = 8
    Top = 178
    Width = 744
    Height = 240
    Anchors = [akLeft, akTop, akRight, akBottom]
    Caption = 'Channel preview'
    TabOrder = 2
    object lvChannels: TListView
      Left = 12
      Top = 24
      Width = 720
      Height = 205
      Anchors = [akLeft, akTop, akRight, akBottom]
      ReadOnly = True
      RowSelect = True
      TabOrder = 0
      ViewStyle = vsReport
    end
  end
  object btExport: TButton
    Left = 8
    Top = 426
    Width = 744
    Height = 33
    Anchors = [akLeft, akRight, akBottom]
    Caption = 'Export to CSV...'
    Enabled = False
    TabOrder = 3
    OnClick = btExportClick
  end
  object cbDebug: TCheckBox
    Left = 8
    Top = 467
    Width = 250
    Height = 17
    Anchors = [akLeft, akBottom]
    Caption = 'Show debug output'
    TabOrder = 4
    OnClick = cbDebugClick
  end
  object mLog: TMemo
    Left = 8
    Top = 488
    Width = 744
    Height = 93
    Anchors = [akLeft, akRight, akBottom]
    Font.Charset = DEFAULT_CHARSET
    Font.Color = clWindowText
    Font.Height = -11
    Font.Name = 'Consolas'
    Font.Style = []
    ParentFont = False
    ReadOnly = True
    ScrollBars = ssBoth
    TabOrder = 5
    WordWrap = False
  end
  object StatusBar1: TStatusBar
    Left = 0
    Top = 581
    Width = 760
    Height = 19
    Panels = <>
    SimplePanel = True
  end
  object MainMenu1: TMainMenu
    Left = 632
    Top = 96
    object miFile: TMenuItem
      Caption = '&File'
      object miFileOpen: TMenuItem
        Caption = '&Open OSF...'
        ShortCut = 16463
        OnClick = miFileOpenClick
      end
      object miFileExport: TMenuItem
        Caption = '&Export CSV...'
        Enabled = False
        ShortCut = 16453
        OnClick = miFileExportClick
      end
      object miFileExit: TMenuItem
        Caption = 'E&xit'
        OnClick = miFileExitClick
      end
    end
  end
  object OpenDialog: TOpenDialog
    Left = 632
    Top = 144
  end
  object SaveDialog: TSaveDialog
    Left = 632
    Top = 192
  end
end
