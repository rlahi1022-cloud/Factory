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



// ============================================================================
#include "pch.h"
#include "PageStation2.h"
#include "NetworkClient.h"
#include "PacketBuilder.h"

IMPLEMENT_DYNAMIC(CPageStation2, CDialogEx)
BEGIN_MESSAGE_MAP(CPageStation2, CDialogEx)
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
    DDX_Control(pDX, IDC_CAM2_VIEW,      m_cam);
    DDX_Control(pDX, IDC_HEATMAP2_VIEW,  m_heat);
    DDX_Control(pDX, IDC_NG_LIST2,       m_ngList);  // v0.15.0: NG 이력 리스트
    // v0.16.0: IDC_LIST_YOLO 제거 — YOLO Detections 패널 삭제
}
BOOL CPageStation2::OnInitDialog() {
    CDialogEx::OnInitDialog();
    // v0.16.0: m_listYolo 초기화 제거 — YOLO Detections 패널 삭제

    // v0.14.3: Start/Stop 버튼 초기 상태 — 기본 검사중 가정
    if (CWnd* w = GetDlgItem(IDC_BTN_S2_START)) w->EnableWindow(FALSE);
    if (CWnd* w = GetDlgItem(IDC_BTN_S2_STOP))  w->EnableWindow(TRUE);

    // v0.16.0: Arduino Serial 포트 동적 탐색 — Station1과 동일 방식
    UpdateSerialPortLabel();

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
    // v0.16.0: YOLO 리스트 제거 — m_listYolo 코드 삭제
}
// SetImages: MainTabDlg::OnNetNgImage 에서 Station2로 라우팅된 이미지 주입.
// v0.16.0: Pred Mask 패널 제거로 pred_mask 인자 미사용.
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
    // v0.16.0: YOLO 디텍션 중 첫 번째 NG 항목을 Entry에 주입
    CNgHistoryList::Entry e;
    e.id        = id;
    e.stationId = 2;
    e.score     = score;
    e.time      = timeLabel;

    // m_last.detections 에서 첫 NG 디텍션 추출
    for (const auto& det : m_last.detections) {
        if (!det.ok) {
            e.detClass = det.className;
            e.detConf  = det.confidence;
            e.detOk    = false;
            break;
        }
    }
    // NG 디텍션이 없으면 첫 번째 항목 표시
    if (e.detClass.IsEmpty() && !m_last.detections.empty()) {
        e.detClass = m_last.detections[0].className;
        e.detConf  = m_last.detections[0].confidence;
        e.detOk    = m_last.detections[0].ok;
    }

    m_ngList.AddEntry(id, 2, score, timeLabel, image, heatmap, pred_mask, e);
}

// ============================================================================
// UpdateSerialPortLabel (v0.16.0) — 실제 COM 포트 탐색 → Serial 레이블 갱신
// ============================================================================
void CPageStation2::UpdateSerialPortLabel(int labelId)
{
    CWnd* w = GetDlgItem(labelId);
    if (!w) return;

    CString portText = _T("● 미연결");

    HKEY hKey = nullptr;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                     _T("HARDWARE\\DEVICEMAP\\SERIALCOMM"),
                     0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        TCHAR valueName[64] = {}, portName[64] = {};
        DWORD idx = 0, vnSize = 64, pnSize = 64, type = 0;
        if (RegEnumValue(hKey, idx, valueName, &vnSize,
                         nullptr, &type,
                         reinterpret_cast<BYTE*>(portName), &pnSize) == ERROR_SUCCESS)
        {
            portText.Format(_T("● (%s)"), portName);
        }
        RegCloseKey(hKey);
    }

    w->SetWindowText(portText);
    TRACE(_T("[PageStation2] Serial 포트 탐색 결과: %s\n"), (LPCTSTR)portText);
}
