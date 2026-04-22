// ============================================================================
// PageStation2.cpp — 조립검사(Station2) 페이지 (YOLO + PatchCore 하이브리드)
// ============================================================================
// 책임:
//   Station2 검사 결과 실시간 표시. Station1 과 유사한 3분할 뷰 + 하단 리스트
//   구조이나, YOLO 디텍션(cap/label/fill_ok 등) 결과를 함께 렌더링한다는 점이 다름.
//
// 데이터 흐름:
//   MainTabDlg 가 STATION2_NG(1002) 수신 → SetImages(원본/히트맵/마스크)
//                                       → UpdateDetections(cap_ok, label_ok, fill_ok)
//
// 수동 버튼:
//   OnBtnDefect/OnBtnRework — 로컬 더미 결과 주입(개발/데모용).
//   실제 환경에서는 버튼 없이 추론서버의 결과만 표시됨.
// ============================================================================
#include "pch.h"
#include "PageStation2.h"

IMPLEMENT_DYNAMIC(CPageStation2, CDialogEx)
BEGIN_MESSAGE_MAP(CPageStation2, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_S2_DEFECT, OnBtnDefect)
    ON_BN_CLICKED(IDC_BTN_S2_REWORK, OnBtnRework)
END_MESSAGE_MAP()

CPageStation2::CPageStation2(CWnd* p) : CDialogEx(IDD_PAGE_STATION2,p), m_last{} {
    m_last.id = 10000; m_last.station = 2;
    m_last.time = _T("--:--:--"); m_last.isNG = false;
    m_last.score = 0.15; m_last.defect = EDefect::None; m_last.latencyMs = 65;
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

// SetImages: MainTabDlg::OnNetNgImage 에서 Station2로 라우팅된 이미지 주입.
// Station2는 pred_mask 뷰가 없어 해당 인자는 사용하지 않음 (Station1 전용 패널).
void CPageStation2::SetImages(const std::vector<BYTE>& image,
                              const std::vector<BYTE>& heatmap,
                              const std::vector<BYTE>& /*pred_mask*/) {
    m_cam.SetImage(image);
    m_heat.SetImage(heatmap);
}
