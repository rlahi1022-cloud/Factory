#include "pch.h"
#include "PageStation2.h"

IMPLEMENT_DYNAMIC(CPageStation2, CDialogEx)
BEGIN_MESSAGE_MAP(CPageStation2, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_S2_DEFECT, OnBtnDefect)
    ON_BN_CLICKED(IDC_BTN_S2_REWORK, OnBtnRework)
END_MESSAGE_MAP()

CPageStation2::CPageStation2(CWnd* p) : CDialogEx(IDD_PAGE_STATION2,p) {
    m_last={10000,2,_T("--:--:--"),false,0.15,EDefect::None,65};
}
void CPageStation2::DoDataExchange(CDataExchange* pDX) {
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_CAM2_VIEW,     m_cam);
    DDX_Control(pDX, IDC_HEATMAP2_VIEW, m_heat);
    DDX_Control(pDX, IDC_LIST_YOLO,     m_listYolo);
}
BOOL CPageStation2::OnInitDialog() {
    CDialogEx::OnInitDialog();
    m_listYolo.SetExtendedStyle(LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
    m_listYolo.InsertColumn(0,_T("클래스"), LVCFMT_LEFT,80);
    m_listYolo.InsertColumn(1,_T("신뢰도"), LVCFMT_LEFT,60);
    m_listYolo.InsertColumn(2,_T("판정"),   LVCFMT_LEFT,50);
    Refresh(); return TRUE;
}
void CPageStation2::Update(const std::vector<InspectionRecord>& recs) {
    for (int i=(int)recs.size()-1;i>=0;--i)
        if(recs[i].station==2){m_last=recs[i];break;}
    Refresh();
}
void CPageStation2::Tick() { m_cam.Tick(); }
void CPageStation2::Refresh() {
    m_cam.SetInspection(2,m_last.isNG,m_last.score,m_last.defect);
    m_heat.SetActive(m_last.isNG);
    CWnd* w;
    if ((w=GetDlgItem(IDC_STATIC_S2_RESULT))) w->SetWindowText(m_last.isNG?_T("NG"):_T("OK"));
    CString s; s.Format(_T("PatchCore 이상 점수: %.2f"),m_last.score);
    if ((w=GetDlgItem(IDC_STATIC_S2_SCORE))) w->SetWindowText(s);
    if ((w=GetDlgItem(IDC_STATIC_S2_LED)))
        w->SetWindowText(m_last.isNG?CString(_T("⚠ "))+QCUtil::DefectName(m_last.defect):_T("대기중"));
    // YOLO list
    m_listYolo.DeleteAllItems();
    struct Det{LPCTSTR cls;double conf;bool ok;};
    Det dets[]={{_T("cap"),0.94,!m_last.isNG},{_T("label"),0.91,!m_last.isNG||(rand()%3!=0)},{_T("liquid_level"),0.96,true}};
    for(int i=0;i<3;++i){
        m_listYolo.InsertItem(i,dets[i].cls);
        s.Format(_T("%.2f"),dets[i].conf); m_listYolo.SetItemText(i,1,s);
        m_listYolo.SetItemText(i,2,dets[i].ok?_T("OK"):_T("NG"));
    }
}
void CPageStation2::OnBtnDefect(){MessageBox(_T("불량 유형 선택\n(구현 예정)"),_T("알림"),MB_OK);}
void CPageStation2::OnBtnRework(){MessageBox(_T("재작업 지시 전송"),_T("재작업"),MB_OK|MB_ICONWARNING);}
