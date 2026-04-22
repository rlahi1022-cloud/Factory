// ============================================================================
// Factory_UI_CLDlg.cpp — MFC 마법사 기본 다이얼로그 (미사용/레거시)
// ============================================================================
// 참고:
//   Visual Studio MFC 프로젝트 템플릿이 자동 생성한 다이얼로그 클래스.
//   실제 앱은 LoginDlg → MainTabDlg 로 진입하므로 이 다이얼로그는
//   표시되지 않는다. 템플릿 파일 제거 시 리소스 의존성 영향을 피하기 위해
//   최소 상태로 남겨둔 상태.
// ============================================================================
#include "pch.h"
#include "framework.h"
#include "Factory_UI_CL.h"
#include "Factory_UI_CLDlg.h"
#include "afxdialogex.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CFactoryUICLDlg 대화 상자
CFactoryUICLDlg::CFactoryUICLDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_FACTORY_UI_CL_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CFactoryUICLDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CFactoryUICLDlg, CDialogEx)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
END_MESSAGE_MAP()

BOOL CFactoryUICLDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	SetIcon(m_hIcon, TRUE);
	SetIcon(m_hIcon, FALSE);
	return TRUE;
}

void CFactoryUICLDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this);
		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

HCURSOR CFactoryUICLDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}