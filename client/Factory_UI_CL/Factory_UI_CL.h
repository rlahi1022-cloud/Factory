// Factory_UI_CL.h
#pragma once

#ifndef __AFXWIN_H__
    #error "PCH에 대해 이 파일을 포함하기 전에 'pch.h'를 포함합니다."
#endif

#include "resource.h"

class CFactoryUICLApp : public CWinApp
{
public:
    CFactoryUICLApp();
    virtual BOOL InitInstance() override;
    virtual int ExitInstance() override;   // 앱 종료 시 WSACleanup 호출
    DECLARE_MESSAGE_MAP()
};

extern CFactoryUICLApp theApp;
