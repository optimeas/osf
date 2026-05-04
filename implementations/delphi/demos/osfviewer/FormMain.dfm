object FormOSFViewer: TFormOSFViewer
  Left = 0
  Top = 0
  Caption = 'OSF Viewer'
  ClientHeight = 753
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
    Height = 753
    ExplicitHeight = 600
  end
  object pnlLeft: TPanel
    Left = 0
    Top = 0
    Width = 280
    Height = 753
    Align = alLeft
    BevelOuter = bvNone
    Padding.Left = 4
    Padding.Top = 4
    Padding.Right = 4
    TabOrder = 0
    ExplicitHeight = 592
    object lblChannels: TLabel
      AlignWithMargins = True
      Left = 4
      Top = 4
      Width = 272
      Height = 13
      Margins.Left = 0
      Margins.Top = 0
      Margins.Right = 0
      Margins.Bottom = 4
      Align = alTop
      Caption = 'Channels'
      ExplicitWidth = 48
    end
    object lbChannels: TListBox
      Left = 4
      Top = 21
      Width = 272
      Height = 732
      Style = lbOwnerDrawFixed
      Align = alClient
      Font.Charset = DEFAULT_CHARSET
      Font.Color = clWindowText
      Font.Height = -12
      Font.Name = 'Consolas'
      Font.Style = []
      ParentFont = False
      TabOrder = 0
      OnClick = lbChannelsClick
      OnDrawItem = lbChannelsDrawItem
      ExplicitHeight = 571
    end
  end
  object pnlRight: TPanel
    Left = 283
    Top = 0
    Width = 717
    Height = 753
    Align = alClient
    BevelOuter = bvNone
    TabOrder = 1
    ExplicitLeft = 282
    ExplicitHeight = 814
    DesignSize = (
      717
      753)
    object splHorz: TSplitter
      Left = 0
      Top = 569
      Width = 717
      Height = 5
      Cursor = crVSplit
      Align = alTop
      ExplicitTop = 495
      ExplicitWidth = 715
    end
    object lblNoChart: TLabel
      Left = 0
      Top = 0
      Width = 303
      Height = 174
      Alignment = taCenter
      Anchors = [akLeft, akTop, akRight, akBottom]
      Caption = 'Channel type cannot be displayed as a chart'
      Font.Charset = DEFAULT_CHARSET
      Font.Color = clGrayText
      Font.Height = -16
      Font.Name = 'Segoe UI'
      Font.Style = []
      ParentFont = False
      Layout = tlCenter
      Visible = False
      ExplicitHeight = 21
    end
    object chtData: TChart
      Left = 0
      Top = 0
      Width = 717
      Height = 569
      Title.Font.Color = clWindowText
      Title.Font.Name = 'Segoe UI'
      Title.Font.Style = [fsBold]
      Title.Text.Strings = (
        '')
      BottomAxis.DateTimeFormat = 'yyyy-mm-dd hh:nn:ss'
      View3D = False
      Align = alTop
      TabOrder = 0
      DefaultCanvas = 'TGDIPlusCanvas'
      ColorPaletteIndex = 0
    end
    object pnlBottomLog: TPanel
      Left = 0
      Top = 615
      Width = 717
      Height = 138
      Align = alClient
      BevelOuter = bvNone
      TabOrder = 1
      ExplicitTop = 492
      ExplicitWidth = 715
      ExplicitHeight = 100
      object memLog: TMemo
        AlignWithMargins = True
        Left = 8
        Top = 8
        Width = 701
        Height = 122
        Margins.Left = 8
        Margins.Top = 8
        Margins.Right = 8
        Margins.Bottom = 8
        Align = alClient
        Font.Charset = DEFAULT_CHARSET
        Font.Color = clWindowText
        Font.Height = -11
        Font.Name = 'Consolas'
        Font.Style = []
        ParentFont = False
        ReadOnly = True
        ScrollBars = ssBoth
        TabOrder = 0
        WordWrap = False
        ExplicitLeft = 6
        ExplicitTop = -24
        ExplicitWidth = 717
        ExplicitHeight = 100
      end
    end
    object Panel1: TPanel
      Left = 0
      Top = 574
      Width = 717
      Height = 41
      Align = alTop
      BevelOuter = bvNone
      TabOrder = 2
      ExplicitLeft = 23
      ExplicitTop = 580
      ExplicitWidth = 185
      object cbDebug: TCheckBox
        Left = 19
        Top = 12
        Width = 200
        Height = 17
        Margins.Left = 16
        Caption = 'Show debug output'
        TabOrder = 0
        OnClick = cbDebugClick
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
