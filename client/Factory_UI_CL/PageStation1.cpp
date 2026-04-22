// ============================================================================
// PageStation1.cpp — 입고검사(Station1) 페이지 (PatchCore 이상탐지)
// ============================================================================
// 책임:
//   Station1 의 실시간 NG 결과 + 최근 10건 이력을 표시.
//   - 상단 3분할 뷰: 원본 / 히트맵 / Pred Mask 이미지
//   - 하단 리스트: NG 발생 10건의 세로 썸네일 + 메타정보
//   - 수동 테스트 버튼(OnBtnOK/NG/Arduino) — 로컬 디버깅용
//
// 데이터 원천:
//   MainTabDlg::OnNetNgImage → SetImages() (상단 3뷰 덮어쓰기)
//                           → AddNgEntry() (하단 리스트 누적)
//
// 상단 덮어쓰기 + 하단 누적 정책:
//   상단은 "가장 최근 NG" 를 강조, 하단은 "최근 N건 이력" 을 추적.
//   10건 초과 시 가장 오래된 항목부터 제거.
// ============================================================================
#include "pch.h"
#include "PageStation1.h"
#include "NetworkClient.h"
#include "PacketBuilder.h"

IMPLEMENT_DYNAMIC(CPageStation1, CDialogEx)
BEGIN_MESSAGE_MAP(CPageStation1, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_S1_OK,      OnBtnOK)
    ON_BN_CLICKED(IDC_BTN_S1_NG,      OnBtnNG)
    ON_BN_CLICKED(IDC_BTN_S1_ARDUINO, OnBtnArduino)
    ON_BN_CLICKED(IDC_BTN_S1_START,   OnBtnS1Start)   // v0.14.3 1공정 시작
    ON_BN_CLICKED(IDC_BTN_S1_STOP,    OnBtnS1Stop)    // v0.14.3 1공정 중지
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
    DDX_Control(pDX, IDC_NG_LIST1,       m_ngList); // 하단 NG 이벤트 리스트
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

    // v0.14.3: Start/Stop 버튼 초기 상태 — 기본 "검사 중"으로 간주해 Start 비활성
    //   (추론서버가 기본적으로 running 상태이므로)
    if (CWnd* w = GetDlgItem(IDC_BTN_S1_START)) w->EnableWindow(FALSE);
    if (CWnd* w = GetDlgItem(IDC_BTN_S1_STOP))  w->EnableWindow(TRUE);

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

// SetImages: MainTabDlg::OnNetNgImage 에서 수신한 3장 바이너리를 각 뷰에 주입.
// 비어있는 벡터는 SetImage 내부에서 "이미지 해제"로 처리되어 플레이스홀더로 복귀.
void CPageStation1::SetImages(const std::vector<BYTE>& image,
                              const std::vector<BYTE>& heatmap,
                              const std::vector<BYTE>& pred_mask) {
    m_cam.SetImage(image);
    m_heat.SetImage(heatmap);
    m_mask.SetImage(pred_mask);
}

// AddNgEntry: 하단 NG 이력 리스트에 누적. 상단 SetImages와 독립 — 둘 다 호출하면
// 최신 1건은 상단 대형, 최대 10건은 하단 리스트.
void CPageStation1::AddNgEntry(int id, double score, const CString& timeLabel,
                               const std::vector<BYTE>& image,
                               const std::vector<BYTE>& heatmap,
                               const std::vector<BYTE>& pred_mask) {
    m_ngList.AddEntry(id, 1 /*stationId*/, score, timeLabel, image, heatmap, pred_mask);
}

// ============================================================================
// v0.14.3 — 1공정 검사 Start/Stop
// ============================================================================
// 서버에 INSPECT_CONTROL_REQ(160) station_filter=1 전송.
// 서버 측 StationRunner 가 _pause_event 를 set/clear → grab 루프 일시정지/재개.
// 응답(161) 은 MainTabDlg 가 받아 MessageBox 로 알림.
//
// 버튼 상태:
//   실행 중 → Start 비활성, Stop 활성
//   정지 중 → Stop 비활성, Start 활성
// 낙관적 업데이트 (서버 응답 전 UI 먼저 토글). 실패 응답 오면 롤백 로직은
// 추후 보강 (현재는 응답이 항상 성공으로 가정).
void CPageStation1::OnBtnS1Start()
{
    if (!m_net || !m_net->IsConnected()) {
        MessageBox(_T("서버에 연결되어 있지 않습니다."),
                   _T("검사 시작"), MB_OK | MB_ICONWARNING);
        return;
    }
    CString req = CPacketBuilder::BuildInspectControlReq(1 /*station1*/, _T("resume"));
    m_net->SendJson(req);
    TRACE(_T("[PageStation1] 검사 시작 요청 (station=1, resume)\n"));

    // UI 낙관적 업데이트
    if (CWnd* w = GetDlgItem(IDC_BTN_S1_START)) w->EnableWindow(FALSE);
    if (CWnd* w = GetDlgItem(IDC_BTN_S1_STOP))  w->EnableWindow(TRUE);
}

void CPageStation1::OnBtnS1Stop()
{
    if (!m_net || !m_net->IsConnected()) {
        MessageBox(_T("서버에 연결되어 있지 않습니다."),
                   _T("검사 중지"), MB_OK | MB_ICONWARNING);
        return;
    }
    CString req = CPacketBuilder::BuildInspectControlReq(1 /*station1*/, _T("pause"));
    m_net->SendJson(req);
    TRACE(_T("[PageStation1] 검사 중지 요청 (station=1, pause)\n"));

    if (CWnd* w = GetDlgItem(IDC_BTN_S1_START)) w->EnableWindow(TRUE);
    if (CWnd* w = GetDlgItem(IDC_BTN_S1_STOP))  w->EnableWindow(FALSE);
}
