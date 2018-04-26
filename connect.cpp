﻿/*=============================================================================
*
*								ホストへの接続／切断
*
===============================================================================
/ Copyright (C) 1997-2007 Sota. All rights reserved.
/
/ Redistribution and use in source and binary forms, with or without 
/ modification, are permitted provided that the following conditions 
/ are met:
/
/  1. Redistributions of source code must retain the above copyright 
/     notice, this list of conditions and the following disclaimer.
/  2. Redistributions in binary form must reproduce the above copyright 
/     notice, this list of conditions and the following disclaimer in the 
/     documentation and/or other materials provided with the distribution.
/
/ THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR 
/ IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
/ OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
/ IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, 
/ INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
/ BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF 
/ USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
/ ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
/ (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF 
/ THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
/============================================================================*/

#include "common.h"
#include "helpid.h"


/*===== プロトタイプ =====*/

// 64ビット対応
//static BOOL CALLBACK QuickConDialogCallBack(HWND hDlg, UINT iMessage, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK QuickConDialogCallBack(HWND hDlg, UINT iMessage, WPARAM wParam, LPARAM lParam);
// 同時接続対応
//static int SendInitCommand(char *Cmd);
static int SendInitCommand(SOCKET Socket, char *Cmd, int *CancelCheckWork);
static void AskUseFireWall(char *Host, int *Fire, int *Pasv, int *List);
static void SaveCurrentSetToHistory(void);
static int ReConnectSkt(SOCKET *Skt);
// 暗号化通信対応
// 同時接続対応
//static SOCKET DoConnect(char *Host, char *User, char *Pass, char *Acct, int Port, int Fwall, int SavePass, int Security);
static SOCKET DoConnectCrypt(int CryptMode, HOSTDATA* HostData, char *Host, char *User, char *Pass, char *Acct, int Port, int Fwall, int SavePass, int Security, int *CancelCheckWork);
static SOCKET DoConnect(HOSTDATA* HostData, char *Host, char *User, char *Pass, char *Acct, int Port, int Fwall, int SavePass, int Security, int *CancelCheckWork);
static int CheckOneTimePassword(char *Pass, char *Reply, int Type);
static int Socks5MakeCmdPacket(SOCKS5REQUEST *Packet, char Cmd, std::variant<const sockaddr*, const char*> addr, ushort Port);
static int SocksSendCmd(SOCKET Socket, void *Data, int Size, int *CancelCheckWork);
// 同時接続対応
//static int Socks5GetCmdReply(SOCKET Socket, SOCKS5REPLY *Packet);
static int Socks5GetCmdReply(SOCKET Socket, SOCKS5REPLY *Packet, int *CancelCheckWork);
// 同時接続対応
//static int Socks4GetCmdReply(SOCKET Socket, SOCKS4REPLY *Packet);
static int Socks4GetCmdReply(SOCKET Socket, SOCKS4REPLY *Packet, int *CancelCheckWork);
static int Socks5SelMethod(SOCKET Socket, int *CancelCheckWork);

/*===== 外部参照 =====*/

extern char FilterStr[FILTER_EXT_LEN+1];
extern char TitleHostName[HOST_ADRS_LEN+1];
extern int CancelFlg;
// タイトルバーにユーザー名表示対応
extern char TitleUserName[USER_NAME_LEN+1];

/* 設定値 */
extern char UserMailAdrs[USER_MAIL_LEN+1];
extern char FwallHost[HOST_ADRS_LEN+1];
extern char FwallUser[USER_NAME_LEN+1];
extern char FwallPass[PASSWORD_LEN+1];
extern int FwallPort;
extern int FwallType;
extern int FwallDefault;
extern int FwallSecurity;
extern int FwallResolve;
extern int FwallLower;
extern int FwallDelimiter;
extern int PasvDefault;
extern int QuickAnonymous;
// 切断対策
extern int TimeOut;
// UPnP対応
extern int UPnPEnabled;
extern bool SupportIdn;

/*===== ローカルなワーク =====*/

static int Anonymous;
static int TryConnect = NO;
static SOCKET CmdCtrlSocket = INVALID_SOCKET;
static SOCKET TrnCtrlSocket = INVALID_SOCKET;
static HOSTDATA CurHost;

/* 接続中の接続先、SOCKSサーバのアドレス情報を保存しておく */
/* この情報はlistenソケットを取得する際に用いる */
// IPv6対応
//static struct sockaddr_in SocksSockAddr;	/* SOCKSサーバのアドレス情報 */
//static struct sockaddr_in CurSockAddr;		/* 接続先ホストのアドレス情報 */

static int UseIPadrs;
static char DomainName[HOST_ADRS_LEN+1];

#if defined(HAVE_TANDEM)
static int Oss = NO;  /* OSS ファイルシステムへアクセスしている場合は YES */
#endif



/*----- ホスト一覧を使ってホストへ接続 ----------------------------------------
*
*	Parameter
*		int Type : ダイアログのタイプ (DLG_TYPE_xxx)
*		int Num : 接続するホスト番号(0～, -1=ダイアログを出す)

*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void ConnectProc(int Type, int Num)
{
	int Save;
	int LFSort;
	int LDSort;
	int RFSort;
	int RDSort;

	SaveBookMark();
	SaveCurrentSetToHost();

	if((Num >= 0) || (SelectHost(Type) == YES))
	{
		if(Num >= 0)
			SetCurrentHost(Num);

		/* 接続中なら切断する */
		if(CmdCtrlSocket != INVALID_SOCKET)
			DisconnectProc();

		SetTaskMsg("----------------------------");

		InitPWDcommand();
		CopyHostFromList(AskCurrentHost(), &CurHost);
		// UTF-8対応
		CurHost.CurNameKanjiCode = CurHost.NameKanjiCode;
		// IPv6対応
		CurHost.CurNetType = CurHost.NetType;

		if(ConnectRas(CurHost.Dialup, CurHost.DialupAlways, CurHost.DialupNotify, CurHost.DialEntry) == FFFTP_SUCCESS)
		{
			SetHostKanaCnvImm(CurHost.KanaCnv);
			SetHostKanjiCodeImm(CurHost.KanjiCode);
			SetSyncMoveMode(CurHost.SyncMove);

			if((AskSaveSortToHost() == YES) && (CurHost.Sort != SORT_NOTSAVED))
			{
				DecomposeSortType(CurHost.Sort, &LFSort, &LDSort, &RFSort, &RDSort);
				SetSortTypeImm(LFSort, LDSort, RFSort, RDSort);
				ReSortDispList(WIN_LOCAL, &CancelFlg);
			}

			Save = NO;
			if(strlen(CurHost.PassWord) > 0)
				Save = YES;

			DisableUserOpe();
			// 暗号化通信対応
			// 同時接続対応
//			CmdCtrlSocket = DoConnect(CurHost.HostAdrs, CurHost.UserName, CurHost.PassWord, CurHost.Account, CurHost.Port, CurHost.FireWall, Save, CurHost.Security);
			CmdCtrlSocket = DoConnect(&CurHost, CurHost.HostAdrs, CurHost.UserName, CurHost.PassWord, CurHost.Account, CurHost.Port, CurHost.FireWall, Save, CurHost.Security, &CancelFlg);
			TrnCtrlSocket = CmdCtrlSocket;

			if(CmdCtrlSocket != INVALID_SOCKET)
			{
				// 暗号化通信対応
				switch(CurHost.CryptMode)
				{
				case CRYPT_NONE:
					if(CurHost.UseFTPIS != NO || CurHost.UseSFTP != NO)
					{
						if(DialogBox(GetFtpInst(), MAKEINTRESOURCE(savecrypt_dlg), GetMainHwnd(), ExeEscDialogProc) == YES)
							SetHostEncryption(AskCurrentHost(), CurHost.UseNoEncryption, CurHost.UseFTPES, NO, NO);
					}
					break;
				case CRYPT_FTPES:
					if(CurHost.UseNoEncryption != NO || CurHost.UseFTPIS != NO || CurHost.UseSFTP != NO)
					{
						if(DialogBox(GetFtpInst(), MAKEINTRESOURCE(savecrypt_dlg), GetMainHwnd(), ExeEscDialogProc) == YES)
							SetHostEncryption(AskCurrentHost(), NO, CurHost.UseFTPES, NO, NO);
					}
					break;
				case CRYPT_FTPIS:
					if(CurHost.UseNoEncryption != NO || CurHost.UseFTPES != NO || CurHost.UseSFTP != NO)
					{
						if(DialogBox(GetFtpInst(), MAKEINTRESOURCE(savecrypt_dlg), GetMainHwnd(), ExeEscDialogProc) == YES)
							SetHostEncryption(AskCurrentHost(), NO, NO, CurHost.UseFTPIS, NO);
					}
					break;
				case CRYPT_SFTP:
					if(CurHost.UseNoEncryption != NO || CurHost.UseFTPES != NO || CurHost.UseFTPIS != NO)
					{
						if(DialogBox(GetFtpInst(), MAKEINTRESOURCE(savecrypt_dlg), GetMainHwnd(), ExeEscDialogProc) == YES)
							SetHostEncryption(AskCurrentHost(), NO, NO, NO, CurHost.UseSFTP);
					}
					break;
				}

				// UTF-8対応
				if(CurHost.CurNameKanjiCode == KANJI_AUTO)
				{
					if(DoDirListCmdSkt("", "", 999, &CancelFlg) == FTP_COMPLETE)
						CurHost.CurNameKanjiCode = AnalyzeNameKanjiCode(999);
					switch(CurHost.CurNameKanjiCode)
					{
					case KANJI_SJIS:
						SetTaskMsg(MSGJPN343, MSGJPN345);
						break;
					case KANJI_JIS:
						SetTaskMsg(MSGJPN343, MSGJPN346);
						break;
					case KANJI_EUC:
						SetTaskMsg(MSGJPN343, MSGJPN347);
						break;
					case KANJI_UTF8N:
						SetTaskMsg(MSGJPN343, MSGJPN348);
						break;
					case KANJI_UTF8HFSX:
						SetTaskMsg(MSGJPN343, MSGJPN349);
						break;
					default:
						SetTaskMsg(MSGJPN344);
						break;
					}
				}

				strcpy(TitleHostName, CurHost.HostName);
				// タイトルバーにユーザー名表示対応
				strcpy(TitleUserName, CurHost.UserName);
				DispWindowTitle();
				UpdateStatusBar();
				SoundPlay(SND_CONNECT);

				SendInitCommand(CmdCtrlSocket, CurHost.InitCmd, &CancelFlg);

				if(strlen(CurHost.LocalInitDir) > 0)
				{
					DoLocalCWD(CurHost.LocalInitDir);
					GetLocalDirForWnd();
				}
				InitTransCurDir();
				DoCWD(CurHost.RemoteInitDir, YES, YES, YES);

				LoadBookMark();
				GetRemoteDirForWnd(CACHE_NORMAL, &CancelFlg);
			}
			else
				SoundPlay(SND_ERROR);

			EnableUserOpe();
		}
		else
			SetTaskMsg(MSGJPN001);
	}
	return;
}


/*----- ホスト名を入力してホストへ接続 ----------------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void QuickConnectProc(void)
{
	char Tmp[FMAX_PATH+1 + USER_NAME_LEN+1 + PASSWORD_LEN+1 + 2];
	char File[FMAX_PATH+1];

	SaveBookMark();
	SaveCurrentSetToHost();

	if(DialogBoxParam(GetFtpInst(), MAKEINTRESOURCE(hostname_dlg), GetMainHwnd(), QuickConDialogCallBack, (LPARAM)Tmp) == YES)
	{
		/* 接続中なら切断する */
		if(CmdCtrlSocket != INVALID_SOCKET)
			DisconnectProc();

		SetTaskMsg("----------------------------");

		InitPWDcommand();
		CopyDefaultHost(&CurHost);
		// UTF-8対応
		CurHost.CurNameKanjiCode = CurHost.NameKanjiCode;
		// IPv6対応
		CurHost.CurNetType = CurHost.NetType;
		if(SplitUNCpath(Tmp, CurHost.HostAdrs, CurHost.RemoteInitDir, File, CurHost.UserName, CurHost.PassWord, &CurHost.Port) == FFFTP_SUCCESS)
		{
			if(strlen(CurHost.UserName) == 0)
			{
				strcpy(CurHost.UserName, Tmp + FMAX_PATH+1);
				strcpy(CurHost.PassWord, Tmp + FMAX_PATH+1 + USER_NAME_LEN+1);
			}

			SetCurrentHost(HOSTNUM_NOENTRY);
			AskUseFireWall(CurHost.HostAdrs, &CurHost.FireWall, &CurHost.Pasv, &CurHost.ListCmdOnly);
			CurHost.FireWall = (int)Tmp[FMAX_PATH+1 + USER_NAME_LEN+1 + PASSWORD_LEN+1];
			CurHost.Pasv = (int)Tmp[FMAX_PATH+1 + USER_NAME_LEN+1 + PASSWORD_LEN+1 + 1];

			SetHostKanaCnvImm(CurHost.KanaCnv);
			SetHostKanjiCodeImm(CurHost.KanjiCode);
			SetSyncMoveMode(CurHost.SyncMove);

			DisableUserOpe();
			// 暗号化通信対応
			// 同時接続対応
//			CmdCtrlSocket = DoConnect(CurHost.HostAdrs, CurHost.UserName, CurHost.PassWord, CurHost.Account, CurHost.Port, CurHost.FireWall, NO, CurHost.Security);
			CmdCtrlSocket = DoConnect(&CurHost, CurHost.HostAdrs, CurHost.UserName, CurHost.PassWord, CurHost.Account, CurHost.Port, CurHost.FireWall, NO, CurHost.Security, &CancelFlg);
			TrnCtrlSocket = CmdCtrlSocket;

			if(CmdCtrlSocket != INVALID_SOCKET)
			{
				// UTF-8対応
				if(CurHost.CurNameKanjiCode == KANJI_AUTO)
				{
					if(DoDirListCmdSkt("", "", 999, &CancelFlg) == FTP_COMPLETE)
						CurHost.CurNameKanjiCode = AnalyzeNameKanjiCode(999);
					switch(CurHost.CurNameKanjiCode)
					{
					case KANJI_SJIS:
						SetTaskMsg(MSGJPN343, MSGJPN345);
						break;
					case KANJI_JIS:
						SetTaskMsg(MSGJPN343, MSGJPN346);
						break;
					case KANJI_EUC:
						SetTaskMsg(MSGJPN343, MSGJPN347);
						break;
					case KANJI_UTF8N:
						SetTaskMsg(MSGJPN343, MSGJPN348);
						break;
					case KANJI_UTF8HFSX:
						SetTaskMsg(MSGJPN343, MSGJPN349);
						break;
					default:
						SetTaskMsg(MSGJPN344);
						break;
					}
				}

				strcpy(TitleHostName, CurHost.HostAdrs);
				// タイトルバーにユーザー名表示対応
				strcpy(TitleUserName, CurHost.UserName);
				DispWindowTitle();
				UpdateStatusBar();
				SoundPlay(SND_CONNECT);

				InitTransCurDir();
				DoCWD(CurHost.RemoteInitDir, YES, YES, YES);

				GetRemoteDirForWnd(CACHE_NORMAL, &CancelFlg);
				EnableUserOpe();

				if(strlen(File) > 0)
					DirectDownloadProc(File);
			}
			else
			{
				SoundPlay(SND_ERROR);
				EnableUserOpe();
			}
		}
	}
	return;
}


/*----- クイック接続ダイアログのコールバック ----------------------------------
*
*	Parameter
*		HWND hDlg : ウインドウハンドル
*		UINT message : メッセージ番号
*		WPARAM wParam : メッセージの WPARAM 引数
*		LPARAM lParam : メッセージの LPARAM 引数
*
*	Return Value
*		BOOL TRUE/FALSE
*----------------------------------------------------------------------------*/

// 64ビット対応
//static BOOL CALLBACK QuickConDialogCallBack(HWND hDlg, UINT iMessage, WPARAM wParam, LPARAM lParam)
static INT_PTR CALLBACK QuickConDialogCallBack(HWND hDlg, UINT iMessage, WPARAM wParam, LPARAM lParam)
{
	static char *Buf;
	int i;
	HISTORYDATA Tmp;

//char Str[HOST_ADRS_LEN+USER_NAME_LEN+INIT_DIR_LEN+5+1];

	switch (iMessage)
	{
		case WM_INITDIALOG :
			SendDlgItemMessage(hDlg, QHOST_HOST, CB_LIMITTEXT, FMAX_PATH, 0);
			SendDlgItemMessage(hDlg, QHOST_HOST, WM_SETTEXT, 0, (LPARAM)"");
			SendDlgItemMessage(hDlg, QHOST_USER, EM_LIMITTEXT, USER_NAME_LEN, 0);
			if(QuickAnonymous == YES)
			{
				SendDlgItemMessage(hDlg, QHOST_USER, WM_SETTEXT, 0, (LPARAM)"anonymous");
				SendDlgItemMessage(hDlg, QHOST_PASS, WM_SETTEXT, 0, (LPARAM)UserMailAdrs);
			}
			else
			{
				SendDlgItemMessage(hDlg, QHOST_USER, WM_SETTEXT, 0, (LPARAM)"");
				SendDlgItemMessage(hDlg, QHOST_PASS, WM_SETTEXT, 0, (LPARAM)"");
			}
			SendDlgItemMessage(hDlg, QHOST_PASS, EM_LIMITTEXT, PASSWORD_LEN, 0);
			SendDlgItemMessage(hDlg, QHOST_FWALL, BM_SETCHECK, FwallDefault, 0);
			SendDlgItemMessage(hDlg, QHOST_PASV, BM_SETCHECK, PasvDefault, 0);
			for(i = 0; i < HISTORY_MAX; i++)
			{
				if(GetHistoryByNum(i, &Tmp) == FFFTP_SUCCESS)
				{
//sprintf(Str, "%s (%s) %s", Tmp.HostAdrs, Tmp.UserName, Tmp.RemoteInitDir);
//SendDlgItemMessage(hDlg, QHOST_HOST, CB_ADDSTRING, 0, (LPARAM)Str);
					SendDlgItemMessage(hDlg, QHOST_HOST, CB_ADDSTRING, 0, (LPARAM)Tmp.HostAdrs);
				}
			}
			Buf = (char *)lParam;
			return(TRUE);

		case WM_COMMAND :
			switch(GET_WM_COMMAND_ID(wParam, lParam))
			{
				case IDOK :
					SendDlgItemMessage(hDlg, QHOST_HOST, WM_GETTEXT, FMAX_PATH+1, (LPARAM)Buf);
					SendDlgItemMessage(hDlg, QHOST_USER, WM_GETTEXT, USER_NAME_LEN+1, (LPARAM)Buf + FMAX_PATH+1);
					SendDlgItemMessage(hDlg, QHOST_PASS, WM_GETTEXT, PASSWORD_LEN+1, (LPARAM)Buf + FMAX_PATH+1 + USER_NAME_LEN+1);
					*(Buf + FMAX_PATH+1 + USER_NAME_LEN+1 + PASSWORD_LEN+1) = (char)SendDlgItemMessage(hDlg, QHOST_FWALL, BM_GETCHECK, 0, 0);
					*(Buf + FMAX_PATH+1 + USER_NAME_LEN+1 + PASSWORD_LEN+1+1) = (char)SendDlgItemMessage(hDlg, QHOST_PASV, BM_GETCHECK, 0, 0);
					EndDialog(hDlg, YES);
					break;

				case IDCANCEL :
					EndDialog(hDlg, NO);
					break;

//				case QHOST_HOST :
//					if(HIWORD(wParam) == CBN_EDITCHANGE)
//						DoPrintf("EDIT");
//					break;
			}
			return(TRUE);
	}
	return(FALSE);
}


/*----- 指定したホスト名でホストへ接続 ----------------------------------------
*
*	Parameter
*		char *unc : UNC文字列
*		int Kanji : ホストの漢字コード (KANJI_xxx)
*		int Kana : 半角かな→全角変換モード (YES/NO)
*		int Fkanji : ファイル名の漢字コード (KANJI_xxx)
*		int TrMode : 転送モード (TYPE_xx)
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void DirectConnectProc(char *unc, int Kanji, int Kana, int Fkanji, int TrMode)
{
	char Host[HOST_ADRS_LEN+1];
	char Path[FMAX_PATH+1];
	char File[FMAX_PATH+1];
	char User[USER_NAME_LEN+1];
	char Pass[PASSWORD_LEN+1];
	int Port;

	SaveBookMark();
	SaveCurrentSetToHost();

	/* 接続中なら切断する */
	if(CmdCtrlSocket != INVALID_SOCKET)
		DisconnectProc();

	SetTaskMsg("----------------------------");

	InitPWDcommand();
	if(SplitUNCpath(unc, Host, Path, File, User, Pass, &Port) == FFFTP_SUCCESS)
	{
		if(strlen(User) == 0)
		{
			strcpy(User, "anonymous");
			strcpy(Pass, UserMailAdrs);
		}

		CopyDefaultHost(&CurHost);

		SetCurrentHost(HOSTNUM_NOENTRY);
		strcpy(CurHost.HostAdrs, Host);
		strcpy(CurHost.UserName, User);
		strcpy(CurHost.PassWord, Pass);
		strcpy(CurHost.RemoteInitDir, Path);
		AskUseFireWall(CurHost.HostAdrs, &CurHost.FireWall, &CurHost.Pasv, &CurHost.ListCmdOnly);
		CurHost.Port = Port;
		CurHost.KanjiCode = Kanji;
		CurHost.KanaCnv = Kana;
		CurHost.NameKanjiCode = Fkanji;
		CurHost.KanaCnv = YES;			/* とりあえず */
		// UTF-8対応
		CurHost.CurNameKanjiCode = CurHost.NameKanjiCode;
		// IPv6対応
		CurHost.CurNetType = CurHost.NetType;

		SetHostKanaCnvImm(CurHost.KanaCnv);
		SetHostKanjiCodeImm(CurHost.KanjiCode);
		SetSyncMoveMode(CurHost.SyncMove);

		if(TrMode != TYPE_DEFAULT)
		{
			SetTransferTypeImm(TrMode);
			DispTransferType();
		}

		DisableUserOpe();
		// 暗号化通信対応
		// 同時接続対応
//		CmdCtrlSocket = DoConnect(CurHost.HostAdrs, CurHost.UserName, CurHost.PassWord, CurHost.Account, CurHost.Port, CurHost.FireWall, NO, CurHost.Security);
		CmdCtrlSocket = DoConnect(&CurHost, CurHost.HostAdrs, CurHost.UserName, CurHost.PassWord, CurHost.Account, CurHost.Port, CurHost.FireWall, NO, CurHost.Security, &CancelFlg);
		TrnCtrlSocket = CmdCtrlSocket;

		if(CmdCtrlSocket != INVALID_SOCKET)
		{
			// UTF-8対応
			if(CurHost.CurNameKanjiCode == KANJI_AUTO)
			{
				if(DoDirListCmdSkt("", "", 999, &CancelFlg) == FTP_COMPLETE)
					CurHost.CurNameKanjiCode = AnalyzeNameKanjiCode(999);
				switch(CurHost.CurNameKanjiCode)
				{
				case KANJI_SJIS:
					SetTaskMsg(MSGJPN343, MSGJPN345);
					break;
				case KANJI_JIS:
					SetTaskMsg(MSGJPN343, MSGJPN346);
					break;
				case KANJI_EUC:
					SetTaskMsg(MSGJPN343, MSGJPN347);
					break;
				case KANJI_UTF8N:
					SetTaskMsg(MSGJPN343, MSGJPN348);
					break;
				case KANJI_UTF8HFSX:
					SetTaskMsg(MSGJPN343, MSGJPN349);
					break;
				default:
					SetTaskMsg(MSGJPN344);
					break;
				}
			}

			strcpy(TitleHostName, CurHost.HostAdrs);
			// タイトルバーにユーザー名表示対応
			strcpy(TitleUserName, CurHost.UserName);
			DispWindowTitle();
			UpdateStatusBar();
			SoundPlay(SND_CONNECT);

			InitTransCurDir();
			DoCWD(CurHost.RemoteInitDir, YES, YES, YES);

			GetRemoteDirForWnd(CACHE_NORMAL, &CancelFlg);
			EnableUserOpe();

			if(strlen(File) > 0)
				DirectDownloadProc(File);
			else
				ResetAutoExitFlg();
		}
		else
		{
			SoundPlay(SND_ERROR);
			EnableUserOpe();
		}
	}
	return;
}


/*----- ホストのヒストリで指定されたホストへ接続 ------------------------------
*
*	Parameter
*		int MenuCmd : 取り出すヒストリに割り当てられたメニューコマンド
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void HistoryConnectProc(int MenuCmd)
{
	HISTORYDATA Hist;
	int LFSort;
	int LDSort;
	int RFSort;
	int RDSort;

	if(GetHistoryByCmd(MenuCmd, &Hist) == FFFTP_SUCCESS)
	{
		SaveBookMark();
		SaveCurrentSetToHost();

		/* 接続中なら切断する */
		if(CmdCtrlSocket != INVALID_SOCKET)
			DisconnectProc();

		SetTaskMsg("----------------------------");

		InitPWDcommand();
		CopyHistoryToHost(&Hist, &CurHost);
		// UTF-8対応
		CurHost.CurNameKanjiCode = CurHost.NameKanjiCode;
		// IPv6対応
		CurHost.CurNetType = CurHost.NetType;

		if(ConnectRas(CurHost.Dialup, CurHost.DialupAlways, CurHost.DialupNotify, CurHost.DialEntry) == FFFTP_SUCCESS)
		{
			SetCurrentHost(HOSTNUM_NOENTRY);
			SetHostKanaCnvImm(CurHost.KanaCnv);
			SetHostKanjiCodeImm(CurHost.KanjiCode);
			SetSyncMoveMode(CurHost.SyncMove);

			DecomposeSortType(CurHost.Sort, &LFSort, &LDSort, &RFSort, &RDSort);
			SetSortTypeImm(LFSort, LDSort, RFSort, RDSort);
			ReSortDispList(WIN_LOCAL, &CancelFlg);

			SetTransferTypeImm(Hist.Type);
			DispTransferType();

			DisableUserOpe();
			// 暗号化通信対応
			// 同時接続対応
//			CmdCtrlSocket = DoConnect(CurHost.HostAdrs, CurHost.UserName, CurHost.PassWord, CurHost.Account, CurHost.Port, CurHost.FireWall, NO, CurHost.Security);
			CmdCtrlSocket = DoConnect(&CurHost, CurHost.HostAdrs, CurHost.UserName, CurHost.PassWord, CurHost.Account, CurHost.Port, CurHost.FireWall, NO, CurHost.Security, &CancelFlg);
			TrnCtrlSocket = CmdCtrlSocket;

			if(CmdCtrlSocket != INVALID_SOCKET)
			{
				// UTF-8対応
				if(CurHost.CurNameKanjiCode == KANJI_AUTO)
				{
					if(DoDirListCmdSkt("", "", 999, &CancelFlg) == FTP_COMPLETE)
						CurHost.CurNameKanjiCode = AnalyzeNameKanjiCode(999);
					switch(CurHost.CurNameKanjiCode)
					{
					case KANJI_SJIS:
						SetTaskMsg(MSGJPN343, MSGJPN345);
						break;
					case KANJI_JIS:
						SetTaskMsg(MSGJPN343, MSGJPN346);
						break;
					case KANJI_EUC:
						SetTaskMsg(MSGJPN343, MSGJPN347);
						break;
					case KANJI_UTF8N:
						SetTaskMsg(MSGJPN343, MSGJPN348);
						break;
					case KANJI_UTF8HFSX:
						SetTaskMsg(MSGJPN343, MSGJPN349);
						break;
					default:
						SetTaskMsg(MSGJPN344);
						break;
					}
				}

				strcpy(TitleHostName, CurHost.HostAdrs);
				// タイトルバーにユーザー名表示対応
				strcpy(TitleUserName, CurHost.UserName);
				DispWindowTitle();
				UpdateStatusBar();
				SoundPlay(SND_CONNECT);

				SendInitCommand(CmdCtrlSocket, CurHost.InitCmd, &CancelFlg);

				DoLocalCWD(CurHost.LocalInitDir);
				GetLocalDirForWnd();

				InitTransCurDir();
				DoCWD(CurHost.RemoteInitDir, YES, YES, YES);

				GetRemoteDirForWnd(CACHE_NORMAL, &CancelFlg);
			}
			else
				SoundPlay(SND_ERROR);

			EnableUserOpe();
		}
		else
			SetTaskMsg(MSGJPN002);
	}
	else
		SoundPlay(SND_ERROR);

	return;
}


/*----- ホストの初期化コマンドを送る ------------------------------------------
*
*	Parameter
*		int Cmd : 初期化コマンドス
*
*	Return Value
*		なし
*
*	NOte
*		初期化コマンドは以下のようなフォーマットであること
*			cmd1\0
*			cmd1\r\ncmd2\r\n\0
*----------------------------------------------------------------------------*/

// 同時接続対応
//static int SendInitCommand(char *Cmd)
static int SendInitCommand(SOCKET Socket, char *Cmd, int *CancelCheckWork)
{
	char Tmp[INITCMD_LEN+1];
	char *Pos;

	while(strlen(Cmd) > 0)
	{
		strcpy(Tmp, Cmd);
		if((Pos = strchr(Tmp, '\r')) != NULL)
			*Pos = NUL;
		if(strlen(Tmp) > 0)
//			DoQUOTE(Tmp);
			DoQUOTE(Socket, Tmp, CancelCheckWork);

		if((Cmd = strchr(Cmd, '\n')) != NULL)
			Cmd++;
		else
			break;
	}
	return(0);
}


/*----- 指定のホストはFireWallを使う設定かどうかを返す ------------------------
*
*	Parameter
*		char *Hots : ホスト名
*		int *Fire : FireWallを使うかどうかを返すワーク
*		int *Pasv : PASVモードを返すワーク
*		int *List : LISTコマンドのみ使用フラグ
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

static void AskUseFireWall(char *Host, int *Fire, int *Pasv, int *List)
{
	int i;
	HOSTDATA Tmp;

	*Fire = FwallDefault;
	*Pasv = PasvDefault;
	// NLSTを送ってしまうバグ修正（ただしNLSTを使うべきホストへクイック接続できなくなる）
//	*List = NO;

	i = 0;
	while(CopyHostFromList(i, &Tmp) == FFFTP_SUCCESS)
	{
		if(strcmp(Host, Tmp.HostAdrs) == 0)
		{
			*Fire = Tmp.FireWall;
			*Pasv = Tmp.Pasv;
			*List = Tmp.ListCmdOnly;
			break;
		}
		i++;
	}
	return;
}


/*----- 接続しているホストのアドレスを返す ------------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		char *ホストのアドレス
*----------------------------------------------------------------------------*/

char *AskHostAdrs(void)
{
	return(CurHost.HostAdrs);
}


/*----- 接続しているホストのポートを返す --------------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		int ホストのポート
*----------------------------------------------------------------------------*/

int AskHostPort(void)
{
	return(CurHost.Port);
}

/*----- 接続しているホストのファイル名の漢字コードを返す ----------------------
*
*	Parameter
*		なし
*
*	Return Value
*		int 漢字コード (KANJI_xxx)
*----------------------------------------------------------------------------*/

int AskHostNameKanji(void)
{
	// UTF-8対応
//	if(AskCurrentHost() != HOSTNUM_NOENTRY)
//		CopyHostFromListInConnect(AskCurrentHost(), &CurHost);
//
//	return(CurHost.NameKanjiCode);
	return(CurHost.CurNameKanjiCode);
}


/*----- 接続しているホストのファイル名の半角カナ変換フラグを返す --------------
*
*	Parameter
*		なし
*
*	Return Value
*		int 半角カナを全角に変換するかどうか (YES/NO)
*----------------------------------------------------------------------------*/

int AskHostNameKana(void)
{
	// 同時接続対応
	HOSTDATA TmpHost;
	TmpHost = CurHost;

	if(AskCurrentHost() != HOSTNUM_NOENTRY)
		// 同時接続対応
//		CopyHostFromListInConnect(AskCurrentHost(), &CurHost);
		CopyHostFromListInConnect(AskCurrentHost(), &TmpHost);

	// 同時接続対応
//	return(CurHost.NameKanaCnv);
	return(TmpHost.NameKanaCnv);
}


/*----- 接続しているホストのLISTコマンドモードを返す --------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		int LISTコマンドモード (YES/NO)
*----------------------------------------------------------------------------*/

int AskListCmdMode(void)
{
	// 同時接続対応
	HOSTDATA TmpHost;
	TmpHost = CurHost;

	if(CurHost.HostType == HTYPE_VMS)
		return(YES);
	else
	{
		if(AskCurrentHost() != HOSTNUM_NOENTRY)
			// 同時接続対応
//			CopyHostFromListInConnect(AskCurrentHost(), &CurHost);
			CopyHostFromListInConnect(AskCurrentHost(), &TmpHost);
		// 同時接続対応
//		return(CurHost.ListCmdOnly);
		return(TmpHost.ListCmdOnly);
	}
}


/*----- 接続しているホストでNLST -Rを使うかどうかを返す ------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		int NLST -Rを使うかどうか (YES/NO)
*----------------------------------------------------------------------------*/

int AskUseNLST_R(void)
{
	// 同時接続対応
	HOSTDATA TmpHost;
	TmpHost = CurHost;

	if(AskCurrentHost() != HOSTNUM_NOENTRY)
		// 同時接続対応
//		CopyHostFromListInConnect(AskCurrentHost(), &CurHost);
		CopyHostFromListInConnect(AskCurrentHost(), &TmpHost);

	// 同時接続対応
//	return(CurHost.UseNLST_R);
	return(TmpHost.UseNLST_R);
}


std::string AskHostChmodCmd() {
	HOSTDATA TmpHost = CurHost;
	if (AskCurrentHost() != HOSTNUM_NOENTRY)
		CopyHostFromListInConnect(AskCurrentHost(), &TmpHost);
	return TmpHost.ChmodCmd;
}


/*----- 接続しているホストのタイムゾーンを返す --------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		int タイムゾーン
*----------------------------------------------------------------------------*/

int AskHostTimeZone(void)
{
	// 同時接続対応
	HOSTDATA TmpHost;
	TmpHost = CurHost;

	if(AskCurrentHost() != HOSTNUM_NOENTRY)
		// 同時接続対応
//		CopyHostFromListInConnect(AskCurrentHost(), &CurHost);
		CopyHostFromListInConnect(AskCurrentHost(), &TmpHost);

	// 同時接続対応
//	return(CurHost.TimeZone);
	return(TmpHost.TimeZone);
}


/*----- 接続しているホストのPASVモードを返す ----------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		int PASVモードかどうか (YES/NO)
*----------------------------------------------------------------------------*/

int AskPasvMode(void)
{
	return(CurHost.Pasv);
}


std::string AskHostLsName() {
	HOSTDATA TmpHost = CurHost;
	if (AskCurrentHost() != HOSTNUM_NOENTRY)
		CopyHostFromListInConnect(AskCurrentHost(), &TmpHost);
	return TmpHost.LsName;
}


/*----- 接続しているホストのホストタイプを返す --------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		char *ファイル名／オプション
*----------------------------------------------------------------------------*/

int AskHostType(void)
{
	// 同時接続対応
	HOSTDATA TmpHost;
	TmpHost = CurHost;

	if(AskCurrentHost() != HOSTNUM_NOENTRY)
		// 同時接続対応
//		CopyHostFromListInConnect(AskCurrentHost(), &CurHost);
		CopyHostFromListInConnect(AskCurrentHost(), &TmpHost);

#if defined(HAVE_TANDEM)
	/* OSS ファイルシステムは UNIX ファイルシステムと同じでいいので AUTO を返す
	   ただし、Guardian ファイルシステムに戻ったときにおかしくならないように
	   CurHost.HostType 変数は更新しない */
	if(CurHost.HostType == HTYPE_TANDEM && Oss == YES)
		return(HTYPE_AUTO);
#endif

	// 同時接続対応
//	return(CurHost.HostType);
	return(TmpHost.HostType);
}


/*----- 接続しているホストはFireWallを使うホストかどうかを返す ----------------
*
*	Parameter
*		なし
*
*	Return Value
*		int FireWallを使うかどうか (YES/NO)
*----------------------------------------------------------------------------*/

int AskHostFireWall(void)
{
	return(CurHost.FireWall);
}


/*----- 接続しているホストでフルパスでファイルアクセスしないかどうかを返す ----
*
*	Parameter
*		なし
*
*	Return Value
*		int フルパスでアクセスしない (YES=フルパス禁止/NO)
*----------------------------------------------------------------------------*/

int AskNoFullPathMode(void)
{
	if(CurHost.HostType == HTYPE_VMS)
		return(YES);
	else
		return(CurHost.NoFullPath);
}


/*----- 接続しているユーザ名を返す --------------------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		char *ユーザ名
*----------------------------------------------------------------------------*/

char *AskHostUserName(void)
{
	return(CurHost.UserName);
}


/*----- 現在の設定をホストの設定にセットする ----------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		なし
*
*	Note
*		カレントディレクトリ、ソート方法をホストの設定にセットする
*----------------------------------------------------------------------------*/

void SaveCurrentSetToHost(void)
{
	int Host;
	char LocDir[FMAX_PATH+1];
	char HostDir[FMAX_PATH+1];
	// 同時接続対応
	HOSTDATA TmpHost;
	TmpHost = CurHost;

	if(TrnCtrlSocket != INVALID_SOCKET)
	{
		if((Host = AskCurrentHost()) != HOSTNUM_NOENTRY)
		{
			// 同時接続対応
//			CopyHostFromListInConnect(Host, &CurHost);
//			if(CurHost.LastDir == YES)
			CopyHostFromListInConnect(Host, &TmpHost);
			if(TmpHost.LastDir == YES)
			{
				AskLocalCurDir(LocDir, FMAX_PATH);
				AskRemoteCurDir(HostDir, FMAX_PATH);
				SetHostDir(AskCurrentHost(), LocDir, HostDir);
			}
			SetHostSort(AskCurrentHost(), AskSortType(ITEM_LFILE), AskSortType(ITEM_LDIR), AskSortType(ITEM_RFILE), AskSortType(ITEM_RDIR));
		}
	}
	return;
}


/*----- 現在の設定をヒストリにセットする --------------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

static void SaveCurrentSetToHistory(void)
{
	char LocDir[FMAX_PATH+1];
	char HostDir[FMAX_PATH+1];

	AskLocalCurDir(LocDir, FMAX_PATH);
	AskRemoteCurDir(HostDir, FMAX_PATH);
	strcpy(CurHost.LocalInitDir, LocDir);
	strcpy(CurHost.RemoteInitDir, HostDir);

	CurHost.Sort = AskSortType(ITEM_LFILE) * 0x1000000 | AskSortType(ITEM_LDIR) * 0x10000 | AskSortType(ITEM_RFILE) * 0x100 | AskSortType(ITEM_RDIR);

	CurHost.KanjiCode = AskHostKanjiCode();
	CurHost.KanaCnv = AskHostKanaCnv();

	CurHost.SyncMove = AskSyncMoveMode();

	AddHostToHistory(&CurHost, AskTransferType());
	SetAllHistoryToMenu();

	return;
}


/*----- コマンドコントロールソケットの再接続 ----------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		int ステータス
*			FFFTP_SUCCESS/FFFTP_FAIL
*----------------------------------------------------------------------------*/

int ReConnectCmdSkt(void)
{
	int Sts;


	// 同時接続対応
//	if(CmdCtrlSocket != TrnCtrlSocket)
//		do_closesocket(TrnCtrlSocket);
//	TrnCtrlSocket = INVALID_SOCKET;

	// 同時接続対応
//	Sts = ReConnectSkt(&CmdCtrlSocket);
	if(AskShareProh() == YES && AskTransferNow() == YES)
		SktShareProh();
	else
	{
		if(CmdCtrlSocket == TrnCtrlSocket)
			TrnCtrlSocket = INVALID_SOCKET;
		Sts = ReConnectSkt(&CmdCtrlSocket);
	}

	// 同時接続対応
//	TrnCtrlSocket = CmdCtrlSocket;
	if(TrnCtrlSocket == INVALID_SOCKET)
		TrnCtrlSocket = CmdCtrlSocket;

	return(Sts);
}


/*----- 転送コントロールソケットの再接続 --------------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		int ステータス
*			FFFTP_SUCCESS/FFFTP_FAIL
*----------------------------------------------------------------------------*/

//int ReConnectTrnSkt(void)
//{
//	return(ReConnectSkt(&TrnCtrlSocket));
//}
// 同時接続対応
int ReConnectTrnSkt(SOCKET *Skt, int *CancelCheckWork)
{
//	char Path[FMAX_PATH+1];
	int Sts;
	// 暗号化通信対応
	HOSTDATA HostData;

	Sts = FFFTP_FAIL;

	SetTaskMsg(MSGJPN003);

//	DisableUserOpe();
	/* 現在のソケットは切断 */
	if(*Skt != INVALID_SOCKET)
		do_closesocket(*Skt);
	/* 再接続 */
	// 暗号化通信対応
	HostData = CurHost;
	if(HostData.CryptMode != CRYPT_NONE)
		HostData.UseNoEncryption = NO;
	if(HostData.CryptMode != CRYPT_FTPES)
		HostData.UseFTPES = NO;
	if(HostData.CryptMode != CRYPT_FTPIS)
		HostData.UseFTPIS = NO;
	if(HostData.CryptMode != CRYPT_SFTP)
		HostData.UseSFTP = NO;
	// UTF-8対応
	HostData.CurNameKanjiCode = HostData.NameKanjiCode;
	// IPv6対応
	HostData.CurNetType = HostData.NetType;
	// 同時接続対応
	HostData.NoDisplayUI = YES;
	// 暗号化通信対応
	// 同時接続対応
//	if((*Skt = DoConnect(CurHost.HostAdrs, CurHost.UserName, CurHost.PassWord, CurHost.Account, CurHost.Port, CurHost.FireWall, NO, CurHost.Security)) != INVALID_SOCKET)
	if((*Skt = DoConnect(&HostData, CurHost.HostAdrs, CurHost.UserName, CurHost.PassWord, CurHost.Account, CurHost.Port, CurHost.FireWall, NO, CurHost.Security, CancelCheckWork)) != INVALID_SOCKET)
	{
		SendInitCommand(*Skt, CurHost.InitCmd, CancelCheckWork);
//		AskRemoteCurDir(Path, FMAX_PATH);
//		DoCWD(Path, YES, YES, YES);
		Sts = FFFTP_SUCCESS;
	}
	else
		SoundPlay(SND_ERROR);

//	EnableUserOpe();
	return(Sts);
}


/*----- 回線の再接続 ----------------------------------------------------------
*
*	Parameter
*		SOCKET *Skt : 接続したソケットを返すワーク
*
*	Return Value
*		int ステータス
*			FFFTP_SUCCESS/FFFTP_FAIL
*----------------------------------------------------------------------------*/

static int ReConnectSkt(SOCKET *Skt)
{
	char Path[FMAX_PATH+1];
	int Sts;

	Sts = FFFTP_FAIL;

	SetTaskMsg(MSGJPN003);

	DisableUserOpe();
	/* 現在のソケットは切断 */
	if(*Skt != INVALID_SOCKET)
		do_closesocket(*Skt);
	/* 再接続 */
	// 暗号化通信対応
	// 同時接続対応
//	if((*Skt = DoConnect(CurHost.HostAdrs, CurHost.UserName, CurHost.PassWord, CurHost.Account, CurHost.Port, CurHost.FireWall, NO, CurHost.Security)) != INVALID_SOCKET)
	if((*Skt = DoConnect(&CurHost, CurHost.HostAdrs, CurHost.UserName, CurHost.PassWord, CurHost.Account, CurHost.Port, CurHost.FireWall, NO, CurHost.Security, &CancelFlg)) != INVALID_SOCKET)
	{
		SendInitCommand(*Skt, CurHost.InitCmd, &CancelFlg);
		AskRemoteCurDir(Path, FMAX_PATH);
		DoCWD(Path, YES, YES, YES);
		Sts = FFFTP_SUCCESS;
	}
	else
		SoundPlay(SND_ERROR);

	EnableUserOpe();
	return(Sts);
}


/*----- コマンドコントロールソケットを返す ------------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		SOCKET コマンドコントロールソケット
*----------------------------------------------------------------------------*/

SOCKET AskCmdCtrlSkt(void)
{
	return(CmdCtrlSocket);
}


/*----- 転送コントロールソケットを返す ----------------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		SOCKET 転送コントロールソケット
*----------------------------------------------------------------------------*/

SOCKET AskTrnCtrlSkt(void)
{
	return(TrnCtrlSocket);
}


/*----- コマンド／転送コントロールソケットの共有を解除 ------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void SktShareProh(void)
{
	if(CmdCtrlSocket == TrnCtrlSocket)
	{

//SetTaskMsg("############### SktShareProh");

		// 同時接続対応
//		CmdCtrlSocket = INVALID_SOCKET;
//		ReConnectSkt(&CmdCtrlSocket);
		if(CurHost.ReuseCmdSkt == YES)
		{
			CurHost.ReuseCmdSkt = NO;
			CmdCtrlSocket = INVALID_SOCKET;
			ReConnectSkt(&CmdCtrlSocket);
		}
	}
	return;
}


/*----- コマンド／転送コントロールソケットの共有が解除されているかチェック ----
*
*	Parameter
*		なし
*
*	Return Value
*		int ステータス
*			YES=共有解除/NO=共有
*----------------------------------------------------------------------------*/

int AskShareProh(void)
{
	int Sts;

	Sts = YES;
	// 同時接続対応
//	if(CmdCtrlSocket == TrnCtrlSocket)
	if(CmdCtrlSocket == TrnCtrlSocket || TrnCtrlSocket == INVALID_SOCKET)
		Sts = NO;

	return(Sts);
}


/*----- ホストから切断 --------------------------------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void DisconnectProc(void)
{

//SetTaskMsg("############### Disconnect Cmd=%x, Trn=%x", CmdCtrlSocket,TrnCtrlSocket);

	// 同時接続対応
	AbortAllTransfer();

	if((CmdCtrlSocket != INVALID_SOCKET) && (CmdCtrlSocket != TrnCtrlSocket))
	{
		// 同時接続対応
//		DoQUIT(CmdCtrlSocket);
		DoQUIT(CmdCtrlSocket, &CancelFlg);
		DoClose(CmdCtrlSocket);
	}

	if(TrnCtrlSocket != INVALID_SOCKET)
	{
		// 同時接続対応
//		DoQUIT(TrnCtrlSocket);
		DoQUIT(TrnCtrlSocket, &CancelFlg);
		DoClose(TrnCtrlSocket);

		SaveCurrentSetToHistory();

		EraseRemoteDirForWnd();
		SetTaskMsg(MSGJPN004);
	}

	TrnCtrlSocket = INVALID_SOCKET;
	CmdCtrlSocket = INVALID_SOCKET;

	DispWindowTitle();
	UpdateStatusBar();
	MakeButtonsFocus();
	ClearBookMark();

	return;
}


/*----- ソケットが強制切断されたときの処理 ------------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void DisconnectSet(void)
{
	CmdCtrlSocket = INVALID_SOCKET;
	TrnCtrlSocket = INVALID_SOCKET;

	EraseRemoteDirForWnd();
	DispWindowTitle();
	UpdateStatusBar();
	MakeButtonsFocus();
	SetTaskMsg(MSGJPN005);
	return;
}


/*----- ホストに接続中かどうかを返す ------------------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		int ステータス (YES/NO)
*----------------------------------------------------------------------------*/

int AskConnecting(void)
{
	int Sts;

	Sts = NO;
	if(TrnCtrlSocket != INVALID_SOCKET)
		Sts = YES;

	return(Sts);
}


#if defined(HAVE_TANDEM)
/*----- 接続している本当のホストのホストタイプを返す --------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		char *ファイル名／オプション
*----------------------------------------------------------------------------*/

int AskRealHostType(void)
{
	// 同時接続対応
	HOSTDATA TmpHost;
	TmpHost = CurHost;

	if(AskCurrentHost() != HOSTNUM_NOENTRY)
		// 同時接続対応
//		CopyHostFromListInConnect(AskCurrentHost(), &CurHost);
		CopyHostFromListInConnect(AskCurrentHost(), &TmpHost);

	// 同時接続対応
//	return(CurHost.HostType);
	return(TmpHost.HostType);
}

/*----- OSS ファイルシステムにアクセスしているかどうかのフラグを変更する ------
*
*	Parameter
*		int ステータス (YES/NO)
*
*	Return Value
*		int ステータス (YES/NO)
*----------------------------------------------------------------------------*/

int SetOSS(int wkOss)
{
	if(Oss != wkOss) {
		if (wkOss == YES) {
			strcpy(CurHost.InitCmd, "OSS");
		} else {
			strcpy(CurHost.InitCmd, "GUARDIAN");
		}
	}
	Oss = wkOss;
	return(Oss);
}

/*----- OSS ファイルシステムにアクセスしているかどうかを返す ------------------
*
*	Parameter
*		なし
*
*	Return Value
*		int ステータス (YES/NO)
*----------------------------------------------------------------------------*/

int AskOSS(void)
{
	return(Oss);
}
#endif /* HAVE_TANDEM */


/*----- ホストへ接続する ------------------------------------------------------
*
*	Parameter
*		char *Host : ホスト名
*		char *User : ユーザ名
*		char *Pass : パスワード
*		char *Acct : アカウント
*		int Port : ポート
*		int Fwall : FireWallを使うかどうか (YES/NO)
*		int SavePass : パスワードを再入力した時に保存するかどうか (YES/NO)
*		int Security : セキュリティ (SECURITY_xxx, MDx)
*
*	Return Value
*		SOCKET ソケット
*
*	Note
*		ホスト名、ユーザ名、パスワードが指定されていなかったときは、接続に使用
*		したものをコピーしてかえす
*			char *Host : ホスト名
*			char *User : ユーザ名
*			char *Pass : パスワード
*			char *Acct : アカウント
*
*		FireWallは次のように動作する
*			TYPE1	Connect fire → USER user(f) → PASS pass(f) → SITE host → USER user(h) →      PASS pass(h) → ACCT acct
*			TYPE2	Connect fire → USER user(f) → PASS pass(f) →              USER user(h)@host → PASS pass(h) → ACCT acct
*			TYPE3	Connect fire →                                              USER user(h)@host → PASS pass(h) → ACCT acct
*			TYPE4	Connect fire →                                 OPEN host → USER user(h) →      PASS pass(h) → ACCT acct
*			TYPE5	SOCKS4
*			none	Connect host →                                              USER user(h) →      PASS pass(h) → ACCT acct
*----------------------------------------------------------------------------*/

// 暗号化通信対応
static SOCKET DoConnectCrypt(int CryptMode, HOSTDATA* HostData, char *Host, char *User, char *Pass, char *Acct, int Port, int Fwall, int SavePass, int Security, int *CancelCheckWork)
{
	int Sts;
	int Flg;
	int Anony;
	SOCKET ContSock;
	char Buf[1024];
	char Reply[1024];
	int Continue;
	int ReInPass;
	char *Tmp;
	int HostPort;
	static const char *SiteTbl[4] = { "SITE", "site", "OPEN", "open" };
	char TmpBuf[ONELINE_BUF_SIZE];
	struct linger LingerOpt;
	struct tcp_keepalive KeepAlive;
	DWORD dwTmp;

	// 暗号化通信対応
	ContSock = INVALID_SOCKET;

	if(CryptMode == CRYPT_NONE || CryptMode == CRYPT_FTPES || CryptMode == CRYPT_FTPIS)
	{
		if(Fwall == YES)
			Fwall = FwallType;
		else
			Fwall = FWALL_NONE;

		TryConnect = YES;
		// 暗号化通信対応
//		CancelFlg = NO;
#if 0
//		WSASetBlockingHook(BlkHookFnc);
#endif

		ContSock = INVALID_SOCKET;

		HostPort = Port;
		Tmp = Host;
		if(((Fwall >= FWALL_FU_FP_SITE) && (Fwall <= FWALL_OPEN)) ||
		   (Fwall == FWALL_SIDEWINDER) ||
		   (Fwall == FWALL_FU_FP))
		{
			Tmp = FwallHost;
			Port = FwallPort;
		}

		if(strlen(Tmp) != 0)
		{
			// 同時接続対応
//			if((ContSock = connectsock(Tmp, Port, "", &CancelFlg)) != INVALID_SOCKET)
			if((ContSock = connectsock(Tmp, Port, "", CancelCheckWork)) != INVALID_SOCKET)
			{
				// バッファを無効
#ifdef DISABLE_CONTROL_NETWORK_BUFFERS
				int BufferSize = 0;
				setsockopt(ContSock, SOL_SOCKET, SO_SNDBUF, (char*)&BufferSize, sizeof(int));
				setsockopt(ContSock, SOL_SOCKET, SO_RCVBUF, (char*)&BufferSize, sizeof(int));
#endif
				// FTPIS対応
//				while((Sts = ReadReplyMessage(ContSock, Buf, 1024, &CancelFlg, TmpBuf) / 100) == FTP_PRELIM)
//					;
				if(CryptMode == CRYPT_FTPIS)
				{
					if(AttachSSL(ContSock, INVALID_SOCKET, CancelCheckWork, Host))
					{
						while((Sts = ReadReplyMessage(ContSock, Buf, 1024, CancelCheckWork, TmpBuf) / 100) == FTP_PRELIM)
							;
					}
					else
						Sts = FTP_ERROR;
				}
				else
				{
					while((Sts = ReadReplyMessage(ContSock, Buf, 1024, CancelCheckWork, TmpBuf) / 100) == FTP_PRELIM)
						;
				}

				if(Sts == FTP_COMPLETE)
				{
					Flg = 1;
					if(setsockopt(ContSock, SOL_SOCKET, SO_OOBINLINE, (LPSTR)&Flg, sizeof(Flg)) == SOCKET_ERROR)
						ReportWSError("setsockopt", WSAGetLastError());
					// データ転送用ソケットのTCP遅延転送が無効されているので念のため
					if(setsockopt(ContSock, IPPROTO_TCP, TCP_NODELAY, (LPSTR)&Flg, sizeof(Flg)) == SOCKET_ERROR)
						ReportWSError("setsockopt", WSAGetLastError());
//#pragma aaa
					Flg = 1;
					if(setsockopt(ContSock, SOL_SOCKET, SO_KEEPALIVE, (LPSTR)&Flg, sizeof(Flg)) == SOCKET_ERROR)
						ReportWSError("setsockopt", WSAGetLastError());
					// 切断対策
					if(TimeOut > 0)
					{
						KeepAlive.onoff = 1;
						KeepAlive.keepalivetime = TimeOut * 1000;
						KeepAlive.keepaliveinterval = 1000;
						if(WSAIoctl(ContSock, SIO_KEEPALIVE_VALS, &KeepAlive, sizeof(struct tcp_keepalive), NULL, 0, &dwTmp, NULL, NULL) == SOCKET_ERROR)
							ReportWSError("WSAIoctl", WSAGetLastError());
					}
					LingerOpt.l_onoff = 1;
					LingerOpt.l_linger = 90;
					if(setsockopt(ContSock, SOL_SOCKET, SO_LINGER, (LPSTR)&LingerOpt, sizeof(LingerOpt)) == SOCKET_ERROR)
						ReportWSError("setsockopt", WSAGetLastError());
///////


					/*===== 認証を行なう =====*/

					Sts = FTP_COMPLETE;
					if((Fwall == FWALL_FU_FP_SITE) ||
					   (Fwall == FWALL_FU_FP_USER) ||
					   (Fwall == FWALL_FU_FP))
					{
						// 同時接続対応
//						if((Sts = command(ContSock, Reply, &CancelFlg, "USER %s", FwallUser) / 100) == FTP_CONTINUE)
						if((Sts = command(ContSock, Reply, CancelCheckWork, "USER %s", FwallUser) / 100) == FTP_CONTINUE)
						{
							CheckOneTimePassword(FwallPass, Reply, FwallSecurity);
							// 同時接続対応
//							Sts = command(ContSock, NULL, &CancelFlg, "PASS %s", Reply) / 100;
							Sts = command(ContSock, NULL, CancelCheckWork, "PASS %s", Reply) / 100;
						}
					}
					else if(Fwall == FWALL_SIDEWINDER)
					{
						// 同時接続対応
//						Sts = command(ContSock, Reply, &CancelFlg, "USER %s:%s%c%s", FwallUser, FwallPass, FwallDelimiter, Host) / 100;
						Sts = command(ContSock, Reply, CancelCheckWork, "USER %s:%s%c%s", FwallUser, FwallPass, FwallDelimiter, Host) / 100;
					}
					if((Sts != FTP_COMPLETE) && (Sts != FTP_CONTINUE))
					{
						SetTaskMsg(MSGJPN006);
						DoClose(ContSock);
						ContSock = INVALID_SOCKET;
					}
					else
					{
						if((Fwall == FWALL_FU_FP_SITE) || (Fwall == FWALL_OPEN))
						{
							Flg = 0;
							if(Fwall == FWALL_OPEN)
								Flg = 2;
							if(FwallLower == YES)
								Flg++;

							if(HostPort == PORT_NOR)
								// 同時接続対応
//								Sts = command(ContSock, NULL, &CancelFlg, "%s %s", SiteTbl[Flg], Host) / 100;
								Sts = command(ContSock, NULL, CancelCheckWork, "%s %s", SiteTbl[Flg], Host) / 100;
							else
								// 同時接続対応
//								Sts = command(ContSock, NULL, &CancelFlg, "%s %s %d", SiteTbl[Flg], Host, HostPort) / 100;
								Sts = command(ContSock, NULL, CancelCheckWork, "%s %s %d", SiteTbl[Flg], Host, HostPort) / 100;
						}

						if((Sts != FTP_COMPLETE) && (Sts != FTP_CONTINUE))
						{
							SetTaskMsg(MSGJPN007, Host);
							DoClose(ContSock);
							ContSock = INVALID_SOCKET;
						}
						else
						{
							Anony = NO;
							// 同時接続対応
//							if((strlen(User) != 0) || 
//							   (InputDialogBox(username_dlg, GetMainHwnd(), NULL, User, USER_NAME_LEN+1, &Anony, IDH_HELP_TOPIC_0000001) == YES))
							if((strlen(User) != 0) || 
							   ((HostData->NoDisplayUI == NO) && (InputDialogBox(username_dlg, GetMainHwnd(), NULL, User, USER_NAME_LEN+1, &Anony, IDH_HELP_TOPIC_0000001) == YES)))
							{
								if(Anony == YES)
								{
									strcpy(User, "anonymous");
									strcpy(Pass, UserMailAdrs);
								}

								if((Fwall == FWALL_FU_FP_USER) || (Fwall == FWALL_USER))
								{
									if(HostPort == PORT_NOR)
										sprintf(Buf, "%s%c%s", User, FwallDelimiter, Host);
									else
										sprintf(Buf, "%s%c%s %d", User, FwallDelimiter, Host, HostPort);
								}
								else
									strcpy(Buf, User);

								// FTPES対応
								if(CryptMode == CRYPT_FTPES)
								{
									if(((Sts = command(ContSock, Reply, CancelCheckWork, "AUTH TLS")) == 234 || (Sts = command(ContSock, Reply, CancelCheckWork, "AUTH SSL")) == 234))
									{
										if(AttachSSL(ContSock, INVALID_SOCKET, CancelCheckWork, Host))
										{
											if((Sts = command(ContSock, Reply, CancelCheckWork, "PBSZ 0")) == 200)
											{
												if((Sts = command(ContSock, Reply, CancelCheckWork, "PROT P")) == 200)
												{
												}
												else
													Sts = FTP_ERROR;
											}
											else
												Sts = FTP_ERROR;
										}
										else
											Sts = FTP_ERROR;
									}
									else
										Sts = FTP_ERROR;
								}

								// FTPIS対応
								// "PBSZ 0"と"PROT P"は黙示的に設定されているはずだが念のため
								if(CryptMode == CRYPT_FTPIS)
								{
									if((Sts = command(ContSock, Reply, CancelCheckWork, "PBSZ 0")) == 200)
									{
										if((Sts = command(ContSock, Reply, CancelCheckWork, "PROT P")) == 200)
										{
										}
									}
								}

								ReInPass = NO;
								do
								{
									// FTPES対応
									if(Sts == FTP_ERROR)
										break;
									Continue = NO;
									// 同時接続対応
//									if((Sts = command(ContSock, Reply, &CancelFlg, "USER %s", Buf) / 100) == FTP_CONTINUE)
									if((Sts = command(ContSock, Reply, CancelCheckWork, "USER %s", Buf) / 100) == FTP_CONTINUE)
									{
										// 同時接続対応
//										if((strlen(Pass) != 0) || 
//										   (InputDialogBox(passwd_dlg, GetMainHwnd(), NULL, Pass, PASSWORD_LEN+1, &Anony, IDH_HELP_TOPIC_0000001) == YES))
										if((strlen(Pass) != 0) || 
										   ((HostData->NoDisplayUI == NO) && (InputDialogBox(passwd_dlg, GetMainHwnd(), NULL, Pass, PASSWORD_LEN+1, &Anony, IDH_HELP_TOPIC_0000001) == YES)))
										{
											CheckOneTimePassword(Pass, Reply, Security);

											/* パスワードがスペース1個の時はパスワードの実体なしとする */
											if(strcmp(Reply, " ") == 0)
												strcpy(Reply, "");

											// 同時接続対応
//											Sts = command(ContSock, NULL, &CancelFlg, "PASS %s", Reply) / 100;
											Sts = command(ContSock, NULL, CancelCheckWork, "PASS %s", Reply) / 100;
											if(Sts == FTP_ERROR)
											{
												strcpy(Pass, "");
												// 同時接続対応
//												if(InputDialogBox(re_passwd_dlg, GetMainHwnd(), NULL, Pass, PASSWORD_LEN+1, &Anony, IDH_HELP_TOPIC_0000001) == YES)
												if(HostData->NoDisplayUI == NO && InputDialogBox(re_passwd_dlg, GetMainHwnd(), NULL, Pass, PASSWORD_LEN+1, &Anony, IDH_HELP_TOPIC_0000001) == YES)
													Continue = YES;
												else
													DoPrintf("No password specified.");
												ReInPass = YES;
											}
											else if(Sts == FTP_CONTINUE)
											{
												// 同時接続対応
//												if((strlen(Acct) != 0) || 
//												   (InputDialogBox(account_dlg, GetMainHwnd(), NULL, Acct, ACCOUNT_LEN+1, &Anony, IDH_HELP_TOPIC_0000001) == YES))
												if((strlen(Acct) != 0) || 
												   ((HostData->NoDisplayUI == NO) && (InputDialogBox(account_dlg, GetMainHwnd(), NULL, Acct, ACCOUNT_LEN+1, &Anony, IDH_HELP_TOPIC_0000001) == YES)))
												{
													// 同時接続対応
//													Sts = command(ContSock, NULL, &CancelFlg, "ACCT %s", Acct) / 100;
													Sts = command(ContSock, NULL, CancelCheckWork, "ACCT %s", Acct) / 100;
												}
												else
													DoPrintf("No account specified");
											}
										}
										else
										{
											Sts = FTP_ERROR;
											DoPrintf("No password specified.");
										}
									}
									// FTPES対応
									if(Continue == YES)
										Sts = FTP_COMPLETE;
								}
								while(Continue == YES);
							}
							else
							{
								Sts = FTP_ERROR;
								DoPrintf("No user name specified");
							}

							if(Sts != FTP_COMPLETE)
							{
								SetTaskMsg(MSGJPN008);
								DoClose(ContSock);
								ContSock = INVALID_SOCKET;
							}
							else if((SavePass == YES) && (ReInPass == YES))
							{
								// 同時接続対応
//								if(DialogBox(GetFtpInst(), MAKEINTRESOURCE(savepass_dlg), GetMainHwnd(), ExeEscDialogProc) == YES)
								if(HostData->NoDisplayUI == NO && DialogBox(GetFtpInst(), MAKEINTRESOURCE(savepass_dlg), GetMainHwnd(), ExeEscDialogProc) == YES)
									SetHostPassword(AskCurrentHost(), Pass);
							}
						}
					}
				}
				else
				{
//#pragma aaa
					SetTaskMsg(MSGJPN009/*"接続できません(1) %x", ContSock*/);
					DoClose(ContSock);
					ContSock = INVALID_SOCKET;
				}
			}
		}
		else
		{

			if(((Fwall >= FWALL_FU_FP_SITE) && (Fwall <= FWALL_OPEN)) ||
			   (Fwall == FWALL_FU_FP))
				SetTaskMsg(MSGJPN010);
			else
				SetTaskMsg(MSGJPN011);
		}

#if 0
//		WSAUnhookBlockingHook();
#endif
		TryConnect = NO;

		// FEAT対応
		// ホストの機能を確認
		if(ContSock != INVALID_SOCKET)
		{
			if((Sts = command(ContSock, Reply, CancelCheckWork, "FEAT")) == 211)
			{
				// 改行文字はReadReplyMessageで消去されるため区切り文字に空白を使用
				// UTF-8対応
				if(strstr(Reply, " UTF8 "))
					HostData->Feature |= FEATURE_UTF8;
				// MLST対応
				if(strstr(Reply, " MLST ") || strstr(Reply, " MLSD "))
					HostData->Feature |= FEATURE_MLSD;
				// IPv6対応
				if(strstr(Reply, " EPRT ") || strstr(Reply, " EPSV "))
					HostData->Feature |= FEATURE_EPRT | FEATURE_EPSV;
				// ホスト側の日時取得
				if(strstr(Reply, " MDTM "))
					HostData->Feature |= FEATURE_MDTM;
				// ホスト側の日時設定
				if(strstr(Reply, " MFMT "))
					HostData->Feature |= FEATURE_MFMT;
			}
			// UTF-8対応
			if(HostData->CurNameKanjiCode == KANJI_AUTO && (HostData->Feature & FEATURE_UTF8))
			{
				// UTF-8を指定した場合も自動判別を行う
//				if((Sts = command(ContSock, Reply, CancelCheckWork, "OPTS UTF8 ON")) == 200)
//					HostData->CurNameKanjiCode = KANJI_UTF8N;
				command(ContSock, Reply, CancelCheckWork, "OPTS UTF8 ON");
			}
		}
	}

	return(ContSock);
}

// 同時接続対応
//static SOCKET DoConnect(HOSTDATA* HostData, char *Host, char *User, char *Pass, char *Acct, int Port, int Fwall, int SavePass, int Security)
static SOCKET DoConnect(HOSTDATA* HostData, char *Host, char *User, char *Pass, char *Acct, int Port, int Fwall, int SavePass, int Security, int *CancelCheckWork)
{
	SOCKET ContSock;
	ContSock = INVALID_SOCKET;
	*CancelCheckWork = NO;
	if(*CancelCheckWork == NO && ContSock == INVALID_SOCKET && HostData->UseFTPIS == YES)
	{
		SetTaskMsg(MSGJPN316);
		if((ContSock = DoConnectCrypt(CRYPT_FTPIS, HostData, Host, User, Pass, Acct, Port, Fwall, SavePass, Security, CancelCheckWork)) != INVALID_SOCKET)
			HostData->CryptMode = CRYPT_FTPIS;
	}
	if(*CancelCheckWork == NO && ContSock == INVALID_SOCKET && HostData->UseFTPES == YES)
	{
		SetTaskMsg(MSGJPN315);
		if((ContSock = DoConnectCrypt(CRYPT_FTPES, HostData, Host, User, Pass, Acct, Port, Fwall, SavePass, Security, CancelCheckWork)) != INVALID_SOCKET)
			HostData->CryptMode = CRYPT_FTPES;
	}
	if(*CancelCheckWork == NO && ContSock == INVALID_SOCKET && HostData->UseNoEncryption == YES)
	{
		SetTaskMsg(MSGJPN314);
		if((ContSock = DoConnectCrypt(CRYPT_NONE, HostData, Host, User, Pass, Acct, Port, Fwall, SavePass, Security, CancelCheckWork)) != INVALID_SOCKET)
			HostData->CryptMode = CRYPT_NONE;
	}
	return ContSock;
}


/*----- ワンタイムパスワードのチェック ----------------------------------------
*
*	Parameter
*		chat *Pass : パスワード／パスフレーズ
*		char *Reply : USERコマンドを送ったあとのリプライ文字列
*						／PASSコマンドで送るパスワードを返すバッファ
*		int Type : タイプ (SECURITY_xxx, MDx)
*
*	Return Value
*		int ステータス
*			FFFTP_SUCCESS/FFFTP_FAIL
*
*	Note
*		ワンタイムパスワードでない時はPassをそのままReplyにコピー
*----------------------------------------------------------------------------*/

static int CheckOneTimePassword(char *Pass, char *Reply, int Type)
{
	int Sts;
	char *Pos;
	int Seq;
	char Seed[MAX_SEED_LEN+1];
	int i;

	Sts = FFFTP_SUCCESS;
	Pos = NULL;

	if(Type == SECURITY_AUTO)
	{
		if((Pos = stristr(Reply, "otp-md5")) != NULL)
		{
			Type = MD5;
			SetTaskMsg(MSGJPN012);
		}
		else if((Pos = stristr(Reply, "otp-sha1")) != NULL)
		{
			Type = SHA1;
			SetTaskMsg(MSGJPN013);
		}
		else if(((Pos = stristr(Reply, "otp-md4")) != NULL) || ((Pos = stristr(Reply, "s/key")) != NULL))
		{
			Type = MD4;
			SetTaskMsg(MSGJPN014);
		}
	}
	else
		Pos = GetNextField(Reply);

	if((Type == MD4) || (Type == MD5) || (Type == SHA1))
	{
		/* シーケンス番号を見つけるループ */
		DoPrintf("Analyze OTP");
		DoPrintf("%s", Pos);
		Sts = FFFTP_FAIL;
		while((Pos = GetNextField(Pos)) != NULL)
		{
			if(IsDigit(*Pos))
			{
				Seq = atoi(Pos);
				DoPrintf("Sequence=%d", Seq);

				/* Seed */
				if((Pos = GetNextField(Pos)) != NULL)
				{
					if(GetOneField(Pos, Seed, MAX_SEED_LEN) == FFFTP_SUCCESS)
					{
						/* Seedは英数字のみ有効とする */
						for(i = (int)strlen(Seed)-1; i >= 0; i--)
						{
							if((IsAlpha(Seed[i]) == 0) && (IsDigit(Seed[i]) == 0))
								Seed[i] = NUL;
						}
						if(strlen(Seed) > 0)
						{
							DoPrintf("Seed=%s", Seed);
							Make6WordPass(Seq, Seed, Pass, Type, Reply);
							DoPrintf("Response=%s", Reply);

							/* シーケンス番号のチェックと警告 */
							if(Seq <= 10)
								DialogBox(GetFtpInst(), MAKEINTRESOURCE(otp_notify_dlg), GetMainHwnd(), ExeEscDialogProc);

							Sts = FFFTP_SUCCESS;
						}
					}
				}
				break;
			}
		}

		if(Sts == FFFTP_FAIL)
			SetTaskMsg(MSGJPN015);
	}
	else
	{
		strcpy(Reply, Pass);
		DoPrintf("No OTP used.");
	}
	return(Sts);
}


namespace std {
	template<>
	struct default_delete<addrinfoW> {
		void operator()(addrinfoW* ai) const noexcept {
			FreeAddrInfoW(ai);
		}
	};
}

static inline auto getaddrinfo(std::wstring const& host, std::wstring const& port, int family = AF_UNSPEC, int flags = AI_NUMERICHOST | AI_NUMERICSERV) {
	if (addrinfoW hint{ flags, family }, *ai; GetAddrInfoW(host.c_str(), port.c_str(), &hint, &ai) == 0) {
		assert(family == AF_INET || family == AF_INET6 ? ai->ai_family == family : true);
		assert(ai->ai_family == AF_INET && ai->ai_addrlen == sizeof(sockaddr_in) || ai->ai_family == AF_INET6 && ai->ai_addrlen == sizeof(sockaddr_in6));
		assert(ai->ai_addr->sa_family == ai->ai_family);
		return std::unique_ptr<addrinfoW>{ ai };
	}
	return std::unique_ptr<addrinfoW>{};
}

static inline auto getaddrinfo(std::wstring const& host, int port, int family = AF_UNSPEC, int flags = AI_NUMERICHOST | AI_NUMERICSERV) {
	return getaddrinfo(host, std::to_wstring(port), family, flags);
}

static std::unique_ptr<addrinfoW> getaddrinfo(std::wstring const& host, std::wstring const& port, int family, int* CancelCheckWork) {
	auto future = std::async(std::launch::async, [host, port, family] {
		std::wstring idn;
		if (SupportIdn) {
			auto length = IdnToAscii(0, data(host), size_as<int>(host), nullptr, 0);
			idn.resize(length);
			auto result = IdnToAscii(0, data(host), size_as<int>(host), data(idn), length);
			assert(result == length);
		} else
			idn = host;
		return getaddrinfo(idn, port, family, AI_NUMERICSERV);
	});
	while (*CancelCheckWork == NO && future.wait_for(1ms) == std::future_status::timeout)
		if (BackgrndMessageProc() == YES)
			*CancelCheckWork = YES;
	if (*CancelCheckWork == YES)
		return {};
	return future.get();
}


static inline auto getaddrinfo(std::wstring const& host, int port, int family, int* CancelCheckWork) {
	return getaddrinfo(host, std::to_wstring(port), family, CancelCheckWork);
}


static SOCKET connectsock(ADDRESS_FAMILY family, char *host, int port, char *PreMsg, int *CancelCheckWork);

typedef struct
{
	HANDLE h;
	DWORD ExitCode;
	ADDRESS_FAMILY family;
	char *host;
	int port;
	char *PreMsg;
	int CancelCheckWork;
	SOCKET s;
} CONNECTSOCKDATA;

DWORD WINAPI connectsockThreadProc(LPVOID lpParameter)
{
	CONNECTSOCKDATA* pData;
	pData = (CONNECTSOCKDATA*)lpParameter;
	pData->s = connectsock(pData->family, pData->host, pData->port, pData->PreMsg, &pData->CancelCheckWork);
	return 0;
}

SOCKET connectsock(char *host, int port, char *PreMsg, int *CancelCheckWork)
{
	SOCKET Result;
	CONNECTSOCKDATA DataIPv4;
	CONNECTSOCKDATA DataIPv6;
	Result = INVALID_SOCKET;
	switch(CurHost.CurNetType)
	{
	case NTYPE_AUTO:
		DataIPv4.family = AF_INET;
		DataIPv4.host = host;
		DataIPv4.port = port;
		DataIPv4.PreMsg = PreMsg;
		DataIPv4.CancelCheckWork = *CancelCheckWork;
		DataIPv4.h = CreateThread(NULL, 0, connectsockThreadProc, &DataIPv4, 0, NULL);
		DataIPv6.family = AF_INET6;
		DataIPv6.host = host;
		DataIPv6.port = port;
		DataIPv6.PreMsg = PreMsg;
		DataIPv6.CancelCheckWork = *CancelCheckWork;
		DataIPv6.h = CreateThread(NULL, 0, connectsockThreadProc, &DataIPv6, 0, NULL);
		while(1)
		{
			if(GetExitCodeThread(DataIPv4.h, &DataIPv4.ExitCode))
			{
				if(DataIPv4.ExitCode != STILL_ACTIVE)
				{
					if(DataIPv4.s != INVALID_SOCKET)
					{
						Result = DataIPv4.s;
						CurHost.CurNetType = NTYPE_IPV4;
						break;
					}
				}
			}
			if(GetExitCodeThread(DataIPv6.h, &DataIPv6.ExitCode))
			{
				if(DataIPv6.ExitCode != STILL_ACTIVE)
				{
					if(DataIPv6.s != INVALID_SOCKET)
					{
						Result = DataIPv6.s;
						CurHost.CurNetType = NTYPE_IPV6;
						break;
					}
				}
			}
			if(GetExitCodeThread(DataIPv4.h, &DataIPv4.ExitCode) && GetExitCodeThread(DataIPv6.h, &DataIPv6.ExitCode))
			{
				if(DataIPv4.ExitCode != STILL_ACTIVE && DataIPv6.ExitCode != STILL_ACTIVE)
				{
					if(DataIPv4.s == INVALID_SOCKET && DataIPv6.s == INVALID_SOCKET)
						break;
				}
			}
			DataIPv4.CancelCheckWork = *CancelCheckWork;
			DataIPv6.CancelCheckWork = *CancelCheckWork;
			BackgrndMessageProc();
			Sleep(1);
		}
		while(1)
		{
			if(GetExitCodeThread(DataIPv4.h, &DataIPv4.ExitCode) && GetExitCodeThread(DataIPv6.h, &DataIPv6.ExitCode))
			{
				if(DataIPv4.ExitCode != STILL_ACTIVE && DataIPv6.ExitCode != STILL_ACTIVE)
				{
					CloseHandle(DataIPv4.h);
					CloseHandle(DataIPv6.h);
					break;
				}
			}
			DataIPv4.CancelCheckWork = YES;
			DataIPv6.CancelCheckWork = YES;
			BackgrndMessageProc();
			Sleep(1);
		}
		break;
	case NTYPE_IPV4:
		Result = connectsock(AF_INET, host, port, PreMsg, CancelCheckWork);
		CurHost.CurNetType = NTYPE_IPV4;
		break;
	case NTYPE_IPV6:
		Result = connectsock(AF_INET6, host, port, PreMsg, CancelCheckWork);
		CurHost.CurNetType = NTYPE_IPV6;
		break;
	}
	return Result;
}


static SOCKET connectsock(ADDRESS_FAMILY family, char *host, int port, char *PreMsg, int *CancelCheckWork) {
	assert(family == AF_INET || family == AF_INET6);
	auto ipversion = family == AF_INET ? MSGJPN333 : MSGJPN334;
	int Fwall = AskHostFireWall() == YES ? FwallType : FWALL_NONE;

	//////////////////////////////
	// ホスト名解決と接続の準備
	//////////////////////////////

	UseIPadrs = YES;
	strcpy(DomainName, host);
	sockaddr_storage saTarget{ family };		/* 接続先ホストのアドレス情報 */
	if (family == AF_INET)
		reinterpret_cast<sockaddr_in&>(saTarget).sin_port = htons(port);
	else
		reinterpret_cast<sockaddr_in6&>(saTarget).sin6_port = htons(port);
	if (auto ai = getaddrinfo(u8(host), port, family); !ai) {
		// ホスト名が指定された
		// ホスト名からアドレスを求める
		if ((Fwall == FWALL_SOCKS5_NOAUTH || Fwall == FWALL_SOCKS5_USER) && FwallResolve == YES) {
			// ホスト名解決はSOCKSサーバに任せる
		} else {
			// アドレスを取得
			SetTaskMsg(MSGJPN016, DomainName, ipversion);
			ai = getaddrinfo(u8(host), port, family, CancelCheckWork);
		}

		if (ai) {
			memcpy(&saTarget, ai->ai_addr, ai->ai_addrlen);
			SetTaskMsg(MSGJPN017, PreMsg, DomainName, u8(AddressPortToString(ai->ai_addr, ai->ai_addrlen)).c_str(), ipversion);
		} else {
			if (Fwall == FWALL_SOCKS5_NOAUTH || Fwall == FWALL_SOCKS5_USER) {
				UseIPadrs = NO;
				SetTaskMsg(MSGJPN018, PreMsg, DomainName, port, ipversion);
			} else {
				SetTaskMsg(MSGJPN019, host, ipversion);
				return INVALID_SOCKET;
			}
		}
	} else {
		memcpy(&saTarget, ai->ai_addr, ai->ai_addrlen);
		SetTaskMsg(MSGJPN020, PreMsg, u8(AddressPortToString(ai->ai_addr, ai->ai_addrlen)).c_str(), ipversion);
	}

	SOCKS4CMD cmd4;
	SOCKS5REQUEST cmd5;
	int cmdlen;
	sockaddr_storage saSocks;	/* SOCKSサーバのアドレス情報 */
	sockaddr_storage saConnect;
	if (family == AF_INET && Fwall == FWALL_SOCKS4 || Fwall == FWALL_SOCKS5_NOAUTH || Fwall == FWALL_SOCKS5_USER) {
		// SOCKSを使う
		// SOCKSに接続する準備
		if (family == AF_INET && Fwall == FWALL_SOCKS4) {
			auto const& sa = reinterpret_cast<sockaddr_in const&>(saTarget);
			cmd4 = { SOCKS4_VER, SOCKS4_CMD_CONNECT, sa.sin_port, sa.sin_addr.s_addr };
			strcpy(cmd4.UserID, FwallUser);
			cmdlen = offsetof(SOCKS4CMD, UserID) + (int)strlen(FwallUser) + 1;
		} else {
			auto port = family == AF_INET ? reinterpret_cast<sockaddr_in const&>(saTarget).sin_port : reinterpret_cast<sockaddr_in6 const&>(saTarget).sin6_port;
			cmdlen = UseIPadrs == YES ? Socks5MakeCmdPacket(&cmd5, SOCKS5_CMD_CONNECT, reinterpret_cast<const sockaddr*>(&saTarget), port) : Socks5MakeCmdPacket(&cmd5, SOCKS5_CMD_CONNECT, DomainName, port);
		}

		auto ai = getaddrinfo(u8(FwallHost), FwallPort, family);
		if (!ai)
			ai = getaddrinfo(u8(FwallHost), FwallPort, family, CancelCheckWork);
		if (!ai) {
			SetTaskMsg(MSGJPN021, FwallHost, ipversion);
			return INVALID_SOCKET;
		}
		memcpy(&saSocks, ai->ai_addr, ai->ai_addrlen);
		SetTaskMsg(MSGJPN022, u8(AddressPortToString(ai->ai_addr, ai->ai_addrlen)).c_str(), ipversion);
		// connectで接続する先はSOCKSサーバ
		saConnect = saSocks;
	} else {
		// connectで接続するのは接続先のホスト
		saConnect = saTarget;
	}

	/////////////
	// 接続実行
	/////////////

	SOCKET s = do_socket(family, SOCK_STREAM, TCP_PORT);
	if (s == INVALID_SOCKET) {
		SetTaskMsg(MSGJPN027, ipversion);
		return INVALID_SOCKET;
	}
	// ソケットにデータを付与
	SetAsyncTableData(s, &saTarget, &saSocks);
	if (do_connect(s, (struct sockaddr *)&saConnect, sizeof(saConnect), CancelCheckWork) == SOCKET_ERROR) {
		SetTaskMsg(MSGJPN026, ipversion);
		DoClose(s);
		return INVALID_SOCKET;
	}
	if (family == AF_INET && Fwall == FWALL_SOCKS4) {
		if (SOCKS4REPLY reply{ 0, -1 }; SocksSendCmd(s, &cmd4, cmdlen, CancelCheckWork) != FFFTP_SUCCESS || Socks4GetCmdReply(s, &reply, CancelCheckWork) != FFFTP_SUCCESS || reply.Result != SOCKS4_RES_OK) {
			SetTaskMsg(MSGJPN023, reply.Result, ipversion);
			DoClose(s);
			return INVALID_SOCKET;
		}
	} else if (Fwall == FWALL_SOCKS5_NOAUTH || Fwall == FWALL_SOCKS5_USER) {
		if (Socks5SelMethod(s, CancelCheckWork) == FFFTP_FAIL) {
			DoClose(s);
			return INVALID_SOCKET;
		}
		if (SOCKS5REPLY reply{ 0, -1 }; SocksSendCmd(s, &cmd5, cmdlen, CancelCheckWork) != FFFTP_SUCCESS || Socks5GetCmdReply(s, &reply, CancelCheckWork) != FFFTP_SUCCESS || reply.Result != SOCKS5_RES_OK) {
			SetTaskMsg(MSGJPN024, reply.Result, ipversion);
			DoClose(s);
			return INVALID_SOCKET;
		}
	}
	SetTaskMsg(MSGJPN025, ipversion);
	return s;
}


/*----- リッスンソケットを取得 ------------------------------------------------
*
*	Parameter
*		SOCKET ctrl_skt : コントロールソケット
*
*	Return Value
*		SOCKET リッスンソケット
*----------------------------------------------------------------------------*/

// IPv6対応
SOCKET GetFTPListenSocket(SOCKET ctrl_skt, int *CancelCheckWork)
{
	SOCKET Result;
	Result = INVALID_SOCKET;
	switch(CurHost.CurNetType)
	{
	case NTYPE_IPV4:
		Result = GetFTPListenSocketIPv4(ctrl_skt, CancelCheckWork);
		break;
	case NTYPE_IPV6:
		Result = GetFTPListenSocketIPv6(ctrl_skt, CancelCheckWork);
		break;
	}
	return Result;
}


// IPv6対応
//SOCKET GetFTPListenSocket(SOCKET ctrl_skt, int *CancelCheckWork)
SOCKET GetFTPListenSocketIPv4(SOCKET ctrl_skt, int *CancelCheckWork)
{
	// IPv6対応
	sockaddr_storage SocksSockAddr;	/* SOCKSサーバのアドレス情報 */
	sockaddr_storage CurSockAddr;		/* 接続先ホストのアドレス情報 */
	SOCKET listen_skt;
	int iLength;
	struct sockaddr_in saCtrlAddr;
	sockaddr_in saTmpAddr{ AF_INET };
	SOCKS4CMD Socks4Cmd;
	SOCKS4REPLY Socks4Reply;
	SOCKS5REQUEST Socks5Cmd;
	SOCKS5REPLY Socks5Reply;

	int Len;
	int Fwall;
	int Port;
	char ExtAdrs[40];

	// ソケットにデータを付与
	GetAsyncTableData(ctrl_skt, &CurSockAddr, &SocksSockAddr);

	Fwall = FWALL_NONE;
	if(AskHostFireWall() == YES)
		Fwall = FwallType;

	if((listen_skt = do_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) != INVALID_SOCKET)
	{
		if(auto sa = reinterpret_cast<const sockaddr_in*>(&CurSockAddr); Fwall == FWALL_SOCKS4)
		{
			/*===== SOCKS4を使う =====*/
			DoPrintf("Use SOCKS4 BIND");
			if(do_connect(listen_skt, (struct sockaddr *)&SocksSockAddr, sizeof(SocksSockAddr), CancelCheckWork) != SOCKET_ERROR)
			{
				Socks4Cmd.Ver = SOCKS4_VER;
				Socks4Cmd.Cmd = SOCKS4_CMD_BIND;
				Socks4Cmd.Port = sa->sin_port;
				Socks4Cmd.AdrsInt = sa->sin_addr.s_addr;
				strcpy(Socks4Cmd.UserID, FwallUser);
				Len = offsetof(SOCKS4CMD, UserID) + (int)strlen(FwallUser) + 1;

				Socks4Reply.Result = -1;
				// 同時接続対応
//				if((SocksSendCmd(listen_skt, &Socks4Cmd, Len, CancelCheckWork) != FFFTP_SUCCESS) ||
//				   (Socks4GetCmdReply(listen_skt, &Socks4Reply) != FFFTP_SUCCESS) || 
//				   (Socks4Reply.Result != SOCKS4_RES_OK))
				if((SocksSendCmd(listen_skt, &Socks4Cmd, Len, CancelCheckWork) != FFFTP_SUCCESS) ||
				   (Socks4GetCmdReply(listen_skt, &Socks4Reply, CancelCheckWork) != FFFTP_SUCCESS) || 
				   (Socks4Reply.Result != SOCKS4_RES_OK))
				{
					// IPv6対応
//					SetTaskMsg(MSGJPN028, Socks4Reply.Result);
					SetTaskMsg(MSGJPN028, Socks4Reply.Result, MSGJPN333);
					DoClose(listen_skt);
					listen_skt = INVALID_SOCKET;
				}

				if(Socks4Reply.AdrsInt == 0)
					Socks4Reply.AdrsInt = reinterpret_cast<const sockaddr_in*>(&SocksSockAddr)->sin_addr.s_addr;

				saTmpAddr.sin_addr.s_addr = Socks4Reply.AdrsInt;
				saTmpAddr.sin_port = Socks4Reply.Port;
			}
		}
		else if((Fwall == FWALL_SOCKS5_NOAUTH) || (Fwall == FWALL_SOCKS5_USER))
		{
			/*===== SOCKS5を使う =====*/
			DoPrintf("Use SOCKS5 BIND");
			if(do_connect(listen_skt, (struct sockaddr *)&SocksSockAddr, sizeof(SocksSockAddr), CancelCheckWork) != SOCKET_ERROR)
			{
				if(Socks5SelMethod(listen_skt, CancelCheckWork) == FFFTP_FAIL)
				{
					DoClose(listen_skt);
					listen_skt = INVALID_SOCKET;
					return(listen_skt);
				}

				Len = UseIPadrs == YES
					? Socks5MakeCmdPacket(&Socks5Cmd, SOCKS5_CMD_BIND, reinterpret_cast<const sockaddr*>(&CurSockAddr), sa->sin_port)
					: Socks5MakeCmdPacket(&Socks5Cmd, SOCKS5_CMD_BIND, DomainName, sa->sin_port);

				Socks5Reply.Result = -1;
				// 同時接続対応
//				if((SocksSendCmd(listen_skt, &Socks5Cmd, Len, CancelCheckWork) != FFFTP_SUCCESS) ||
//				   (Socks5GetCmdReply(listen_skt, &Socks5Reply) != FFFTP_SUCCESS) || 
//				   (Socks5Reply.Result != SOCKS5_RES_OK))
				if((SocksSendCmd(listen_skt, &Socks5Cmd, Len, CancelCheckWork) != FFFTP_SUCCESS) ||
				   (Socks5GetCmdReply(listen_skt, &Socks5Reply, CancelCheckWork) != FFFTP_SUCCESS) || 
				   (Socks5Reply.Result != SOCKS5_RES_OK))
				{
					// IPv6対応
//					SetTaskMsg(MSGJPN029, Socks5Reply.Result);
					SetTaskMsg(MSGJPN029, Socks5Reply.Result, MSGJPN333);
					DoClose(listen_skt);
					listen_skt = INVALID_SOCKET;
				}

				saTmpAddr.sin_addr = *reinterpret_cast<const IN_ADDR*>(&Socks5Reply._dummy[0]);
				saTmpAddr.sin_port = *reinterpret_cast<const USHORT*>(&Socks5Reply._dummy[4]);
			}
		}
		else
		{
			/*===== SOCKSを使わない =====*/
			DoPrintf("Use normal BIND");
			saCtrlAddr.sin_port = htons(0);
			saCtrlAddr.sin_family = AF_INET;
			saCtrlAddr.sin_addr.s_addr = 0;

			if(bind(listen_skt, (struct sockaddr *)&saCtrlAddr, sizeof(struct sockaddr)) != SOCKET_ERROR)
			{
				iLength = sizeof(saCtrlAddr);
				if(getsockname(listen_skt, (struct sockaddr *)&saCtrlAddr, &iLength) != SOCKET_ERROR)
				{
					if(do_listen(listen_skt, 1) == 0)
					{
						iLength = sizeof(saTmpAddr);
						if(getsockname(ctrl_skt, (struct sockaddr *)&saTmpAddr, &iLength) == SOCKET_ERROR)
							ReportWSError("getsockname", WSAGetLastError());

						saTmpAddr.sin_port = saCtrlAddr.sin_port;
						// UPnP対応
						if(IsUPnPLoaded() == YES && UPnPEnabled == YES)
						{
							if (auto port = ntohs(saTmpAddr.sin_port); AddPortMapping(u8(AddressToString(saTmpAddr)).c_str(), port, ExtAdrs) == FFFTP_SUCCESS)
								if (auto ai = getaddrinfo(u8(ExtAdrs), port, AF_INET)) {
									saTmpAddr = *reinterpret_cast<const sockaddr_in*>(ai->ai_addr);
									SetAsyncTableDataMapPort(listen_skt, port);
								}
						}
					}
					else
					{
						ReportWSError("listen", WSAGetLastError());
						do_closesocket(listen_skt);
						listen_skt = INVALID_SOCKET;
					}
				}
				else
				{
					ReportWSError("getsockname", WSAGetLastError());
					do_closesocket(listen_skt);
					listen_skt = INVALID_SOCKET;
				}
			}
			else
			{
				ReportWSError("bind", WSAGetLastError());
				do_closesocket(listen_skt);
				listen_skt = INVALID_SOCKET;
			}

			if(listen_skt == INVALID_SOCKET)
				// IPv6対応
//				SetTaskMsg(MSGJPN030);
				SetTaskMsg(MSGJPN030, MSGJPN333);
		}
	}
	else
		ReportWSError("socket create", WSAGetLastError());

	if(listen_skt != INVALID_SOCKET)
	{
		auto a = reinterpret_cast<const unsigned char*>(&saTmpAddr.sin_addr), p = reinterpret_cast<const unsigned char*>(&saTmpAddr.sin_port);
		if((command(ctrl_skt,NULL, CancelCheckWork, "PORT %hhu,%hhu,%hhu,%hhu,%hhu,%hhu", a[0], a[1], a[2], a[3], p[0], p[1]) / 100) != FTP_COMPLETE)
		{
			// IPv6対応
//			SetTaskMsg(MSGJPN031);
			SetTaskMsg(MSGJPN031, MSGJPN333);
			// UPnP対応
			if(IsUPnPLoaded() == YES)
			{
				if(GetAsyncTableDataMapPort(listen_skt, &Port) == YES)
					RemovePortMapping(Port);
			}
			do_closesocket(listen_skt);
			listen_skt = INVALID_SOCKET;
		}
	}

	return(listen_skt);
}


SOCKET GetFTPListenSocketIPv6(SOCKET ctrl_skt, int *CancelCheckWork)
{
	sockaddr_storage SocksSockAddr;	/* SOCKSサーバのアドレス情報 */
	sockaddr_storage CurSockAddr;		/* 接続先ホストのアドレス情報 */
	SOCKET listen_skt;
	int iLength;
	struct sockaddr_in6 saCtrlAddr;
	sockaddr_in6 saTmpAddr{ AF_INET6 };
	SOCKS5REQUEST Socks5Cmd;
	SOCKS5REPLY Socks5Reply;

	int Len;
	int Fwall;
	int Port;
	char ExtAdrs[40];

	// ソケットにデータを付与
	GetAsyncTableData(ctrl_skt, &CurSockAddr, &SocksSockAddr);

	Fwall = FWALL_NONE;
	if(AskHostFireWall() == YES)
		Fwall = FwallType;

	if((listen_skt = do_socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP)) != INVALID_SOCKET)
	{
		if((Fwall == FWALL_SOCKS5_NOAUTH) || (Fwall == FWALL_SOCKS5_USER))
		{
			/*===== SOCKS5を使う =====*/
			DoPrintf("Use SOCKS5 BIND");
			if(do_connect(listen_skt, (struct sockaddr *)&SocksSockAddr, sizeof(SocksSockAddr), CancelCheckWork) != SOCKET_ERROR)
			{
				if(Socks5SelMethod(listen_skt, CancelCheckWork) == FFFTP_FAIL)
				{
					DoClose(listen_skt);
					listen_skt = INVALID_SOCKET;
					return(listen_skt);
				}

				auto port = reinterpret_cast<const sockaddr_in6*>(&CurSockAddr)->sin6_port;
				Len = UseIPadrs == YES
					? Socks5MakeCmdPacket(&Socks5Cmd, SOCKS5_CMD_BIND, reinterpret_cast<const sockaddr*>(&CurSockAddr), port)
					: Socks5MakeCmdPacket(&Socks5Cmd, SOCKS5_CMD_BIND, DomainName, port);

				Socks5Reply.Result = -1;
				// 同時接続対応
//				if((SocksSendCmd(listen_skt, &Socks5Cmd, Len, CancelCheckWork) != FFFTP_SUCCESS) ||
//				   (Socks5GetCmdReply(listen_skt, &Socks5Reply) != FFFTP_SUCCESS) || 
//				   (Socks5Reply.Result != SOCKS5_RES_OK))
				if((SocksSendCmd(listen_skt, &Socks5Cmd, Len, CancelCheckWork) != FFFTP_SUCCESS) ||
				   (Socks5GetCmdReply(listen_skt, &Socks5Reply, CancelCheckWork) != FFFTP_SUCCESS) || 
				   (Socks5Reply.Result != SOCKS5_RES_OK))
				{
					SetTaskMsg(MSGJPN029, Socks5Reply.Result, MSGJPN334);
					DoClose(listen_skt);
					listen_skt = INVALID_SOCKET;
				}

				saTmpAddr.sin6_addr = *reinterpret_cast<const IN6_ADDR*>(&Socks5Reply._dummy[0]);
				saTmpAddr.sin6_port = *reinterpret_cast<const USHORT*>(&Socks5Reply._dummy[16]);
			}
		}
		else
		{
			/*===== SOCKSを使わない =====*/
			DoPrintf("Use normal BIND");
			saCtrlAddr.sin6_port = htons(0);
			saCtrlAddr.sin6_family = AF_INET6;
			memset(&saCtrlAddr.sin6_addr, 0, 16);
			saCtrlAddr.sin6_flowinfo = 0;
			saCtrlAddr.sin6_scope_id = 0;

			if(bind(listen_skt, (struct sockaddr *)&saCtrlAddr, sizeof(struct sockaddr_in6)) != SOCKET_ERROR)
			{
				iLength = sizeof(saCtrlAddr);
				if(getsockname(listen_skt, (struct sockaddr *)&saCtrlAddr, &iLength) != SOCKET_ERROR)
				{
					if(do_listen(listen_skt, 1) == 0)
					{
						iLength = sizeof(saTmpAddr);
						if(getsockname(ctrl_skt, (struct sockaddr *)&saTmpAddr, &iLength) == SOCKET_ERROR)
							ReportWSError("getsockname", WSAGetLastError());

						saTmpAddr.sin6_port = saCtrlAddr.sin6_port;
						// UPnP対応
						if(IsUPnPLoaded() == YES && UPnPEnabled == YES)
						{
							if (auto port = ntohs(saTmpAddr.sin6_port); AddPortMapping(u8(AddressToString(saTmpAddr)).c_str(), port, ExtAdrs) == FFFTP_SUCCESS)
								if (auto ai = getaddrinfo(u8(ExtAdrs), port, AF_INET6)) {
									saTmpAddr = *reinterpret_cast<const sockaddr_in6*>(ai->ai_addr);
									SetAsyncTableDataMapPort(listen_skt, port);
								}
						}
					}
					else
					{
						ReportWSError("listen", WSAGetLastError());
						do_closesocket(listen_skt);
						listen_skt = INVALID_SOCKET;
					}
				}
				else
				{
					ReportWSError("getsockname", WSAGetLastError());
					do_closesocket(listen_skt);
					listen_skt = INVALID_SOCKET;
				}
			}
			else
			{
				ReportWSError("bind", WSAGetLastError());
				do_closesocket(listen_skt);
				listen_skt = INVALID_SOCKET;
			}

			if(listen_skt == INVALID_SOCKET)
				SetTaskMsg(MSGJPN030, MSGJPN334);
		}
	}
	else
		ReportWSError("socket create", WSAGetLastError());

	if(listen_skt != INVALID_SOCKET)
	{
		if((command(ctrl_skt,NULL, CancelCheckWork, "EPRT |2|%s|%d|", u8(AddressToString(saTmpAddr)).c_str(), ntohs(saTmpAddr.sin6_port)) / 100) != FTP_COMPLETE)
		{
			SetTaskMsg(MSGJPN031, MSGJPN334);
			// UPnP対応
			if(IsUPnPLoaded() == YES)
			{
				if(GetAsyncTableDataMapPort(listen_skt, &Port) == YES)
					RemovePortMapping(Port);
			}
			do_closesocket(listen_skt);
			listen_skt = INVALID_SOCKET;
		}
	}

	return(listen_skt);
}


/*----- ホストへ接続処理中かどうかを返す---------------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		int ステータス
*			YES/NO
*----------------------------------------------------------------------------*/

int AskTryingConnect(void)
{
	return(TryConnect);
}


static int Socks5MakeCmdPacket(SOCKS5REQUEST *Packet, char Cmd, std::variant<const sockaddr*, const char*> addr, ushort Port) {
	Packet->Ver = SOCKS5_VER;
	Packet->Cmd = Cmd;
	Packet->Rsv = 0;
	auto dst = reinterpret_cast<unsigned char*>(Packet->_dummy);
	auto [type, ptr, len] = std::visit([&dst](auto addr) -> std::tuple<char, const void*, int> {
		using type = decltype(addr);
		if constexpr (std::is_same_v<type, const sockaddr*>) {
			if (addr->sa_family == AF_INET) {
				return { SOCKS5_ADRS_IPV4, &reinterpret_cast<const sockaddr_in*>(addr)->sin_addr, static_cast<int>(sizeof(IN_ADDR)) };
			} else {
				assert(addr->sa_family == AF_INET6);
				return { SOCKS5_ADRS_IPV6, &reinterpret_cast<const sockaddr_in6*>(addr)->sin6_addr, static_cast<int>(sizeof(IN6_ADDR)) };
			}
		} else if constexpr (std::is_same_v<type, const char*>) {
			auto hostlen = static_cast<unsigned char>(strlen(addr));
			*dst++ = hostlen;
			return { SOCKS5_ADRS_NAME, addr, hostlen };
		} else
			static_assert(false_v<type>);
	}, addr);
	Packet->Type = type;
	dst = std::copy_n(reinterpret_cast<const unsigned char*>(ptr), len, dst);
	*reinterpret_cast<ushort*>(dst) = Port;
	return static_cast<int>(dst - reinterpret_cast<const unsigned char*>(Packet)) + 2;
}


/*----- SOCKSのコマンドを送る -------------------------------------------------
*
*	Parameter
*		SOCKET Socket : ソケット
*		void *Data : 送るデータ
*		int Size : サイズ
*
*	Return Value
*		int ステータス (FFFTP_SUCCESS/FFFTP_FAIL)
*----------------------------------------------------------------------------*/

static int SocksSendCmd(SOCKET Socket, void *Data, int Size, int *CancelCheckWork)
{
	int Ret;

	Ret = SendData(Socket, (char *)Data, Size, 0, CancelCheckWork);

	if(Ret != FFFTP_SUCCESS)
		SetTaskMsg(MSGJPN033, *((short *)Data));

	return(Ret);
}


/*----- SOCKS5のコマンドに対するリプライパケットを受信する --------------------
*
*	Parameter
*		SOCKET Socket : ソケット
*		SOCKS5REPLY *Packet : パケット
*
*	Return Value
*		int ステータス (FFFTP_SUCCESS/FFFTP_FAIL)
*----------------------------------------------------------------------------*/

// 同時接続対応
//static int Socks5GetCmdReply(SOCKET Socket, SOCKS5REPLY *Packet)
static int Socks5GetCmdReply(SOCKET Socket, SOCKS5REPLY *Packet, int *CancelCheckWork)
{
	uchar *Pos;
	int Len;
	int Ret;

	Pos = (uchar *)Packet;
	Pos += SOCKS5REPLY_SIZE;

	// 同時接続対応
//	if((Ret = ReadNchar(Socket, (char *)Packet, SOCKS5REPLY_SIZE, &CancelFlg)) == FFFTP_SUCCESS)
	if((Ret = ReadNchar(Socket, (char *)Packet, SOCKS5REPLY_SIZE, CancelCheckWork)) == FFFTP_SUCCESS)
	{
		if(Packet->Type == SOCKS5_ADRS_IPV4)
			Len = 4 + 2;
		else if(Packet->Type == SOCKS5_ADRS_IPV6)
			Len = 16 + 2;
		else
		{
			// 同時接続対応
//			if((Ret = ReadNchar(Socket, (char *)Pos, 1, &CancelFlg)) == FFFTP_SUCCESS)
			if((Ret = ReadNchar(Socket, (char *)Pos, 1, CancelCheckWork)) == FFFTP_SUCCESS)
			{
				Len = *Pos + 2;
				Pos++;
			}
		}

		if(Ret == FFFTP_SUCCESS)
			// 同時接続対応
//			Ret = ReadNchar(Socket, (char *)Pos, Len, &CancelFlg);
			Ret = ReadNchar(Socket, (char *)Pos, Len, CancelCheckWork);
	}

	if(Ret != FFFTP_SUCCESS)
		SetTaskMsg(MSGJPN034);

	return(Ret);
}


/*----- SOCKS4のコマンドに対するリプライパケットを受信する --------------------
*
*	Parameter
*		SOCKET Socket : ソケット
*		SOCKS5REPLY *Packet : パケット
*
*	Return Value
*		int ステータス (FFFTP_SUCCESS/FFFTP_FAIL)
*----------------------------------------------------------------------------*/

// 同時接続対応
//static int Socks4GetCmdReply(SOCKET Socket, SOCKS4REPLY *Packet)
static int Socks4GetCmdReply(SOCKET Socket, SOCKS4REPLY *Packet, int *CancelCheckWork)
{
	int Ret;

	// 同時接続対応
//	Ret = ReadNchar(Socket, (char *)Packet, SOCKS4REPLY_SIZE, &CancelFlg);
	Ret = ReadNchar(Socket, (char *)Packet, SOCKS4REPLY_SIZE, CancelCheckWork);

	if(Ret != FFFTP_SUCCESS)
		DoPrintf(MSGJPN035);

	return(Ret);
}


/*----- SOCKS5の認証を行う ----------------------------------------------------
*
*	Parameter
*		SOCKET Socket : ソケット
*
*	Return Value
*		int ステータス (FFFTP_SUCCESS/FFFTP_FAIL)
*----------------------------------------------------------------------------*/

static int Socks5SelMethod(SOCKET Socket, int *CancelCheckWork)
{
	int Ret;
	SOCKS5METHODREQUEST Socks5Method;
	SOCKS5METHODREPLY Socks5MethodReply;
	SOCKS5USERPASSSTATUS Socks5Status;
	char Buf[USER_NAME_LEN + PASSWORD_LEN + 4];
	int Len;
	int Len2;

	Ret = FFFTP_SUCCESS;
	Socks5Method.Ver = SOCKS5_VER;
	Socks5Method.Num = 1;
	if(FwallType == FWALL_SOCKS5_NOAUTH)
		Socks5Method.Methods[0] = SOCKS5_AUTH_NONE;
	else
		Socks5Method.Methods[0] = SOCKS5_AUTH_USER;

	// 同時接続対応
//	if((SocksSendCmd(Socket, &Socks5Method, SOCKS5METHODREQUEST_SIZE, CancelCheckWork) != FFFTP_SUCCESS) ||
//	   (ReadNchar(Socket, (char *)&Socks5MethodReply, SOCKS5METHODREPLY_SIZE, &CancelFlg) != FFFTP_SUCCESS) ||
//	   (Socks5MethodReply.Method == (uchar)0xFF))
	if((SocksSendCmd(Socket, &Socks5Method, SOCKS5METHODREQUEST_SIZE, CancelCheckWork) != FFFTP_SUCCESS) ||
	   (ReadNchar(Socket, (char *)&Socks5MethodReply, SOCKS5METHODREPLY_SIZE, CancelCheckWork) != FFFTP_SUCCESS) ||
	   (Socks5MethodReply.Method == (uchar)0xFF))
	{
		SetTaskMsg(MSGJPN036);
		Ret = FFFTP_FAIL;
	}
	else if(Socks5MethodReply.Method == SOCKS5_AUTH_USER)
	{
		DoPrintf("SOCKS5 User/Pass Authentication");
		Buf[0] = SOCKS5_USERAUTH_VER;
		Len = (int)strlen(FwallUser);
		Len2 = (int)strlen(FwallPass);
		Buf[1] = Len;
		strcpy(Buf+2, FwallUser);
		Buf[2 + Len] = Len2;
		strcpy(Buf+3+Len, FwallPass);

		// 同時接続対応
//		if((SocksSendCmd(Socket, &Buf, Len+Len2+3, CancelCheckWork) != FFFTP_SUCCESS) ||
//		   (ReadNchar(Socket, (char *)&Socks5Status, SOCKS5USERPASSSTATUS_SIZE, &CancelFlg) != FFFTP_SUCCESS) ||
//		   (Socks5Status.Status != 0))
		if((SocksSendCmd(Socket, &Buf, Len+Len2+3, CancelCheckWork) != FFFTP_SUCCESS) ||
		   (ReadNchar(Socket, (char *)&Socks5Status, SOCKS5USERPASSSTATUS_SIZE, CancelCheckWork) != FFFTP_SUCCESS) ||
		   (Socks5Status.Status != 0))
		{
			SetTaskMsg(MSGJPN037);
			Ret = FFFTP_FAIL;
		}
	}
	else
		DoPrintf("SOCKS5 No Authentication");

	return(Ret);
}


/*----- SOCKSのBINDの第２リプライメッセージを受け取る -------------------------
*
*	Parameter
*		SOCKET Socket : ソケット
*		SOCKET *Data : データソケットを返すワーク
*
*	Return Value
*		int ステータス (FFFTP_SUCCESS/FFFTP_FAIL)
*----------------------------------------------------------------------------*/

// 同時接続対応
//int SocksGet2ndBindReply(SOCKET Socket, SOCKET *Data)
int SocksGet2ndBindReply(SOCKET Socket, SOCKET *Data, int *CancelCheckWork)
{
	int Ret;
	char Buf[300];

	Ret = FFFTP_FAIL;
	if((AskHostFireWall() == YES) && (FwallType == FWALL_SOCKS4))
	{
		// 同時接続対応
//		Socks4GetCmdReply(Socket, (SOCKS4REPLY *)Buf);
		Socks4GetCmdReply(Socket, (SOCKS4REPLY *)Buf, CancelCheckWork);
		*Data = Socket;
		Ret = FFFTP_SUCCESS;
	}
	else if((AskHostFireWall() == YES) &&
			((FwallType == FWALL_SOCKS5_NOAUTH) || (FwallType == FWALL_SOCKS5_USER)))
	{
		// 同時接続対応
//		Socks5GetCmdReply(Socket, (SOCKS5REPLY *)Buf);
		Socks5GetCmdReply(Socket, (SOCKS5REPLY *)Buf, CancelCheckWork);
		*Data = Socket;
		Ret = FFFTP_SUCCESS;
	}
	return(Ret);
}


int AskUseNoEncryption(void)
{
	return(CurHost.UseNoEncryption);
}

int AskUseFTPES(void)
{
	return(CurHost.UseFTPES);
}

int AskUseFTPIS(void)
{
	return(CurHost.UseFTPIS);
}

int AskUseSFTP(void)
{
	return(CurHost.UseSFTP);
}

char *AskPrivateKey(void)
{
	return(CurHost.PrivateKey);
}

// 同時接続対応
int AskMaxThreadCount(void)
{
	return(CurHost.MaxThreadCount);
}

int AskReuseCmdSkt(void)
{
	return(CurHost.ReuseCmdSkt);
}

// FEAT対応
int AskHostFeature(void)
{
	return(CurHost.Feature);
}

// MLSD対応
int AskUseMLSD(void)
{
	return(CurHost.UseMLSD);
}

// IPv6対応
int AskCurNetType(void)
{
	return(CurHost.CurNetType);
}

// 自動切断対策
int AskNoopInterval(void)
{
	return(CurHost.NoopInterval);
}

// 再転送対応
int AskTransferErrorMode(void)
{
	return(CurHost.TransferErrorMode);
}

int AskTransferErrorNotify(void)
{
	return(CurHost.TransferErrorNotify);
}

// セッションあたりの転送量制限対策
int AskErrorReconnect(void)
{
	return(CurHost.TransferErrorReconnect);
}

// ホスト側の設定ミス対策
int AskNoPasvAdrs(void)
{
	return(CurHost.NoPasvAdrs);
}

