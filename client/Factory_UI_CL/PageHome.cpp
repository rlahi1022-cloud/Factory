// ============================================================================
// PageHome.cpp — 종합 현황 (Home) 대시보드 페이지
// ============================================================================
// 책임:
//   MainTabDlg 가 PushUpdate() 호출 시 InspectionRecord 컬렉션을 받아
//   종합 통계를 계산/표시. 실시간 NG 이벤트가 들어올 때마다 카운트/불량률 갱신.
//
// 표시 항목:
//   - 전체 OK/NG 카운트 + 불량률(%)
//   - Station1(입고) / Station2(조립) 개별 OK/NG
//   - 최근 NG 이력 리스트 (최근 N건)
//
// 데이터 원천:
//   초기 로드 — STATS_REQ(130) 응답으로 누적 수치 수신
//   실시간   — INSPECT_NG_PUSH(110), INSPECT_OK_COUNT_PUSH(112) 로 증분 갱신
//   이력     — INSPECT_HISTORY_REQ(114) 응답으로 NG 로그 테이블 채움
// ============================================================================

#include "pch.h"
#include "PageHome.h"
#include "NetworkClient.h"
#include "PacketBuilder.h"   // ExtractInt/String/Double/Bool

IMPLEMENT_DYNAMIC(CPageHome, CDialogEx)
BEGIN_MESSAGE_MAP(CPageHome, CDialogEx)
    ON_WM_PAINT()
    // v0.14.6: NG 리스트 더블클릭 → 이미지 요청
    ON_NOTIFY(NM_DBLCLK, IDC_LIST_NG, &CPageHome::OnLvnDoubleClickNgList)
END_MESSAGE_MAP()

CPageHome::CPageHome(CWnd* p) : CDialogEx(IDD_PAGE_HOME, p) {}

void CPageHome::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    // NG 이력 리스트 컨트롤 바인딩
    DDX_Control(pDX, IDC_LIST_NG, m_listNG);
}

BOOL CPageHome::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // NG 이력 리스트뷰 설정
    m_listNG.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    m_listNG.InsertColumn(0, _T("ID"),       LVCFMT_LEFT, 50);
    m_listNG.InsertColumn(1, _T("스테이션"), LVCFMT_LEFT, 60);
    m_listNG.InsertColumn(2, _T("시각"),     LVCFMT_LEFT, 70);
    m_listNG.InsertColumn(3, _T("결과"),     LVCFMT_LEFT, 40);
    m_listNG.InsertColumn(4, _T("점수"),     LVCFMT_LEFT, 50);
    m_listNG.InsertColumn(5, _T("결함"),     LVCFMT_LEFT, 110);
    m_listNG.InsertColumn(6, _T("Latency"),  LVCFMT_LEFT, 60);

    return TRUE;
}

// ============================================================================
// Update — 검사 데이터 갱신
// ============================================================================
// 파라미터: recs — 전체 검사 이력 (최대 50건)
// 동작:
//   1) 종합 통계 계산 및 표시
//   2) 스테이션별 통계 계산 및 표시 (목업 대비 추가)
//   3) NG 이력 리스트 갱신
// v0.14.7: Summary 값은 **누적 카운터(m_cumOk/m_cumNg)** 기반으로만 찍힌다.
//   로그인 직후 ApplyStatsRes 가 DB 절대값을 세팅하고,
//   이후엔 OK_COUNT_PUSH / NG_PUSH 가 증분을 반영 → RefreshSummary 호출로 재그림.
//   Update(recs) 는 이제 Summary 를 건드리지 않음 — 50건 cap 문제 해소.
void CPageHome::Update(const std::vector<InspectionRecord>& /*recs*/)
{
    // Summary 는 RefreshSummary 에서 담당. 여기선 모델 정보만.
    auto set = [&](int id, CString v) {
        CWnd* w = GetDlgItem(id);
        if (w) w->SetWindowText(v);
    };
    // v0.15.0: 모델 정보는 서버 MODEL_LIST_RES(151) 수신 시 SetModelInfo()로 주입.
    // 초기 하드코딩 제거 — 로딩 전까지 빈 문자열 유지.
    // (기존: "모델: PatchCore v1.2.0 | Latency: ~52ms" 등 하드코딩)

    // 누적 카운터로 Summary/스테이션 박스 다시 그림
    RefreshSummary();
}

void CPageHome::UpdateStationCount(int stationId, int okCount, int ngCount)
{
    // v0.14.7: 서버 절대값으로 덮어쓰기 — OK_COUNT_PUSH(112) 는 누적 카운트.
    if (stationId == 1 || stationId == 2) {
        m_cumOk[stationId] = okCount;
        m_cumNg[stationId] = ngCount;
    }
    RefreshSummary();
}

// v0.14.7: 클라 시작 이후 "현재까지의 누적 수치"로 Summary + 스테이션 박스 갱신.
void CPageHome::RefreshSummary()
{
    auto set = [&](int id, CString v) {
        CWnd* w = GetDlgItem(id);
        if (w) w->SetWindowText(v);
    };
    CString s;

    // ── 스테이션별 박스 ──
    s.Format(_T("%d"), m_cumOk[1]);  set(IDC_STATIC_S1_OK, s);
    s.Format(_T("%d"), m_cumNg[1]);  set(IDC_STATIC_S1_NG, s);
    s.Format(_T("%d"), m_cumOk[2]);  set(IDC_STATIC_S2_OK, s);
    s.Format(_T("%d"), m_cumNg[2]);  set(IDC_STATIC_S2_NG, s);

    // ── 종합 Summary (Total / OK / NG / Defect Rate) ──
    int ok    = m_cumOk[1] + m_cumOk[2];
    int ng    = m_cumNg[1] + m_cumNg[2];
    int total = ok + ng;

    s.Format(_T("%d"), total); set(IDC_STATIC_TOTAL, s);
    s.Format(_T("%d"), ok);    set(IDC_STATIC_OK, s);
    s.Format(_T("%d"), ng);    set(IDC_STATIC_NG, s);
    s.Format(_T("%.2f%%"), total > 0 ? 100.0 * ng / total : 0.0);
    set(IDC_STATIC_DEFECT_RATE, s);
}

// v0.14.7: 로그인 직후 STATS_RES(130) 응답으로 초기 누적값 세팅.
//   서버 JSON 필드: total, ok_count, ng_count, s1_ok, s1_ng, s2_ok, s2_ng ...
//   station 별 필드가 있으면 그걸 쓰고, 없으면 합계로 폴백.
void CPageHome::ApplyStatsRes(const std::string& json)
{
    CStringA jsonA(json.c_str());
    int s1Ok = CPacketBuilder::ExtractInt(jsonA, "s1_ok");
    int s1Ng = CPacketBuilder::ExtractInt(jsonA, "s1_ng");
    int s2Ok = CPacketBuilder::ExtractInt(jsonA, "s2_ok");
    int s2Ng = CPacketBuilder::ExtractInt(jsonA, "s2_ng");
    // station 필드가 비어있으면(구서버) 합계로 Station1 에 몰아넣기
    if (s1Ok == 0 && s1Ng == 0 && s2Ok == 0 && s2Ng == 0) {
        int total = CPacketBuilder::ExtractInt(jsonA, "total");
        int okC   = CPacketBuilder::ExtractInt(jsonA, "ok_count");
        int ngC   = CPacketBuilder::ExtractInt(jsonA, "ng_count");
        if (total > 0 && okC == 0 && ngC == 0) {
            ngC = CPacketBuilder::ExtractInt(jsonA, "ng");
            okC = total - ngC;
        }
        s1Ok = okC; s1Ng = ngC;
    }
    m_cumOk[1] = s1Ok; m_cumNg[1] = s1Ng;
    m_cumOk[2] = s2Ok; m_cumNg[2] = s2Ng;
    RefreshSummary();
}

void CPageHome::OnPaint() { Default(); }

// ============================================================================
// InsertNgItem — NG 레코드 1건을 리스트 지정 row 에 삽입 (v0.13.2)
// ============================================================================
void CPageHome::InsertNgItem(int row, const InspectionRecord& r)
{
    CString s;
    s.Format(_T("%d"), r.id);
    m_listNG.InsertItem(row, s);

    s.Format(_T("#%d"), r.station);
    m_listNG.SetItemText(row, 1, s);

    m_listNG.SetItemText(row, 2, r.time);
    m_listNG.SetItemText(row, 3, r.isNG ? _T("NG") : _T("OK"));

    s.Format(_T("%.2f"), r.score);
    m_listNG.SetItemText(row, 4, s);

    m_listNG.SetItemText(row, 5, QCUtil::DefectName(r.defect));

    s.Format(_T("%dms"), r.latencyMs);
    m_listNG.SetItemText(row, 6, s);
}

// ============================================================================
// OnInspectHistoryRes — DB 이력 응답(115) 수신 → NG 리스트 초기화 (v0.13.2)
// ============================================================================
// 접속 직후 MainTabDlg 가 INSPECT_HISTORY_REQ 를 보내면 서버가 응답으로 items
// 배열을 돌려준다. 여기서는 items 중 result=="ng" 만 뽑아 최신순으로 리스트에
// 채운다 (최대 MAX_NG_ROWS 건). 스크롤은 MFC CListCtrl 이 자동 처리.
void CPageHome::OnInspectHistoryRes(const std::string& json)
{
    CStringA jsonA(json.c_str());

    // JSON 안의 items 배열만 추출 (1-depth 플랫 파서라 배열 통째로 자름).
    int arrStart = jsonA.Find("\"items\"");
    if (arrStart < 0) return;
    int arrS = jsonA.Find('[', arrStart);
    int arrE = jsonA.Find(']', arrS);
    if (arrS < 0 || arrE < 0) return;

    CStringA arr = jsonA.Mid(arrS + 1, arrE - arrS - 1);

    m_listNG.DeleteAllItems();
    int row = 0;
    int pos = 0;
    while (pos < arr.GetLength() && row < MAX_NG_ROWS) {
        int os = arr.Find('{', pos);
        int oe = arr.Find('}', os);
        if (os < 0 || oe < 0) break;

        CStringA obj = arr.Mid(os, oe - os + 1);

        // 결과 필터 — NG(=ng) 만 표시. OK 는 건너뜀.
        CString resultStr = CPacketBuilder::ExtractStringW(obj, "result");
        resultStr.MakeLower();
        if (resultStr != _T("ng")) {
            pos = oe + 1;
            continue;
        }

        InspectionRecord r;
        r.id        = CPacketBuilder::ExtractInt(obj, "id");
        r.station   = CPacketBuilder::ExtractInt(obj, "station_id");
        r.isNG      = true;
        r.score     = CPacketBuilder::ExtractDouble(obj, "confidence");
        r.latencyMs = CPacketBuilder::ExtractInt(obj, "latency_ms");

        // timestamp 는 "YYYY-MM-DD HH:MM:SS" 형식 — 시:분:초 부분만 뽑아 표시
        CString ts = CPacketBuilder::ExtractStringW(obj, "timestamp");
        int sp = ts.Find(_T(' '));
        r.time = (sp >= 0 && ts.GetLength() - sp >= 9)
                 ? ts.Mid(sp + 1, 8)
                 : ts;

        // defect_type 문자열 → EDefect 매핑 (UI 표시용)
        CString defectStr = CPacketBuilder::ExtractStringW(obj, "defect_type");
        defectStr.MakeLower();
        if      (defectStr.Find(_T("cap"))    >= 0) r.defect = EDefect::CapLoose;
        else if (defectStr.Find(_T("label"))  >= 0) r.defect = EDefect::LabelTilt;
        else if (defectStr.Find(_T("fill"))   >= 0) r.defect = EDefect::FillLow;
        else if (defectStr.IsEmpty())               r.defect = EDefect::None;
        else                                        r.defect = EDefect::Anomaly;

        InsertNgItem(row, r);
        ++row;
        pos = oe + 1;
    }

    TRACE(_T("[PageHome] DB NG 이력 로드: %d건\n"), row);
}

// ============================================================================
// AddNgRow — 실시간 NG_PUSH 수신 → 리스트 맨 위에 1건 prepend (v0.13.2)
// ============================================================================
// 상한 MAX_NG_ROWS 초과 시 가장 오래된 행(맨 아래) 자동 제거.
void CPageHome::AddNgRow(const InspectionRecord& r)
{
    if (!r.isNG) return;  // OK 는 이 리스트에 표시하지 않음
    InsertNgItem(0, r);   // 맨 위에 삽입

    // 상한 초과 시 꼬리 제거 (오래된 NG)
    int total = m_listNG.GetItemCount();
    while (total > MAX_NG_ROWS) {
        m_listNG.DeleteItem(total - 1);
        --total;
    }

    // v0.14.7: Summary 누적 NG 카운터 +1 (station 별).
    //   OK_COUNT_PUSH(112) 가 다음에 올 때 서버 절대값으로 덮어쓰므로 일시적 오차는 자동 수렴.
    if (r.station == 1 || r.station == 2) {
        ++m_cumNg[r.station];
    }
    RefreshSummary();
}

// ============================================================================
// OnLvnDoubleClickNgList (v0.14.6) — 더블클릭 → 해당 NG 이미지 로드 + 탭 전환
// ============================================================================
// 흐름:
//   1) 클릭된 row 에서 ID 컬럼(0) 과 스테이션 컬럼(1) 텍스트 추출
//   2) CNetworkClient::SendJson 으로 INSPECT_IMAGE_REQ(116) 전송
//      → 서버가 해당 inspection_id 의 원본/히트맵/마스크 3장을 INSPECT_IMAGE_RES(117)
//         로 회신 → NetworkClient 가 WM_NET_NG_IMAGE 로 UI에 전달
//      → MainTabDlg::OnNetNgImage 가 station_id 에 따라 Station1/2 페이지에 주입
//   3) m_onRequestShowImage 콜백으로 MainTabDlg 에 "Station N 탭으로 전환" 요청.
//      사용자가 이미지 즉시 확인 가능.
void CPageHome::OnLvnDoubleClickNgList(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMITEMACTIVATE p = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
    *pResult = 0;

    const int row = p->iItem;
    if (row < 0) return;

    // 컬럼 0: ID (정수). 컬럼 1: "#1" / "#2" 형태 스테이션.
    CString idText   = m_listNG.GetItemText(row, 0);
    CString staText  = m_listNG.GetItemText(row, 1);

    int inspectionId = _ttoi(idText);
    if (inspectionId <= 0) {
        TRACE(_T("[PageHome] 더블클릭 — 잘못된 id=%s\n"), (LPCTSTR)idText);
        return;
    }

    // 스테이션 파싱 — "#1" → 1, "#2" → 2. 실패 시 1 로 폴백.
    int stationId = 1;
    if (staText.GetLength() >= 2) {
        stationId = _ttoi(staText.Mid(1));
        if (stationId != 1 && stationId != 2) stationId = 1;
    }

    TRACE(_T("[PageHome] 더블클릭 → 이미지 요청 | id=%d station=%d\n"),
          inspectionId, stationId);

    // 1) 이미지 요청 송신
    if (m_net && m_net->IsConnected()) {
        CString req = CPacketBuilder::BuildInspectImageReq(inspectionId);
        m_net->SendJson(req);
    } else {
        TRACE(_T("[PageHome] 네트워크 미연결 — 이미지 요청 생략\n"));
    }

    // 2) 부모(MainTabDlg) 에게 탭 전환 요청. 응답이 오면 해당 Station 페이지 상단에 표시됨.
    if (m_onRequestShowImage) {
        m_onRequestShowImage(stationId, inspectionId);
    }
}

// ============================================================================
// SetModelInfo (v0.15.0) — 서버 MODEL_LIST_RES(151) 수신 시 모델 정보 갱신
// ============================================================================
// MainTabDlg::OnNetResponse 에서 MODEL_LIST_RES 파싱 후 호출.
// s1Info 예: "PatchCore v1.2.0 | Latency: ~52ms"
// s2Info 예: "YOLO11 v1.0.0 + PatchCore v1.1.0"
void CPageHome::SetModelInfo(const CString& s1Info, const CString& s2Info)
{
    CWnd* w;
    if ((w = GetDlgItem(IDC_STATIC_S1_MODEL_INFO))) w->SetWindowText(s1Info);
    if ((w = GetDlgItem(IDC_STATIC_S2_MODEL_INFO))) w->SetWindowText(s2Info);
}
