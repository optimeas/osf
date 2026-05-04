object FormOSFViewer: TFormOSFViewer
  Left = 0
  Top = 0
  Caption = 'OSF Viewer'
  ClientHeight = 600
  ClientWidth = 1000
  Color = clBtnFace
  Constraints.MinHeight = 400
  Constraints.MinWidth = 600
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
  object splVert: TSplitter
    Left = 280
    Top = 0
    Height = 600
    ExplicitLeft = 280
    ExplicitTop = 0
    ExplicitHeight = 600
  end
  object pnlLeft: TPanel
    Left = 0
    Top = 0
    Width = 280
    Height = 600
    Align = alLeft
    BevelOuter = bvNone
    Padding.Left = 4
    Padding.Top = 4
    Padding.Right = 4
    TabOrder = 0
    object lblChannels: TLabel
      AlignWithMargins = True
      Left = 4
      Top = 4
      Width = 272
      Height = 17
      Margins.Left = 0
      Margins.Top = 0
      Margins.Right = 0
      Margins.Bottom = 4
      Align = alTop
      Caption = 'Channels'
      ExplicitWidth = 60
    end
    object lbChannels: TListBox
      Left = 4
      Top = 25
      Width = 272
      Height = 575
      Align = alClient
      Style = lbOwnerDrawFixed
      Font.Charset = DEFAULT_CHARSET
      Font.Color = clWindowText
      Font.Height = -12
      Font.Name = 'Consolas'
      Font.Style = []
      ItemHeight = 16
      ParentFont = False
      TabOrder = 0
      OnClick = lbChannelsClick
      OnDrawItem = lbChannelsDrawItem
    end
  end
  object pnlRight: TPanel
    Left = 285
    Top = 0
    Width = 715
    Height = 600
    Align = alClient
    BevelOuter = bvNone
    TabOrder = 1
    object splHorz: TSplitter
      Cursor = crVSplit
      Left = 0
      Top = 495
      Width = 715
      Height = 5
      Align = alBottom
      ExplicitTop = 495
      ExplicitWidth = 715
    end
    object chtData: TChart
      Left = 0
      Top = 0
      Width = 715
      Height = 495
      Title.Font.Color = clWindowText
      Title.Font.Height = -13
      Title.Font.Name = 'Segoe UI'
      Title.Font.Style = [fsBold]
      Title.Text.Strings = (
        '')
      BottomAxis.DateTimeFormat = 'yyyy-mm-dd hh:nn:ss'
      View3D = False
      Align = alClient
      TabOrder = 0
    end
    object lblNoChart: TLabel
      Left = 0
      Top = 0
      Width = 715
      Height = 495
      Anchors = [akLeft, akTop, akRight, akBottom]
      Alignment = taCenter
      Caption = 'Channel type cannot be displayed as a chart'
      Font.Charset = DEFAULT_CHARSET
      Font.Color = clGrayText
      Font.Height = -16
      Font.Name = 'Segoe UI'
      Font.Style = []
      Layout = tlCenter
      ParentFont = False
      Visible = False
      ExplicitWidth = 312
      ExplicitHeight = 21
    end
    object pnlBottomLog: TPanel
      Left = 0
      Top = 500
      Width = 715
      Height = 100
      Align = alBottom
      BevelOuter = bvNone
      TabOrder = 1
      object cbDebug: TCheckBox
        Left = 8
        Top = 4
        Width = 200
        Height = 17
        Caption = 'Show debug output'
        TabOrder = 0
        OnClick = cbDebugClick
      end
      object memLog: TMemo
        Left = 0
        Top = 24
        Width = 715
        Height = 76
        Align = alBottom
        Font.Charset = DEFAULT_CHARSET
        Font.Color = clWindowText
        Font.Height = -11
        Font.Name = 'Consolas'
        Font.Style = []
        ParentFont = False
        ReadOnly = True
        ScrollBars = ssBoth
        TabOrder = 1
        WordWrap = False
      end
    end
  end
  object MainMenu1: TMainMenu
    Left = 16
    Top = 16
    object miFile: TMenuItem
      Caption = '&File'
      object miOpen: TMenuItem
        Caption = '&Open...'
        ShortCut = 16463
        OnClick = miOpenClick
      end
      object miSep1: TMenuItem
        Caption = '-'
      end
      object miExit: TMenuItem
        Caption = 'E&xit'
        OnClick = miExitClick
      end
    end
  end
  object OpenDialog1: TOpenDialog
    Filter = 'OSF files (*.osf, *.osfz)|*.osf;*.osfz|All files (*.*)|*.*'
    Title = 'Open OSF file'
    Left = 16
    Top = 64
  end
end
