#include "pch.h"
#include "PageStation1.h"

IMPLEMENT_DYNAMIC(CPageStation1, CDialogEx)
BEGIN_MESSAGE_MAP(CPageStation1, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_S1_OK,      OnBtnOK)
    ON_BN_CLICKED(IDC_BTN_S1_NG,      OnBtnNG)
    ON_BN_CLICKED(IDC_BTN_S1_ARDUINO, OnBtnArduino)
END_MESSAGE_MAP()

CPageStation1::CPageStation1(CWnd* p) : CDialogEx(IDD_PAGE_STATION1, p), m_last{} {
    m_last.id = 10000; m_last.station = 1;
    m_last.time = _T("--:--:--"); m_last.isNG = false;
    m_last.score = 0.12; m_last.defect = EDefect::None; m_last.latencyMs = 52;
}
void CPageStation1::DoDataExchange(CDataExchange* pDX) {
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_CAM1_VIEW,      m_cam);
    DDX_Control(pDX, IDC_HEATMAP1_VIEW,  m_heat);
    DDX_Control(pDX, IDC_PREDMASK1_VIEW, m_mask);  // 패널 3 연결
}
BOOL CPageStation1::OnInitDialog() {
    CDialogEx::OnInitDialog();

    // ── 검사 설정 정보 표시 (목업 대비 추가) ──
    // 목업의 "검사 설정" GroupBox에 해당하는 정적 정보를 채웁니다.
    auto set = [&](int id, LPCTSTR v) {
        CWnd* w = GetDlgItem(id); if (w) w->SetWindowText(v);
    };
    set(IDC_STATIC_S1_CFG_MODEL,    _T("PatchCore v1.2.0"));
    set(IDC_STATIC_S1_CFG_INPUT,    _T("224×224"));
    set(IDC_STATIC_S1_CFG_THRESH,   _T("0.50"));
    set(IDC_STATIC_S1_CFG_BACKBONE, _T("ResNet-18 (사전학습)"));

    Refresh();
    return TRUE;
}
void CPageStation1::Update(const std::vector<InspectionRecord>& recs) {
    for (int i=(int)recs.size()-1;i>=0;--i)
        if (recs[i].station==1){m_last=recs[i];break;}
    Refresh();
}
void CPageStation1::Tick() { m_cam.Tick(); }
void CPageStation1::Refresh() {
    m_cam.SetInspection(1, m_last.isNG, m_last.score, m_last.defect);
    m_heat.SetActive(m_last.isNG);
    // 패널 3: NG 시 마스크 원 표시 (위치는 기본값 사용 — 실서버 연동 시 좌표 수신 예정)
    m_mask.SetMask(m_last.isNG);
    CWnd* w;
    if ((w=GetDlgItem(IDC_STATIC_S1_RESULT))) w->SetWindowText(m_last.isNG?_T("NG"):_T("OK"));
    CString s; s.Format(_T("이상 점수: %.2f  / 임계값: 0.50"), m_last.score);
    if ((w=GetDlgItem(IDC_STATIC_S1_SCORE))) w->SetWindowText(s);
    if ((w=GetDlgItem(IDC_STATIC_S1_LED)))
        w->SetWindowText(m_last.isNG?_T("⚠ NG 경고 LED 점등!"):_T("대기중"));
}
void CPageStation1::OnBtnOK() {
    m_last.isNG=false; m_last.score=0.10; m_last.defect=EDefect::None; Refresh();
}
void CPageStation1::OnBtnNG() {
    m_last.isNG=true; m_last.score=0.85; m_last.defect=EDefect::Anomaly;
    // NG 시뮬레이션: 마스크 위치를 참조 이미지(상단 병목, 중단 몸통)와 맞춤
    m_mask.SetMask(true, 0.55, 0.22, 0.52, 0.52);
    Refresh();
}
void CPageStation1::OnBtnArduino() {
    MessageBox(_T("Arduino COM3 테스트 신호 전송"),_T("Arduino"),MB_OK|MB_ICONINFORMATION);
}
