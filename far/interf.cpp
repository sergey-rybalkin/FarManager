/*
interf.cpp

���������� ������� �����-������

*/

/* Revision: 1.27 23.05.2001 $ */

/*
Modify:
  23.05.2001 OT
    + ���������� ����� "AltF9 - �� �������"
  23.05.2001 SVS
    ! ������� �������� ������� ��������� ������� ��� ��������� (�� �������
      Alt-F9)
  22.05.2001 tran
    ! �� ��������� ������� �� CodeGuard
  21.05.2001 OT
    - ����������� ��������� AltF9
  06.05.2001 DJ
    ! �������� #include
  06.05.2001 ��
    ! �������������� Window � Frame :)
  05.05.2001 DJ
    + �������� NWZ
  29.04.2001 ��
    + ��������� NWZ �� ����������
  06.03.2001 SVS
   ! SetScreen() - ������ ������ 256 ������� �� 4096 �� ��������� �������
     � ������� (new ��� malloc ������ �� �����, �.�. ������� ����� ����������)
  27.02.2001 SVS
   + BoxText(Char) - ����� ������ �������
   ! � MakeSeparator ��������� ��������� ������� �������������� ����� (Type=3)
  20.02.2001 SVS
   ! ShowSeparator - �������������� �������� - ��� ����������
   + MakeSeparator - �������� ����������� � ������
   ! �������, ��������� �� ������� ��������
     /[\x01-\x08\x0B-\x0C\x0E-\x1F\xB0-\xDF\xF8-\xFF]/
     ���������� � ����.
  14.02.2001 SKV
    ! �������� setpal � InitConsole.
  02.02.2001 VVM
    ! ��������� ������� Text(...). �.�. ��� ���������� ����� �����,
      �� �������� ����� ����� ���� � Text(char *Str)
  24.01.2001 SVS
    + ������������� ������������ ������� KeyQueue,
      ��������  = 1024 ���� �������
  22.01.2001 SVS
    ! ��� FS & WM ����������� 2 ������ ������� - �� ��������� �����
      �������� ����� ������.
  22.01.2001 SVS
    ! �������� � �������� ��� ������� ����������� - ������ ��� ������ �
      SetCursorType, ������� ��� �� ������� ����.
  03.01.2001 SVS
    + ������� SetFarTitle, ScrollBar, ShowSeparator
      ��������� �� mix.cpp
  22.12.2000 SVS
    ! ����������� ����������
      IsMouseButtonPressed -> keyboard.cpp
      GetInputRecord -> keyboard.cpp
      CheckForEsc -> keyboard.cpp
      WriteInput -> keyboard.cpp
      WaitKey -> keyboard.cpp
      PeekInputRecord -> keyboard.cpp
      CalcKeyCode -> keyboard.cpp
    ! hConOut,hConInp - public ������ -> global.cpp
  08.09.2000 SVS
    + KEY_CTRLSHIFTDEL, KEY_ALTSHIFTDEL � ������� CalcKeyCode
  24.08.2000 SVS
    + �������� � ������ WaitKey - ����������� ������� ���������� �������
    + ���������� �� ������� KEY_CTRLALTSHIFTPRESS & KEY_CTRLALTSHIFTRELEASE
  23.08.2000 SVS
    + ��� ��� ������� ������� ���� :-) (�� ���� � ���� ���� ��� �����...)
  09.08.2000 KM
    ! �������� MakeShadow ����� �������, ��� ������ ��� ����������
      ����� (��������) ������ ��� ������ ������� ������ � ��� ������ �� ��
      ���� ���������� ����������, ��� ��� �������� ��������� �������� ����
      ����� ���������.
  23.07.2000 SVS
    ! ������� ����������� � �������� Box(), HiText() :-)
    + ������� (CalcKeyCode):
       Ctrl- Shift- Alt- CtrlShift- AltShift- CtrlAlt- Apps :-)
       KEY_LWIN (VK_LWIN), KEY_RWIN (VK_RWIN)
    + Text(int X, int Y, int Color, char *Str);
    + Text(int X, int Y, int Color, int MsgId);
  13.07.2000 SVS
    ! ��������� ��������� ��� ������������� new/delete/realloc
  11.07.2000 SVS
    ! ��������� ��� ����������� ���������� ��� BC & VC
  06.07.2000 SVS
    - ��������� ������ ����� 11 (NT Console resize bug) �� ������ ������ :-)
  28.06.2000 tran
    - NT Console resize bug
      adding resize support in GetInputRecord for modal window like
      Viewer, Editor, Help
  25.06.2000 SVS
    ! ���������� Master Copy
    ! ��������� � �������� ���������������� ������
*/

#include "headers.hpp"
#pragma hdrstop

#include "farqueue.hpp"
#include "global.hpp"
#include "fn.hpp"
#include "keys.hpp"
#include "colors.hpp"
#include "plugin.hpp"
#include "ctrlobj.hpp"
#include "filepanels.hpp"
#include "panel.hpp"
#include "editor.hpp"
#include "manager.hpp"
#include "scrbuf.hpp"


BOOL __stdcall CtrlHandler(DWORD CtrlType);
static void InitRecodeOutTable();

static int CurX,CurY;
static int CurColor;

static int OutputCP;
static unsigned char RecodeOutTable[256];
static int InitCurVisible,InitCurSize;

void InitConsole(int setpal)
{
  OutputCP=GetConsoleOutputCP();
  InitRecodeOutTable();
  hConOut=CreateFile("CONOUT$",GENERIC_READ|GENERIC_WRITE,
          FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
  ScrBuf.SetHandle(hConOut);
  hConInp=CreateFile("CONIN$",GENERIC_READ|GENERIC_WRITE,
          FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
  SetConsoleCtrlHandler(CtrlHandler,TRUE);
  GetConsoleMode(hConInp,&InitialConsoleMode);
  GetRealCursorType(InitCurVisible,InitCurSize);
  GetRegKey("Interface","Mouse",Opt.Mouse,1);

  // ������ ������������ ������� = 1024 ���� �������
  if(!KeyQueue)
    KeyQueue=new FarQueue<DWORD>(1024);

  SetFarConsoleMode();
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX);
  GetVideoMode(InitScreenBufferInfo);
  GetVideoMode(CurScreenBufferInfo);
  ScrBuf.FillBuf();
  // ���� sizeof(Palette)
  /*$ 14.02.2001 SKV
    ��� consoledetach �� �����, ��� �� ��������� �������.
  */
  if(setpal)memcpy(Palette,DefaultPalette,SizeArrayPalette);
  /* SKV$*/
}


void ReopenConsole()
{
  HANDLE hOldOut=hConOut;
  hConOut=CreateFile("CONOUT$",GENERIC_READ|GENERIC_WRITE,
          FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
  ScrBuf.SetHandle(hConOut);
  SetStdHandle(STD_OUTPUT_HANDLE,hConOut);
  CloseHandle(hOldOut);
}


void CloseConsole()
{
  ScrBuf.Flush();
  SetRealCursorType(InitCurVisible,InitCurSize);
  ChangeConsoleMode(InitialConsoleMode);
  CloseHandle(hConInp);
  CloseHandle(hConOut);
  /* $ 22.05.2001 tran
     codeguard says... */
  delete KeyQueue;
  /* tran $ */
}


void SetFarConsoleMode()
{
  int Mode=ENABLE_WINDOW_INPUT;
  if (Opt.Mouse)
    Mode|=ENABLE_MOUSE_INPUT;
  ChangeConsoleMode(Mode);
}


void InitRecodeOutTable()
{
  int I;
  for (I=0;I<sizeof(RecodeOutTable)/sizeof(RecodeOutTable[0]);I++)
    RecodeOutTable[I]=I;
  if (Opt.CleanAscii)
  {
    for (int I=0;I<32;I++)
      RecodeOutTable[I]='.';
    RecodeOutTable[7]='*';
    RecodeOutTable[24]=RecodeOutTable[25]='|';
    RecodeOutTable[30]='X';
    RecodeOutTable[31]='X';
    RecodeOutTable[255]=' ';
  }
  if (Opt.NoGraphics)
  {
    for (int I=179;I<=218;I++)
      RecodeOutTable[I]='+';
    RecodeOutTable[179]=RecodeOutTable[186]='|';
    RecodeOutTable[196]='-';
    RecodeOutTable[205]='=';
  }
}


void ChangeConsoleMode(int Mode)
{
  DWORD CurrentConsoleMode;
  GetConsoleMode(hConInp,&CurrentConsoleMode);
  if (CurrentConsoleMode!=(DWORD)Mode)
    SetConsoleMode(hConInp,Mode);
}


void FlushInputBuffer()
{
  FlushConsoleInputBuffer(hConInp);
}

//OT
int operator==(CONSOLE_SCREEN_BUFFER_INFO &csbi1,CONSOLE_SCREEN_BUFFER_INFO &csbi2)
{
  return csbi1.dwSize.X==csbi2.dwSize.X && csbi1.dwSize.Y==csbi2.dwSize.Y;
}

#define _OT_ConsoleInfo(_hCon) \
  GetConsoleScreenBufferInfo(_hCon, &_csbi);\
  _OT(SysLog("Current Screen Buffer info[0x%p]:\n\
    dwSize           = (%3i,%3i)\n\
    dwCursorPosition = (%3i,%3i)\n\
    wAttributes        =  0x%02x\n\
    srWindow           = (%3i,%3i), (%3i,%3i)\n\
    dwMaximumWindowSize= (%3i,%3i)\n"\
    ,_hCon\
    ,_csbi.dwSize.X,_csbi.dwSize.Y\
    ,_csbi.dwCursorPosition.X,_csbi.dwCursorPosition.Y\
    ,_csbi.wAttributes\
    ,_csbi.srWindow.Left,_csbi.srWindow.Top,_csbi.srWindow.Right,_csbi.srWindow.Bottom\
    ,_csbi.dwMaximumWindowSize.X,_csbi.dwMaximumWindowSize.Y\
    ))

_OT(void ViewConsoleInfo()\
{\
  CONSOLE_SCREEN_BUFFER_INFO _csbi;\
  _OT_ConsoleInfo(hConOut);\
  _OT_ConsoleInfo(hConInp);\
})

void SetVideoMode(int ScreenMode)
{
  if (!ScreenMode && Opt.AltF9){
    ChangeVideoMode(InitScreenBufferInfo==CurScreenBufferInfo);
  } else {
    ChangeVideoMode(ScrY!=49?50:25,80);
  }
}

void ChangeVideoMode(int Maximized)
{
  COORD coordScreen;
  if (Maximized) {
    SendMessage(hFarWnd,WM_SYSCOMMAND,SC_MAXIMIZE,(LPARAM)0);
    coordScreen = GetLargestConsoleWindowSize(hConOut);
  } else {
    SendMessage(hFarWnd,WM_SYSCOMMAND,SC_RESTORE,(LPARAM)0);
    coordScreen = InitScreenBufferInfo.dwSize;
  }
  ChangeVideoMode(coordScreen.Y,coordScreen.X);

}

void ChangeVideoMode(int NumLines,int NumColumns)
{
  int retSetConsole;
  _OT(DWORD le);
  _OT(SysLog("ChangeVideoMode(NumLines=%i, NumColumns=%i)",NumLines,NumColumns ));
  int xSize=NumColumns,ySize=NumLines;
  CONSOLE_SCREEN_BUFFER_INFO csbi; /* hold current console buffer info */
  SMALL_RECT srWindowRect; /* hold the new console size */
  COORD coordScreen;

  GetConsoleScreenBufferInfo(hConOut, &csbi);
  srWindowRect.Right = xSize-1;
  srWindowRect.Bottom = ySize-1;
  srWindowRect.Left = srWindowRect.Top = (SHORT) 0;
  coordScreen.X = xSize;
  coordScreen.Y = ySize;
  if (xSize>csbi.dwSize.X || ySize > csbi.dwSize.Y){
    if (csbi.dwSize.X<xSize-1){
      srWindowRect.Right = csbi.dwSize.X-1;
      retSetConsole=SetConsoleWindowInfo(hConOut, TRUE, &srWindowRect);
      if (!retSetConsole) {
        _OT(SysLog("LastError=%i, srWindowRect=(%i.%i)(%i.%i)",le=GetLastError(),srWindowRect.Left,srWindowRect.Top,srWindowRect.Right,srWindowRect.Bottom));
      }
      srWindowRect.Right = xSize-1;
    }
    if (csbi.dwSize.Y<ySize-1){
      srWindowRect.Bottom=csbi.dwSize.Y-1;
      retSetConsole=SetConsoleWindowInfo(hConOut, TRUE, &srWindowRect);
      if (!retSetConsole) {
        _OT(SysLog("LastError=%i, srWindowRect",le=GetLastError()));
      }
      srWindowRect.Bottom = ySize-1;
    }
    retSetConsole=SetConsoleScreenBufferSize(hConOut, coordScreen);
    if (!retSetConsole) {
      _OT(SysLog("SetConsoleScreenBufferSize failed... coordScreen=(%i,%i)\n\
        LastError=%i",coordScreen.X,coordScreen.Y,le=GetLastError()));
    }
  }
  if (!(retSetConsole=SetConsoleWindowInfo(hConOut, TRUE, &srWindowRect))){
    _OT(SysLog("LastError=%i, srWindowRect",le=GetLastError()));
    retSetConsole=SetConsoleScreenBufferSize(hConOut, coordScreen);
    _OT(le=GetLastError());
    _OT(SysLog("SetConsoleScreenBufferSize(hConOut, coordScreen),  retSetConsole=%i",retSetConsole));
    retSetConsole=SetConsoleWindowInfo(hConOut, TRUE, &srWindowRect);
    _OT(le=GetLastError());
    _OT(SysLog("SetConsoleWindowInfo(hConOut, TRUE, &srWindowRect),  retSetConsole=%i",retSetConsole));
    _OT(ViewConsoleInfo());
  } else {
    retSetConsole=SetConsoleScreenBufferSize(hConOut, coordScreen);
    _OT(le=GetLastError());
    _OT(SysLog("SetConsoleScreenBufferSize(hConOut, coordScreen),  retSetConsole=%i",retSetConsole));
  }
//  GetVideoMode(CurScreenBufferInfo); // ������ �������� �����, � ����������� �������
  // ��� ������� ������� ������ �����
  if(WinVer.dwPlatformId!=VER_PLATFORM_WIN32_NT)
  {
    INPUT_RECORD Rec;
    DWORD Writes;
    Rec.EventType=WINDOW_BUFFER_SIZE_EVENT;
    Rec.Event.WindowBufferSizeEvent.dwSize.X=NumColumns;
    Rec.Event.WindowBufferSizeEvent.dwSize.Y=NumLines;
    WriteConsoleInput(hConInp,&Rec,1,&Writes);
  }
}


void GetVideoMode(CONSOLE_SCREEN_BUFFER_INFO &csbi)
{
  memset(&csbi,0,sizeof(csbi));
  GetConsoleScreenBufferInfo(hConOut,&csbi);
  ScrX=csbi.dwSize.X-1;
  ScrY=csbi.dwSize.Y-1;
_OT(SysLog("ScrX=%d ScrY=%d",ScrX,ScrY));
  ScrBuf.AllocBuf(csbi.dwSize.X,csbi.dwSize.Y);
  _OT(ViewConsoleInfo());
}

BOOL __stdcall CtrlHandler(DWORD CtrlType)
{
  if (CtrlType==CTRL_C_EVENT || CtrlType==CTRL_BREAK_EVENT)
  {
    if (CtrlType==CTRL_BREAK_EVENT)
      WriteInput(KEY_BREAK);
    if (CtrlObject->Cp()->LeftPanel!=NULL && CtrlObject->Cp()->LeftPanel->GetMode()==PLUGIN_PANEL)
      CtrlObject->Plugins.ProcessEvent(CtrlObject->Cp()->LeftPanel->GetPluginHandle(),FE_BREAK,(void *)CtrlType);
    if (CtrlObject->Cp()->RightPanel!=NULL && CtrlObject->Cp()->RightPanel->GetMode()==PLUGIN_PANEL)
      CtrlObject->Plugins.ProcessEvent(CtrlObject->Cp()->RightPanel->GetPluginHandle(),FE_BREAK,(void *)CtrlType);
    return(TRUE);
  }
  CloseFAR=TRUE;
  if (CurrentEditor!=NULL && CurrentEditor->IsFileModified() ||
      FrameManager->IsAnyFrameModified (FALSE))
    return(TRUE);
  return(FALSE);
}


void GotoXY(int X,int Y)
{
  if (X<0) X=0;
  if (Y<0) Y=0;
  CurX=X;
  CurY=Y;
}


int WhereX()
{
  return(CurX);
}


int WhereY()
{
  return(CurY);
}


void MoveCursor(int X,int Y)
{
  ScrBuf.MoveCursor(X,Y);
}


void GetCursorPos(int& X,int& Y)
{
  ScrBuf.GetCursorPos(X,Y);
}


void SetCursorType(int Visible,int Size)
{
  if (Size==-1 || !Visible)
    Size=IsWindowed()?
       (Opt.CursorSize[0]?Opt.CursorSize[0]:InitCurSize):
       (Opt.CursorSize[1]?Opt.CursorSize[1]:InitCurSize);

  ScrBuf.SetCursorType(Visible,Size);
}


void GetCursorType(int &Visible,int &Size)
{
  ScrBuf.GetCursorType(Visible,Size);
}


void MoveRealCursor(int X,int Y)
{
  COORD C;
  C.X=X;
  C.Y=Y;
  SetConsoleCursorPosition(hConOut,C);
}


void GetRealCursorPos(int& X,int& Y)
{
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(hConOut,&csbi);
  X=csbi.dwCursorPosition.X;
  Y=csbi.dwCursorPosition.Y;
}


void SetRealCursorType(int Visible,int Size)
{
  CONSOLE_CURSOR_INFO cci;
  cci.dwSize=Size;
  cci.bVisible=Visible;
  SetConsoleCursorInfo(hConOut,&cci);
}


void GetRealCursorType(int &Visible,int &Size)
{
  CONSOLE_CURSOR_INFO cci;
  GetConsoleCursorInfo(hConOut,&cci);
  Size=cci.dwSize;
  Visible=cci.bVisible;
}

/* $ 23.07.2000 SVS
   + ��� ������ ������� Text
*/
/* $ 02.02.2001 VVM
    ! ��������� ������� Text(...). �.�. ��� ���������� ����� �����,
      �� �������� ����� ����� ���� � Text(char *Str) */
void Text(int X, int Y, int Color, char *Str)
{
  CurColor=FarColorToReal(Color);
  if (X<0) X=0;
  if (Y<0) Y=0;
  CurX=X;
  CurY=Y;
  Text(Str);
}

void Text(char *Str)
{
  int Length=strlen(Str);
  if (CurX+Length>ScrX)
    Length=ScrX-CurX+1;
  if (Length<=0)
    return;
  CHAR_INFO CharBuf[1024];
  for (int I=0;I<Length;I++)
  {
    CharBuf[I].Char.AsciiChar=RecodeOutTable[Str[I]];
    CharBuf[I].Attributes=CurColor;
  }
  ScrBuf.Write(CurX,CurY,CharBuf,Length);
  CurX+=Length;
}

void Text(int X, int Y, int Color,int MsgId)
{
  Text(X,Y,Color,MSG(MsgId));
}

void Text(int MsgId)
{
  Text(MSG(MsgId));
}
/* VVM $ */
/* SVS $ */

void VText(char *Str)
{
  int Length=strlen(Str);
  if (CurY+Length>ScrY)
    Length=ScrY-CurY+1;
  if (Length<=0)
    return;
  int StartCurX=CurX;
  for (int I=0;I<Length;I++)
  {
    GotoXY(CurX,CurY);
    mprintf("%c",Str[I]);
    CurY++;
    CurX=StartCurX;
  }
}


void HiText(char *Str,int HiColor)
{
  char TextStr[300];
  int SaveColor;
  char *ChPtr;
  strncpy(TextStr,Str,sizeof(TextStr));
  if ((ChPtr=strchr(TextStr,'&'))==NULL)
    Text(TextStr);
  else
  {
    *ChPtr=0;
    Text(TextStr);
    if (ChPtr[1])
    {
      /* $ 23.07.2000 SVS
         ������� ����������� :-)
      */
      char Chr[2];
      SaveColor=CurColor;
      SetColor(HiColor);
      Chr[0]=ChPtr[1]; Chr[1]=0;
      Text(Chr);
      SetColor(SaveColor);
      Text(ChPtr+2);
      /* SVS $ */
    }
  }
}


void SetScreen(int X1,int Y1,int X2,int Y2,int Ch,int Color)
{
  char FillStr[4096];
  int I;
  SetColor(Color);
  if (X1<0) X1=0;
  if (Y1<0) Y1=0;
  if (X2>ScrX) X2=ScrX;
  if (Y2>ScrY) Y2=ScrY;
  memset(FillStr,Ch,X2-X1+1);
  FillStr[X2-X1+1]=0;
  for (I=Y1;I<=Y2;I++)
  {
    GotoXY(X1,I);
    Text(FillStr);
  }
}


void MakeShadow(int X1,int Y1,int X2,int Y2)
{
  int I;
  CHAR_INFO *CharBuf=new CHAR_INFO[(X2-X1+1)*(Y2-Y1+1)];
  /* $ 09.08.2000 KM
     ������ ��������� ��������� ������������ ����, ����
     ���� ���� ���� ����� �� ������� ������
  */
  if (X1<0 || Y1<0)  // || X2>ScrX || Y2>ScrY)
    return;
  /* KM $ */
  GetText(X1,Y1,X2,Y2,CharBuf);
  for (I=0;I<(X2-X1+1)*(Y2-Y1+1);I++)
    CharBuf[I].Attributes&=~0xf8;
  PutText(X1,Y1,X2,Y2,CharBuf);
  /* $ 13.07.2000 SVS
     ��� �� ������� new[], �� � ����� delete[]
  */
  delete[] CharBuf;
  /* SVS $ */
}


void mprintf(char *fmt,...)
{
  va_list argptr;
  va_start(argptr,fmt);
  char OutStr[2048];
  vsprintf(OutStr,fmt,argptr);
  Text(OutStr);
  va_end(argptr);
}

void mprintf(int MsgId,...)
{
  va_list argptr;
  va_start(argptr,MsgId);
  char OutStr[2048];
  vsprintf(OutStr,MSG(MsgId),argptr);
  Text(OutStr);
  va_end(argptr);
}

void vmprintf(char *fmt,...)
{
  va_list argptr;
  va_start(argptr,fmt);
  char OutStr[2048];
  vsprintf(OutStr,fmt,argptr);
  VText(OutStr);
  va_end(argptr);
}

void SetColor(int Color)
{
  CurColor=FarColorToReal(Color);
}


int GetColor()
{
  return(CurColor);
}


void ScrollScreen(int Count)
{
  char *ScreenBuf=new char[(ScrX+1)*(ScrY+1)*4+10];
  if (!ScreenBuf || Count<=0)
    return;
  GetText(0,Count,ScrX,ScrY,ScreenBuf);
  PutText(0,0,ScrX,ScrY-Count,ScreenBuf);
  SetScreen(0,ScrY+1-Count,ScrX,ScrY,' ',F_LIGHTGRAY|B_BLACK);
  /* $ 13.07.2000 SVS
     ��� �� ������� new[], �� � ����� delete[]
  */
  delete[] ScreenBuf;
  /* SVS $ */
}


void GetText(int X1,int Y1,int X2,int Y2,void *Dest)
{
  ScrBuf.Read(X1,Y1,X2,Y2,(CHAR_INFO *)Dest);
}


void PutText(int X1,int Y1,int X2,int Y2,void *Src)
{
  int Width=X2-X1+1;
  for (int I=0;I<Y2-Y1+1;I++)
    ScrBuf.Write(X1,Y1+I,((PCHAR_INFO)Src)+Width*I,Width);
}


void ShowTime(int ShowAlways)
{
  char ClockText[10];
  static SYSTEMTIME lasttm={0,0,0,0,0,0,0,0};
  SYSTEMTIME tm;
  GetLocalTime(&tm);
  CHAR_INFO ScreenClockText[5];
  GetText(ScrX-4,0,ScrX,0,ScreenClockText);
  if (ShowAlways==2)
  {
    memset(&lasttm,0,sizeof(lasttm));
    return;
  }
  if (!ShowAlways && lasttm.wMinute==tm.wMinute && lasttm.wHour==tm.wHour &&
      ScreenClockText[2].Char.AsciiChar==':' || ScreenSaverActive)
    return;
  lasttm=tm;
  sprintf(ClockText,"%02d:%02d",tm.wHour,tm.wMinute);
  GotoXY(ScrX-4,0);
  SetColor(COL_CLOCK);
  Text(ClockText);
  ScrBuf.Flush();
  static int RegChecked=FALSE;
  if (!RegChecked && clock()>10000)
  {
    RegChecked=TRUE;
    struct RegInfo Reg;
    Reg.Done=0;
    _beginthread(CheckReg,0x10000,&Reg);
    while (!Reg.Done)
      Sleep(0);
    if (*Reg.RegName)
    {
      unsigned char Add=158,Xor1=211;
      for (int I=0;Reg.RegName[I];I++)
      {
        Add+=Reg.RegName[I];
        Xor1^=(RegName[I]<<I)^I;
      }
      if ((Add & 0xf)!=ToHex(Reg.RegCode[1]) || ((Add>>3) & 0xf)!=ToHex(Reg.RegCode[2]))
      {
        void *ErrRegFnPtr=((char *)ErrRegFn-111);
        _beginthread((void (cdecl *)(void *))((char *)ErrRegFnPtr+111),0x10000,NULL);
      }
      if (RegVer!=3 && (((Xor1>>5) & 0xf)!=ToHex(Reg.RegCode[4]) || (~Xor1 & 0xf)!=ToHex(Reg.RegCode[6])))
      {
        void *ErrRegFnPtr=((char *)ErrRegFn-50);
        _beginthread((void (cdecl *)(void *))((char *)ErrRegFnPtr+50),0x10000,NULL);
      }
    }
  }
}

void BoxText(unsigned char Chr)
{
  char Str[2];
  Str[0]=Chr;
  Str[1]=0;
  BoxText(Str);
}

void BoxText(char *Str)
{
  if (OutputCP!=437 && OutputCP!=866)
    for (int I=0;Str[I]!=0;I++)
      switch(Str[I])
      {
        case 199:
        case 182:
          Str[I]=186;
          break;
        case 207:
        case 209:
          Str[I]=205;
          break;
      }
  Text(Str);
}

/*
   ��������� ��������������.
*/
/* $ 23.07.2000 SVS
   ������� ����������� :-)
*/
void Box(int x1,int y1,int x2,int y2,int Color,int Type)
{
  char OutStr[4096];
  static char ChrBox[2][6]={
    {0xC4,0xB3,0xDA,0xC0,0xD9,0xBF},
    {0xCD,0xBA,0xC9,0xC8,0xBC,0xBB},
  };

  if (x1>=x2 || y1>=y2)
    return;

  SetColor(Color);
  Type=(Type==SINGLE_BOX || Type==SHORT_SINGLE_BOX)?0:1;

  memset(OutStr,ChrBox[Type][0],sizeof(OutStr));
  OutStr[x2-x1]=0;

  OutStr[0]=ChrBox[Type][2];
  OutStr[x2-x1]=ChrBox[Type][5];
  GotoXY(x1,y1);
  mprintf("%.*s",x2-x1+1,OutStr);

  OutStr[0]=ChrBox[Type][3];
  OutStr[x2-x1]=ChrBox[Type][4];
  GotoXY(x1,y2);
  mprintf("%.*s",x2-x1+1,OutStr);

  memset(OutStr,ChrBox[Type][1],sizeof(OutStr));
  OutStr[y2-y1]=0;

  GotoXY(x1,y1+1);
  vmprintf("%.*s",y2-y1-1,OutStr);
  GotoXY(x2,y1+1);
  vmprintf("%.*s",y2-y1-1,OutStr);
}
/* SVS $ */

void PutRealText(int X1,int Y1,int X2,int Y2,void *Src)
{
  COORD Size,Corner;
  SMALL_RECT Coord;
  if (X2>ScrX) X2=ScrX;
  if (Y2>ScrY) Y2=ScrY;
  if (X1>X2) X1=X2;
  if (Y1>Y2) Y1=Y2;
  Size.X=X2-X1+1;
  Size.Y=Y2-Y1+1;
  Corner.X=0;
  Corner.Y=0;
  Coord.Left=X1;
  Coord.Top=Y1;
  Coord.Right=X2;
  Coord.Bottom=Y2;
  WriteConsoleOutput(hConOut,(PCHAR_INFO)Src,Size,Corner,&Coord);
}

void GetRealText(int X1,int Y1,int X2,int Y2,void *Dest)
{
  COORD Size,Corner;
  SMALL_RECT Coord;
  if (X2>ScrX) X2=ScrX;
  if (Y2>ScrY) Y2=ScrY;
  if (X1>X2) X1=X2;
  if (Y1>Y2) Y1=Y2;
  Size.X=X2-X1+1;
  Size.Y=Y2-Y1+1;
  Corner.X=0;
  Corner.Y=0;
  Coord.Left=X1;
  Coord.Top=Y1;
  Coord.Right=X2;
  Coord.Bottom=Y2;
  ReadConsoleOutput(hConOut,(PCHAR_INFO)Dest,Size,Corner,&Coord);
}

void SetFarTitle(char *Title)
{
  char FarTitle[2*NM];
  sprintf(FarTitle,"%.256s - Far",Title);
  if (WinVer.dwPlatformId!=VER_PLATFORM_WIN32_NT)
    OemToChar(FarTitle,FarTitle);
  SetConsoleTitle(FarTitle);
}

void ScrollBar(int X1,int Y1,int Length,unsigned long Current,unsigned long Total)
{
  /* $ 06.07.2000 tran
     - trap under NT with console height > 210
       was char OutStr[200] :) */
  char OutStr[4096];
  /* tran 06.07.2000 $ */

  int ThumbPos;
  if ((Length-=2)<1)
    return;
  if (Total>0)
    ThumbPos=Length*Current/Total;
  else
    ThumbPos=0;
  if (ThumbPos>=Length)
    ThumbPos=Length-1;
  GotoXY(X1,Y1);
  memset(OutStr,0xB0,Length);
  OutStr[ThumbPos]=0xB2;
  OutStr[Length]=0;
  vmprintf("%c%s%c",0x1E,OutStr,0x1F);
}

void ShowSeparator(int Length,int Type)
{
  if (Length>1)
  {
    char Separator[4096];
    MakeSeparator(Length,Separator,Type);
    BoxText(Separator);
  }
}

// "����������" ��������� � ������.
char* MakeSeparator(int Length,char *DestStr,int Type)
{
  if (Length>1 && DestStr)
  {
    static unsigned char BoxType[4][4]={
      {0x20,0x20,0xC4,0x00},
      {0xC7,0xB6,0xC4,0x00},
      {0xC3,0xB4,0xC4,0x00},
      {0xCC,0xB9,0xCD,0x00},
    };
    Type%=(sizeof(BoxType)/sizeof(BoxType[0]));
    memset(DestStr,BoxType[Type][2],Length);
    DestStr[0]=BoxType[Type][0];
    DestStr[Length-1]=BoxType[Type][1];
    DestStr[Length]=0;
  }
  return DestStr;
}
