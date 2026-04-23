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
    // v0.14.7: Manual OK/NG/Arduino 테스트 버튼 제거 — 리소스에서도 삭제됨.
    ON_BN_CLICKED(IDC_BTN_S1_START,   OnBtnS1Start)   // v0.14.3 1공정 시작
    ON_BN_CLICKED(IDC_BTN_S1_STOP,    OnBtnS1Stop)    // v0.14.3 1공정 중지
END_MESSAGE_MAP()

CPageStation1::CPageStation1(CWnd* p) : CDialogEx(IDD_PAGE_STATION1, p), m_last{} {
    // v0.14.6: 초기 상태 = 검사 대기(OK, 점수 0).
    //   이전엔 score=0.12 더미값이 최초 화면에 잠깐 찍혔던 것을 0.0 으로 정리.
    m_last.id = 0; m_last.station = 1;
    m_last.time = _T("--:--:--"); m_last.isNG = false;
    m_last.score = 0.0; m_last.defect = EDefect::None; m_last.latencyMs = 0;
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
    // v0.15.0: 모델 정보는 서버 MODEL_LIST_RES(151) 수신 시 갱신됨.
    // 초기값은 로딩 중임을 표시 — 실제값은 MainTabDlg::OnNetResponse 에서 주입.
    set(IDC_STATIC_S1_CFG_MODEL,    _T("로딩 중..."));
    set(IDC_STATIC_S1_CFG_INPUT,    _T("-"));
    set(IDC_STATIC_S1_CFG_THRESH,   _T("-"));
    set(IDC_STATIC_S1_CFG_BACKBONE, _T("-"));

    // v0.14.3: Start/Stop 버튼 초기 상태 — 기본 "검사 중"으로 간주해 Start 비활성
    //   (추론서버가 기본적으로 running 상태이므로)
    if (CWnd* w = GetDlgItem(IDC_BTN_S1_START)) w->EnableWindow(FALSE);
    if (CWnd* w = GetDlgItem(IDC_BTN_S1_STOP))  w->EnableWindow(TRUE);

    Refresh();
    return TRUE;
}
// v0.14.6: 실시간 결과 창은 **NG 만** 반영.
//   이전엔 OK 레코드가 들어올 때마다 결과/점수가 "OK / 0.xx" 로 깜빡였는데
//   사용자 요청으로 OK 는 화면 표시 대상에서 제외(점수 표시도 끈다).
//   OK 레코드는 통계/카운터에는 반영되지만 상단 실시간 창은 마지막 NG 상태를 유지.
void CPageStation1::Update(const std::vector<InspectionRecord>& recs) {
    for (int i=(int)recs.size()-1; i>=0; --i) {
        if (recs[i].station == 1 && recs[i].isNG) {  // NG 만 적용
            m_last = recs[i];
            Refresh();
            return;
        }
    }
    // 최근 NG 가 없으면 아무 것도 바꾸지 않음 — 이전 상태 유지.
}
void CPageStation1::Tick() { m_cam.Tick(); }
void CPageStation1::Refresh() {
    m_cam.SetInspection(1, m_last.isNG, m_last.score, m_last.defect);
    m_heat.SetActive(m_last.isNG);
    // v0.15.0: 마스크 좌표는 서버 NG_PUSH JSON 에 bbox 필드가 없어 기본값 사용 중.
    // 서버측 bbox 좌표 제공 시 SetMask(true, cx, cy) 로 변경 예정.
    m_mask.SetMask(m_last.isNG);
    CWnd* w;
    // v0.14.7: Result 패널에서 "임계값" 표기 제거 — 점수만 표시.
    if (m_last.isNG) {
        if ((w = GetDlgItem(IDC_STATIC_S1_RESULT))) w->SetWindowText(_T("NG"));
        CString s; s.Format(_T("이상 점수: %.2f"), m_last.score);
        if ((w = GetDlgItem(IDC_STATIC_S1_SCORE))) w->SetWindowText(s);
        if ((w = GetDlgItem(IDC_STATIC_S1_LED)))
            w->SetWindowText(_T("⚠ NG 경고 LED 점등!"));
    } else {
        if ((w = GetDlgItem(IDC_STATIC_S1_RESULT))) w->SetWindowText(_T("--"));
        if ((w = GetDlgItem(IDC_STATIC_S1_SCORE)))  w->SetWindowText(_T("이상 점수: 0.00"));
        if ((w = GetDlgItem(IDC_STATIC_S1_LED)))    w->SetWindowText(_T("대기중"));
    }
}
// v0.14.7: Manual OK/NG/Arduino 테스트 버튼 완전 제거 (RC + 핸들러 + 선언).

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
// PopulateNgHistoryFromJson (v0.14.6) — 로그인 직후 DB 이력으로 하단 리스트 초기화.
// ============================================================================
// INSPECT_HISTORY_RES(115) 의 items[] 중 station_id=1 && result=="ng" 만 골라
// 최신순(응답 배열 자체가 최신순) 으로 최대 10건을 m_ngList 에 주입.
// 이미지 없이 메타정보(id/score/time) 만 넣음 — 텍스트 전용 리스트이므로 충분.
// 이후 실시간 NG_PUSH 는 기존 AddNgEntry 경로로 맨 위에 prepend 됨.
void CPageStation1::PopulateNgHistoryFromJson(const std::string& json)
{
    CStringA jsonA(json.c_str());

    // items 배열만 추출 (경량 파서 — PageHome::OnInspectHistoryRes 와 동일 로직)
    int arrStart = jsonA.Find("\"items\"");
    if (arrStart < 0) return;
    int arrS = jsonA.Find('[', arrStart);
    int arrE = jsonA.Find(']', arrS);
    if (arrS < 0 || arrE < 0) return;

    CStringA arr = jsonA.Mid(arrS + 1, arrE - arrS - 1);

    // 기존 리스트 비우고 새로 채움 (중복 누적 방지)
    m_ngList.Clear();

    // 1) 서버 items[] 를 순회하며 Station1 NG 만 임시 벡터에 수집.
    struct Row { int id; double score; CString time; };
    std::vector<Row> rows;
    int pos = 0;
    while (pos < arr.GetLength() && rows.size() < 10) {
        int os = arr.Find('{', pos);
        int oe = arr.Find('}', os);
        if (os < 0 || oe < 0) break;

        CStringA obj = arr.Mid(os, oe - os + 1);

        int stationId = CPacketBuilder::ExtractInt(obj, "station_id");
        CString resultStr = CPacketBuilder::ExtractStringW(obj, "result");
        resultStr.MakeLower();
        if (stationId != 1 || resultStr != _T("ng")) {
            pos = oe + 1;
            continue;
        }

        Row r;
        r.id    = CPacketBuilder::ExtractInt(obj, "id");
        r.score = CPacketBuilder::ExtractDouble(obj, "confidence");
        CString ts = CPacketBuilder::ExtractStringW(obj, "timestamp");
        // "YYYY-MM-DD HH:MM:SS" 또는 "YYYY-MM-DDTHH:MM:SS" → "HH:MM:SS"
        r.time = (ts.GetLength() >= 19) ? ts.Mid(11, 8) : CString(_T("--:--:--"));
        rows.push_back(r);

        pos = oe + 1;
    }

    // 2) m_ngList.AddEntry 는 항상 push_front — 입력 역순으로 쌓임.
    //    서버 items 가 최신순(최신 index=0) 이라 가정 시, 그대로 넣으면 최신이 밑으로 가버림.
    //    역순으로 넣어야 최신이 맨 위.
    const std::vector<BYTE> empty;
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
        m_ngList.AddEntry(it->id, 1, it->score, it->time, empty, empty, empty);
    }

    TRACE(_T("[PageStation1] DB NG 이력 초기 로드: %d건\n"), (int)rows.size());
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

// ============================================================================
// UpdateModelInfo (v0.15.0) — MODEL_LIST_RES(151) 수신 시 검사 설정 정보 갱신.
// ============================================================================
// 서버 응답 예시:
//   {"protocol_no":151,"models":[
//     {"id":1,"station_id":1,"model_type":"PatchCore","version":"v20260422_1530",
//      "accuracy":0.95,"deployed_at":"2026-04-22 15:30:00","is_active":1}, ...]}
//
// Station1 은 station_id=1 && is_active=1 인 PatchCore 모델을 찾아
// IDC_STATIC_S1_CFG_MODEL 에 "PatchCore v20260422_1530" 형식으로 표시.
// input_size/backbone/threshold 는 서버에서 오지 않으므로 프로젝트 기본값 고정 표기.
void CPageStation1::UpdateModelInfo(const std::string& json)
{
    CStringA jsonA(json.c_str());

    int arrStart = jsonA.Find("\"models\"");
    if (arrStart < 0) return;
    int arrS = jsonA.Find('[', arrStart);
    int arrE = jsonA.Find(']', arrS);
    if (arrS < 0 || arrE < 0) return;

    CStringA arr = jsonA.Mid(arrS + 1, arrE - arrS - 1);

    // station_id=1 && is_active=1 인 최초 모델만 사용.
    CStringA modelType, version;
    int pos = 0;
    while (pos < arr.GetLength()) {
        int os = arr.Find('{', pos);
        int oe = arr.Find('}', os);
        if (os < 0 || oe < 0) break;
        CStringA obj = arr.Mid(os, oe - os + 1);

        int sid    = CPacketBuilder::ExtractInt(obj, "station_id");
        int active = CPacketBuilder::ExtractInt(obj, "is_active");
        if (sid == 1 && active == 1) {
            modelType = CPacketBuilder::ExtractString(obj, "model_type");
            version   = CPacketBuilder::ExtractString(obj, "version");
            break;
        }
        pos = oe + 1;
    }

    auto set = [&](int id, LPCTSTR v) {
        CWnd* w = GetDlgItem(id); if (w) w->SetWindowText(v);
    };

    if (!modelType.IsEmpty()) {
        CString label;
        label.Format(_T("%S %S"), (LPCSTR)modelType,
                     version.IsEmpty() ? "(no-ver)" : (LPCSTR)version);
        set(IDC_STATIC_S1_CFG_MODEL, label);
        // 서버가 아직 input_size/backbone/threshold 를 보내지 않으므로 프로젝트 기본값 표기.
        set(IDC_STATIC_S1_CFG_INPUT,    _T("224×224"));
        set(IDC_STATIC_S1_CFG_THRESH,   _T("AUTO (F1 최적화)"));
        set(IDC_STATIC_S1_CFG_BACKBONE, _T("ResNet-18 (사전학습)"));
        TRACE(_T("[PageStation1] 모델 정보 갱신: %s\n"), (LPCTSTR)label);
    } else {
        set(IDC_STATIC_S1_CFG_MODEL,    _T("미배포"));
        set(IDC_STATIC_S1_CFG_INPUT,    _T("-"));
        set(IDC_STATIC_S1_CFG_THRESH,   _T("-"));
        set(IDC_STATIC_S1_CFG_BACKBONE, _T("-"));
    }
}
