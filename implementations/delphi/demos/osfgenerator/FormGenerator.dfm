object FormGenerator: TFormGenerator
  Left = 0
  Top = 0
  Caption = 'OSF Demo Generator'
  ClientHeight = 540
  ClientWidth = 720
  Color = clBtnFace
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clWindowText
  Font.Height = -11
  Font.Name = 'Segoe UI'
  Font.Style = []
  Menu = MainMenu1
  Position = poScreenCenter
  OnCreate = FormCreate
  TextHeight = 13
  object gbOutput: TGroupBox
    Left = 8
    Top = 8
    Width = 704
    Height = 60
    Anchors = [akLeft, akTop, akRight]
    Caption = 'Output directory'
    TabOrder = 0
    object edOutputDir: TEdit
      Left = 12
      Top = 24
      Width = 593
      Height = 21
      Anchors = [akLeft, akTop, akRight]
      TabOrder = 0
    end
    object btBrowse: TButton
      Left = 615
      Top = 22
      Width = 75
      Height = 25
      Anchors = [akTop, akRight]
      Caption = 'Browse...'
      TabOrder = 1
      OnClick = btBrowseClick
    end
  end
  object gbOptions: TGroupBox
    Left = 8
    Top = 76
    Width = 704
    Height = 80
    Anchors = [akLeft, akTop, akRight]
    Caption = 'Options'
    TabOrder = 1
    object lblSampleCount: TLabel
      Left = 360
      Top = 26
      Width = 99
      Height = 13
      Caption = 'Samples per channel'
    end
    object cbOSF4: TCheckBox
      Left = 12
      Top = 24
      Width = 161
      Height = 17
      Caption = 'Generate OSF4 files'
      Checked = True
      State = cbChecked
      TabOrder = 0
    end
    object cbOSF5: TCheckBox
      Left = 12
      Top = 47
      Width = 161
      Height = 17
      Caption = 'Generate OSF5 files'
      Checked = True
      State = cbChecked
      TabOrder = 1
    end
    object spSampleCount: TSpinEdit
      Left = 480
      Top = 23
      Width = 121
      Height = 22
      MaxValue = 100000
      MinValue = 10
      TabOrder = 2
      Value = 100
    end
  end
  object btGenerate: TButton
    Left = 8
    Top = 168
    Width = 704
    Height = 33
    Anchors = [akLeft, akTop, akRight]
    Caption = 'Generate'
    Default = True
    TabOrder = 2
    OnClick = btGenerateClick
  end
  object mLog: TMemo
    Left = 8
    Top = 208
    Width = 704
    Height = 305
    Anchors = [akLeft, akTop, akRight, akBottom]
    Font.Charset = DEFAULT_CHARSET
    Font.Color = clWindowText
    Font.Height = -11
    Font.Name = 'Consolas'
    Font.Style = []
    ParentFont = False
    ReadOnly = True
    ScrollBars = ssBoth
    TabOrder = 3
    WordWrap = False
  end
  object StatusBar1: TStatusBar
    Left = 0
    Top = 521
    Width = 720
    Height = 19
    Panels = <>
    SimplePanel = True
  end
  object MainMenu1: TMainMenu
    Left = 600
    Top = 80
    object miFile: TMenuItem
      Caption = '&File'
      object miFileExit: TMenuItem
        Caption = 'E&xit'
        OnClick = miFileExitClick
      end
    end
    object miGenerate: TMenuItem
      Caption = '&Generate'
      object miGenerateAll: TMenuItem
        Caption = 'Generate &All'
        OnClick = miGenerateAllClick
      end
      object miSep1: TMenuItem
        Caption = '-'
      end
      object miGenOSF4: TMenuItem
        Caption = 'OSF4 - All Types'
        OnClick = miGenOSF4Click
      end
      object miGenOSF5: TMenuItem
        Caption = 'OSF5 - All Types'
        OnClick = miGenOSF5Click
      end
    end
  end
end
