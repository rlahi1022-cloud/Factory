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
#include "NetworkClient.h"
#include "PacketBuilder.h"

IMPLEMENT_DYNAMIC(CPageStation2, CDialogEx)
BEGIN_MESSAGE_MAP(CPageStation2, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_S2_DEFECT, OnBtnDefect)
    ON_BN_CLICKED(IDC_BTN_S2_REWORK, OnBtnRework)
    ON_BN_CLICKED(IDC_BTN_S2_START,  OnBtnS2Start)   // v0.14.3 2공정 시작
    ON_BN_CLICKED(IDC_BTN_S2_STOP,   OnBtnS2Stop)    // v0.14.3 2공정 중지
END_MESSAGE_MAP()

CPageStation2::CPageStation2(CWnd* p) : CDialogEx(IDD_PAGE_STATION2,p), m_last{} {
    // v0.14.6: 초기 상태 = 검사 대기. 더미 score 제거.
    m_last.id = 0; m_last.station = 2;
    m_last.time = _T("--:--:--"); m_last.isNG = false;
    m_last.score = 0.0; m_last.defect = EDefect::None; m_last.latencyMs = 0;
}
void CPageStation2::DoDataExchange(CDataExchange* pDX) {
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_CAM2_VIEW,     m_cam);
    DDX_Control(pDX, IDC_HEATMAP2_VIEW, m_heat);
    DDX_Control(pDX, IDC_LIST_YOLO,     m_listYolo);
    DDX_Control(pDX, IDC_NG_LIST2,      m_ngList);  // v0.15.0: NG 이력 리스트
}
BOOL CPageStation2::OnInitDialog() {
    CDialogEx::OnInitDialog();
    m_listYolo.SetExtendedStyle(LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
    m_listYolo.InsertColumn(0,_T("클래스"), LVCFMT_LEFT,80);
    m_listYolo.InsertColumn(1,_T("신뢰도"), LVCFMT_LEFT,60);
    m_listYolo.InsertColumn(2,_T("판정"),   LVCFMT_LEFT,50);

    // v0.14.3: Start/Stop 버튼 초기 상태 — 기본 검사중 가정
    if (CWnd* w = GetDlgItem(IDC_BTN_S2_START)) w->EnableWindow(FALSE);
    if (CWnd* w = GetDlgItem(IDC_BTN_S2_STOP))  w->EnableWindow(TRUE);

    Refresh(); return TRUE;
}
// v0.14.6: Station1 과 동일 정책 — NG 만 실시간 창에 반영.
//   OK 레코드는 무시해 점수/결과 깜빡임 차단.
void CPageStation2::Update(const std::vector<InspectionRecord>& recs) {
    for (int i=(int)recs.size()-1; i>=0; --i) {
        if (recs[i].station == 2 && recs[i].isNG) {
            m_last = recs[i];
            Refresh();
            return;
        }
    }
}
void CPageStation2::Tick() { m_cam.Tick(); }
void CPageStation2::Refresh() {
    m_cam.SetInspection(2, m_last.isNG, m_last.score, m_last.defect);
    m_heat.SetActive(m_last.isNG);
    CWnd* w;
    // v0.14.6: s 를 함수 최상단에 선언 — 아래 YOLO 리스트 루프에서도 재사용.
    //   이전 편집에서 if-블록 안에만 선언해 "식별자 s 를 찾을 수 없음" 컴파일 에러 발생.
    CString s;
    // v0.14.6: NG 때만 결과/점수 표시. OK 상태면 "--" / 대기.
    if (m_last.isNG) {
        if ((w = GetDlgItem(IDC_STATIC_S2_RESULT))) w->SetWindowText(_T("NG"));
        s.Format(_T("PatchCore 이상 점수: %.2f"), m_last.score);
        if ((w = GetDlgItem(IDC_STATIC_S2_SCORE))) w->SetWindowText(s);
        if ((w = GetDlgItem(IDC_STATIC_S2_LED)))
            w->SetWindowText(CString(_T("⚠ ")) + QCUtil::DefectName(m_last.defect));
    } else {
        if ((w = GetDlgItem(IDC_STATIC_S2_RESULT))) w->SetWindowText(_T("--"));
        if ((w = GetDlgItem(IDC_STATIC_S2_SCORE)))  w->SetWindowText(_T(""));
        if ((w = GetDlgItem(IDC_STATIC_S2_LED)))    w->SetWindowText(_T("대기중"));
    }
    // v0.15.0: YOLO 리스트 — 서버 수신 실데이터 사용.
    // detections 가 비어있으면 리스트를 비움 (더미 데이터 표시 안 함).
    // 데이터는 MainTabDlg::OnNetNgPush 에서 detections[] 파싱 후 rec.detections 에 저장됨.
    m_listYolo.DeleteAllItems();
    for (int i = 0; i < (int)m_last.detections.size(); ++i) {
        const auto& det = m_last.detections[i];
        m_listYolo.InsertItem(i, det.className);
        s.Format(_T("%.2f"), det.confidence);
        m_listYolo.SetItemText(i, 1, s);
        m_listYolo.SetItemText(i, 2, det.ok ? _T("OK") : _T("NG"));
    }
}
// v0.15.0: 불량 유형 선택 / 재작업 지시 — 서버 재작업 프로토콜(REWORK_REQ) 미확정.
// 확정 후 CPacketBuilder::BuildReworkReq() 추가 및 아래 TODO 구현 예정.
void CPageStation2::OnBtnDefect()
{
    if (!m_net || !m_net->IsConnected()) {
        MessageBox(_T("서버에 연결되어 있지 않습니다."),
                   _T("불량 처리"), MB_OK | MB_ICONWARNING);
        return;
    }
    // TODO: 서버 REWORK_REQ 프로토콜 확정 후 구현
    MessageBox(_T("불량 유형 선택 기능은 서버 프로토콜 확정 후 구현됩니다."),
               _T("알림"), MB_OK | MB_ICONINFORMATION);
}

void CPageStation2::OnBtnRework()
{
    if (!m_net || !m_net->IsConnected()) {
        MessageBox(_T("서버에 연결되어 있지 않습니다."),
                   _T("재작업 지시"), MB_OK | MB_ICONWARNING);
        return;
    }
    // TODO: 서버 REWORK_REQ 프로토콜 확정 후 구현
    MessageBox(_T("재작업 지시 기능은 서버 프로토콜 확정 후 구현됩니다."),
               _T("재작업"), MB_OK | MB_ICONINFORMATION);
}

// SetImages: MainTabDlg::OnNetNgImage 에서 Station2로 라우팅된 이미지 주입.
// Station2는 pred_mask 뷰가 없어 해당 인자는 사용하지 않음 (Station1 전용 패널).
void CPageStation2::SetImages(const std::vector<BYTE>& image,
                              const std::vector<BYTE>& heatmap,
                              const std::vector<BYTE>& /*pred_mask*/) {
    m_cam.SetImage(image);
    m_heat.SetImage(heatmap);
}

// ============================================================================
// v0.14.3 — 2공정 검사 Start/Stop (INSPECT_CONTROL_REQ station_filter=2)
// ============================================================================
void CPageStation2::OnBtnS2Start()
{
    if (!m_net || !m_net->IsConnected()) {
        MessageBox(_T("서버에 연결되어 있지 않습니다."),
                   _T("검사 시작"), MB_OK | MB_ICONWARNING);
        return;
    }
    CString req = CPacketBuilder::BuildInspectControlReq(2 /*station2*/, _T("resume"));
    m_net->SendJson(req);
    TRACE(_T("[PageStation2] 검사 시작 요청 (station=2, resume)\n"));
    if (CWnd* w = GetDlgItem(IDC_BTN_S2_START)) w->EnableWindow(FALSE);
    if (CWnd* w = GetDlgItem(IDC_BTN_S2_STOP))  w->EnableWindow(TRUE);
}

void CPageStation2::OnBtnS2Stop()
{
    if (!m_net || !m_net->IsConnected()) {
        MessageBox(_T("서버에 연결되어 있지 않습니다."),
                   _T("검사 중지"), MB_OK | MB_ICONWARNING);
        return;
    }
    CString req = CPacketBuilder::BuildInspectControlReq(2 /*station2*/, _T("pause"));
    m_net->SendJson(req);
    TRACE(_T("[PageStation2] 검사 중지 요청 (station=2, pause)\n"));
    if (CWnd* w = GetDlgItem(IDC_BTN_S2_START)) w->EnableWindow(TRUE);
    if (CWnd* w = GetDlgItem(IDC_BTN_S2_STOP))  w->EnableWindow(FALSE);
}

// ============================================================================
// AddNgEntry (v0.15.0) — NG 이력 리스트 누적 (Station1과 동일)
// ============================================================================
void CPageStation2::AddNgEntry(int id, double score, const CString& timeLabel,
                               const std::vector<BYTE>& image,
                               const std::vector<BYTE>& heatmap,
                               const std::vector<BYTE>& pred_mask) {
    m_ngList.AddEntry(id, 2 /*stationId*/, score, timeLabel, image, heatmap, pred_mask);
}
