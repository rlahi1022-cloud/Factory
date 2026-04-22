// ============================================================================
// PageModel.cpp — 모델 관리 페이지 (조회 + 재학습 요청)
// ============================================================================
// 책임:
//   - 배포된 AI 모델 목록 조회 (MODEL_LIST_REQ 150)
//   - 학습용 이미지 폴더 선택 + 파일 수 표시
//   - 재학습 요청 송신 (RETRAIN_REQ 152) + 진행률 실시간 표시
//
// v0.11.0 변경점:
//   - 콤보박스에 "Station #2 — PatchCore" 항목 추가 (YOLO 와 독립 재학습)
//   - OnRetrainProgress 시그니처에 (station_id, model_type) 추가 —
//     진행률 라벨에 "Station 2 · PatchCore 80%" 형태로 명시
//   - 학습 완료 시 자동으로 RequestModelList() 호출 → 신규 모델이 목록에 즉시 반영
//
// 프로토콜:
//   150 MODEL_LIST_REQ       → 151 MODEL_LIST_RES          (DB 조회)
//   152 RETRAIN_REQ          → 153 RETRAIN_RES             (수락/거부)
//                            → 154 RETRAIN_PROGRESS_PUSH   (진행률/완료/실패)
// ============================================================================
#include "pch.h"
#include "PageModel.h"
#include "PacketBuilder.h"   // ExtractInt/String/Double — cpp에서만 include

IMPLEMENT_DYNAMIC(CPageModel, CDialogEx)
BEGIN_MESSAGE_MAP(CPageModel, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_SELECT_FOLDER, OnBtnSelectFolder)
    ON_BN_CLICKED(IDC_BTN_RETRAIN,       OnBtnRetrain)
    ON_BN_CLICKED(IDC_BTN_CLEAR_FILES,   OnBtnClear)
    ON_WM_TIMER()
END_MESSAGE_MAP()

CPageModel::CPageModel(CWnd* p)
    : CDialogEx(IDD_PAGE_MODEL, p)
    , m_training(false)
    , m_prog(0)
    , m_uploadedCount(0)
    , m_uploadTotal(0)
    , m_currentStation(1)
{}

void CPageModel::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LIST_MODELS,      m_listModels);
    DDX_Control(pDX, IDC_LIST_UPLOADED,    m_listFiles);
    DDX_Control(pDX, IDC_PROGRESS_TRAINING,m_progress);
}

BOOL CPageModel::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    m_listModels.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    m_listModels.InsertColumn(0, _T("ID"),       LVCFMT_LEFT, 30);
    m_listModels.InsertColumn(1, _T("스테이션"), LVCFMT_LEFT, 55);
    m_listModels.InsertColumn(2, _T("모델"),     LVCFMT_LEFT, 70);
    m_listModels.InsertColumn(3, _T("버전"),     LVCFMT_LEFT, 60);
    m_listModels.InsertColumn(4, _T("정확도"),   LVCFMT_LEFT, 55);
    m_listModels.InsertColumn(5, _T("배포일시"), LVCFMT_LEFT, 130);
    m_listModels.InsertColumn(6, _T("활성"),     LVCFMT_LEFT, 50);

    m_listFiles.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    m_listFiles.InsertColumn(0, _T("파일명"), LVCFMT_LEFT, 180);
    m_listFiles.InsertColumn(1, _T("크기"),   LVCFMT_LEFT, 70);

    CComboBox* cb = (CComboBox*)GetDlgItem(IDC_COMBO_TARGET);
    if (cb) {
        // 인덱스는 OnBtnRetrain() 분기와 동일한 순서를 유지해야 함
        cb->AddString(_T("Station #1 — PatchCore"));   // 0
        cb->AddString(_T("Station #2 — YOLO11"));      // 1
        cb->AddString(_T("Station #2 — PatchCore"));   // 2 (라벨 표면 품질)
        cb->SetCurSel(0);
    }

    CEdit* ed = (CEdit*)GetDlgItem(IDC_EDIT_PRODUCT_NAME);
    if (ed) ed->SetWindowText(_T("samdasoo_500ml"));

    m_progress.SetRange(0, 100);
    m_progress.SetPos(0);
    m_progress.ShowWindow(SW_HIDE);

    FillModels();  // 초기에는 비어있음 — 서버 연결 후 RequestModelList()로 채움

    auto set = [&](int id, LPCTSTR v) {
        CWnd* w = GetDlgItem(id);
        if (w) w->SetWindowText(v);
    };
    set(IDC_STATIC_TRAIN_SERVER,    _T("Ubuntu 24.04 + CUDA 12.x"));
    set(IDC_STATIC_TRAIN_GPU,       _T("NVIDIA RTX 계열"));
    set(IDC_STATIC_TRAIN_FRAMEWORK, _T("PyTorch + Anomalib / Ultralytics"));

    return TRUE;
}

void CPageModel::FillModels()
{
    m_listModels.DeleteAllItems();
    for (int i = 0; i < (int)m_models.size(); ++i) {
        auto& m = m_models[i];
        CString s;
        s.Format(_T("%d"), m.id);
        m_listModels.InsertItem(i, s);
        s.Format(_T("#%d"), m.station);
        m_listModels.SetItemText(i, 1, s);
        m_listModels.SetItemText(i, 2, m.type);
        m_listModels.SetItemText(i, 3, m.ver);
        s.Format(_T("%.1f%%"), m.acc);
        m_listModels.SetItemText(i, 4, s);
        m_listModels.SetItemText(i, 5, m.deployed);
        m_listModels.SetItemText(i, 6, m.active ? _T("● 활성") : _T("○ 비활성"));
    }
}

void CPageModel::FillFiles()
{
    m_listFiles.DeleteAllItems();
    for (int i = 0; i < (int)m_files.size(); ++i) {
        m_listFiles.InsertItem(i, m_files[i].name);
        m_listFiles.SetItemText(i, 1, m_files[i].size);
    }
    CWnd* w = GetDlgItem(IDC_BTN_RETRAIN);
    if (w) w->EnableWindow(!m_training && !m_files.empty());
}

void CPageModel::OnBtnSelectFolder()
{
    // 실제 폴더 선택 다이얼로그
    CFolderPickerDialog dlg(nullptr, OFN_FILEMUSTEXIST, this);
    if (dlg.DoModal() != IDOK) return;

    // v0.13.0: 폴더 경로도 저장 — 재학습 실행 시 파일 바이너리를 읽기 위해 필요.
    m_folderPath = dlg.GetPathName();
    m_files.clear();

    // 선택한 폴더의 이미지 파일 목록 수집 (.jpg + .png 모두 수집)
    auto collect = [&](LPCTSTR pattern) {
        CFileFind finder;
        BOOL found = finder.FindFile(m_folderPath + pattern);
        while (found) {
            found = finder.FindNextFile();
            if (finder.IsDots() || finder.IsDirectory()) continue;

            UpFile f;
            f.name = finder.GetFileName();
            ULONGLONG size = finder.GetLength();
            if (size >= 1024 * 1024)
                f.size.Format(_T("%.1f MB"), size / (1024.0 * 1024.0));
            else
                f.size.Format(_T("%llu KB"), size / 1024);
            m_files.push_back(f);
        }
        finder.Close();
    };
    collect(_T("\\*.jpg"));
    collect(_T("\\*.png"));

    if (m_files.empty()) {
        MessageBox(_T("선택한 폴더에 이미지 파일이 없습니다."),
                   _T("알림"), MB_OK | MB_ICONINFORMATION);
    }

    FillFiles();
}

void CPageModel::OnBtnRetrain()
{
    if (m_files.empty() || m_training) return;
    if (m_folderPath.IsEmpty()) {
        MessageBox(_T("먼저 학습 이미지 폴더를 선택하세요."),
                   _T("알림"), MB_OK | MB_ICONINFORMATION);
        return;
    }

    // ── 학습 대상 결정 (콤보 인덱스 순서 = OnInitDialog 의 AddString) ──
    //   0: Station #1 — PatchCore
    //   1: Station #2 — YOLO11
    //   2: Station #2 — PatchCore
    int station_id = 1;
    CString model_type = _T("PatchCore");
    CComboBox* cb = (CComboBox*)GetDlgItem(IDC_COMBO_TARGET);
    if (cb) {
        int sel = cb->GetCurSel();
        if (sel == 1)      { station_id = 2; model_type = _T("YOLO11");    }
        else if (sel == 2) { station_id = 2; model_type = _T("PatchCore"); }
    }

    CString product_name;
    CEdit* ed = (CEdit*)GetDlgItem(IDC_EDIT_PRODUCT_NAME);
    if (ed) ed->GetWindowText(product_name);

    if (!m_net || !m_net->IsConnected()) {
        MessageBox(_T("서버에 연결되어 있지 않습니다."),
                   _T("오류"), MB_OK | MB_ICONWARNING);
        return;
    }

    // ── v0.13.0: 파일 업로드 → RETRAIN_REQ 순서로 진행 ──
    m_sessionId       = CPacketBuilder::GenerateSessionId();
    m_uploadedCount   = 0;
    m_uploadTotal     = (int)m_files.size();
    m_currentStation  = station_id;
    m_currentModelType= model_type;
    m_currentProduct  = product_name;

    m_training = true;
    m_prog = 0;
    m_progress.SetRange(0, 100);
    m_progress.SetPos(0);
    m_progress.ShowWindow(SW_SHOW);

    CWnd* w = GetDlgItem(IDC_BTN_RETRAIN);
    if (w) { w->SetWindowText(_T("업로드 중...")); w->EnableWindow(FALSE); }
    w = GetDlgItem(IDC_STATIC_TRAIN_STATUS);
    if (w) {
        CString msg;
        msg.Format(_T("학습 이미지 업로드 중... (0/%d) session=%s"),
                   m_uploadTotal, (LPCTSTR)m_sessionId);
        w->SetWindowText(msg);
    }

    // 파일 순차 업로드 — 각 파일을 읽어 RETRAIN_UPLOAD(158) 로 전송.
    // 서버 ACK(159) 는 OnRetrainUploadAck() 로 수신되어 진행률 업데이트.
    // 전체 전송 완료 체크는 ACK 누적 카운트 기반이지만, 여기서는 한 번에 순차 송신
    // 후 UI 가 ACK 를 받으며 진행률만 갱신하는 구조 (클라-서버 QoS 는 TCP 보장).
    for (int i = 0; i < m_uploadTotal; ++i) {
        const CString& filename = m_files[i].name;
        CString fullPath = m_folderPath + _T("\\") + filename;

        // 파일 바이너리 읽기
        CFile file;
        CFileException ex;
        if (!file.Open(fullPath, CFile::modeRead | CFile::shareDenyNone, &ex)) {
            CString e; e.Format(_T("파일 열기 실패: %s"), (LPCTSTR)filename);
            MessageBox(e, _T("업로드 오류"), MB_OK | MB_ICONERROR);
            continue;
        }
        ULONGLONG sz = file.GetLength();
        if (sz == 0 || sz > 50ULL * 1024 * 1024) {
            file.Close();
            continue;   // 빈 파일/50MB 초과는 skip
        }
        std::vector<char> bytes((size_t)sz);
        file.Read(bytes.data(), (UINT)sz);
        file.Close();

        // 프레임 조립 + 송신
        std::vector<char> frame = CPacketBuilder::BuildRetrainUploadFrame(
            m_sessionId, station_id, model_type, filename, i, m_uploadTotal, bytes);
        if (!m_net->Send(frame)) {
            MessageBox(_T("업로드 중 네트워크 오류 — 다시 시도해주세요."),
                       _T("오류"), MB_OK | MB_ICONERROR);
            m_training = false;
            CWnd* btn = GetDlgItem(IDC_BTN_RETRAIN);
            if (btn) { btn->SetWindowText(_T("재학습 실행")); btn->EnableWindow(TRUE); }
            return;
        }
    }
    // 송신은 끝났고, ACK 159 를 OnRetrainUploadAck 에서 수신하여 전부 받으면
    // RETRAIN_REQ(152) 를 최종 송신한다 (그 로직은 OnRetrainUploadAck 내부).
}

void CPageModel::RequestModelList()
{
    if (m_net && m_net->IsConnected()) {
        CString req = CPacketBuilder::BuildModelListReq();
        m_net->SendJson(req);
    }
}

void CPageModel::OnBtnClear()
{
    m_files.clear();
    FillFiles();
}

void CPageModel::OnTimer(UINT_PTR id)
{
    if (id == IDT_PAGEMODEL_TRAINING) {
        m_prog += 5;
        m_progress.SetPos(m_prog);
        CString s;
        s.Format(_T("Epoch %d/20 | 이미지 %d장"), m_prog / 5, (int)m_files.size());
        CWnd* w = GetDlgItem(IDC_STATIC_TRAIN_STATUS);
        if (w) w->SetWindowText(s);
        if (m_prog >= 100) {
            KillTimer(IDT_PAGEMODEL_TRAINING);
            m_training = false;
            m_progress.ShowWindow(SW_HIDE);
            w = GetDlgItem(IDC_BTN_RETRAIN);
            if (w) { w->SetWindowText(_T("재학습 실행")); w->EnableWindow(TRUE); }
            w = GetDlgItem(IDC_STATIC_TRAIN_STATUS);
            if (w) w->SetWindowText(_T("✔ 학습 완료!"));
            MessageBox(_T("모델 재학습이 완료되었습니다."), _T("완료"), MB_OK | MB_ICONINFORMATION);
        }
    }
    CDialogEx::OnTimer(id);
}

// ============================================================================
// OnModelListRes — 모델 목록 응답 수신 (프로토콜 151)
// ============================================================================
void CPageModel::OnModelListRes(const std::string& json)
{
    CStringA jsonA(json.c_str());

    // 서버 실제 응답: {"protocol_no":151,"count":N,"items":[{...},...]}
    int arrStart = jsonA.Find("\"items\"");
    if (arrStart < 0) return;
    int arrS = jsonA.Find('[', arrStart);
    int arrE = jsonA.Find(']', arrS);
    if (arrS < 0 || arrE < 0) return;

    CStringA arr = jsonA.Mid(arrS + 1, arrE - arrS - 1);
    m_models.clear();

    int pos = 0;
    while (pos < arr.GetLength()) {
        int os = arr.Find('{', pos);
        int oe = arr.Find('}', os);
        if (os < 0 || oe < 0) break;

        CStringA obj = arr.Mid(os, oe - os + 1);
        ModelRow row;
        row.id       = CPacketBuilder::ExtractInt(obj, "id");
        row.station  = CPacketBuilder::ExtractInt(obj, "station_id");
        row.type     = CPacketBuilder::ExtractStringW(obj, "model_type");   // 서버 필드명 일치
        row.ver      = CPacketBuilder::ExtractStringW(obj, "version");
        row.acc      = CPacketBuilder::ExtractDouble(obj, "accuracy");
        row.deployed = CPacketBuilder::ExtractStringW(obj, "deployed_at");
        row.active   = (CPacketBuilder::ExtractInt(obj, "is_active") != 0);  // 서버는 0/1 정수

        m_models.push_back(row);
        pos = oe + 1;
    }

    FillModels();
    TRACE(_T("[PageModel] 모델 목록 수신: %d개\n"), (int)m_models.size());
}

// ============================================================================
// OnRetrainRes — 재학습 시작 응답 수신 (프로토콜 153)
// ============================================================================
void CPageModel::OnRetrainRes(const std::string& json)
{
    CStringA jsonA(json.c_str());
    bool success = CPacketBuilder::ExtractBool(jsonA, "success");

    if (success) {
        KillTimer(IDT_PAGEMODEL_TRAINING);
        m_training = true;
        m_prog = 0;
        m_progress.SetPos(0);
        m_progress.ShowWindow(SW_SHOW);

        CWnd* w = GetDlgItem(IDC_BTN_RETRAIN);
        if (w) { w->SetWindowText(_T("서버 학습 중...")); w->EnableWindow(FALSE); }
        w = GetDlgItem(IDC_STATIC_TRAIN_STATUS);
        if (w) w->SetWindowText(_T("서버에서 재학습 시작됨 — 진행률 수신 대기 중..."));
        TRACE(_T("[PageModel] 서버 재학습 시작 확인\n"));
    } else {
        m_training = false;
        m_progress.ShowWindow(SW_HIDE);
        CWnd* w = GetDlgItem(IDC_BTN_RETRAIN);
        if (w) { w->SetWindowText(_T("재학습 실행")); w->EnableWindow(TRUE); }
        CString msg = CPacketBuilder::ExtractStringW(jsonA, "message");  // UTF-8 → Unicode
        if (msg.IsEmpty()) msg = _T("서버에서 재학습 요청을 거부했습니다.");
        MessageBox(msg, _T("재학습 실패"), MB_OK | MB_ICONWARNING);
    }
}

// ============================================================================
// OnRetrainUploadAck — 업로드 ACK 수신 (v0.13.0, 프로토콜 159)
// ============================================================================
// 각 파일 업로드 결과를 받아 진행률을 갱신하고, 전체 파일이 끝나면 자동으로
// RETRAIN_REQ(152) 를 송신하여 학습을 트리거.
void CPageModel::OnRetrainUploadAck(const std::string& json)
{
    CStringA jsonA(json.c_str());
    bool success  = CPacketBuilder::ExtractBool(jsonA, "success");
    CString msg   = CPacketBuilder::ExtractStringW(jsonA, "message");
    CString sess  = CPacketBuilder::ExtractStringW(jsonA, "session_id");

    // 세션 불일치 ACK 는 무시 (다른 요청에 대한 응답)
    if (sess != m_sessionId) return;

    if (!success) {
        // 한 파일이라도 실패하면 경고만 남기고 계속 진행 (운영자 판단)
        TRACE(_T("[PageModel] 업로드 실패: %s\n"), (LPCTSTR)msg);
    }
    m_uploadedCount++;

    // UI 진행률: 업로드 구간은 0~50% 로 표시 (이후 학습 50~100%)
    int pct = m_uploadTotal > 0
              ? (m_uploadedCount * 50 / m_uploadTotal)
              : 0;
    m_progress.SetPos(pct);
    CWnd* w = GetDlgItem(IDC_STATIC_TRAIN_STATUS);
    if (w) {
        CString label;
        label.Format(_T("이미지 업로드 중... (%d/%d) session=%s"),
                     m_uploadedCount, m_uploadTotal, (LPCTSTR)m_sessionId);
        w->SetWindowText(label);
    }

    // 모든 파일 업로드 완료 → RETRAIN_REQ(152) 송신 (session_id 포함)
    if (m_uploadedCount >= m_uploadTotal) {
        if (m_net && m_net->IsConnected()) {
            CString req = CPacketBuilder::BuildRetrainReq(
                m_currentStation, m_currentModelType, m_currentProduct,
                m_uploadTotal, m_sessionId);
            m_net->SendJson(req);
            TRACE(_T("[PageModel] 업로드 완료 → RETRAIN_REQ 송신 session=%s\n"),
                  (LPCTSTR)m_sessionId);

            w = GetDlgItem(IDC_STATIC_TRAIN_STATUS);
            if (w) w->SetWindowText(_T("업로드 완료 — 서버 학습 시작 대기 중..."));
        }
    }
}

// ============================================================================
// OnRetrainProgress — 재학습 진행률 푸시 수신 (프로토콜 154)
// station_id / model_type 도 함께 표시하여 Station2 이중모델(YOLO/PatchCore)
// 중 어느 쪽이 학습 중인지 UI에서 명확히 보이도록 한다.
// ============================================================================
void CPageModel::OnRetrainProgress(int progress, int station_id, const CString& model_type)
{
    m_prog = progress;
    m_progress.SetPos(progress);

    CString s;
    if (station_id > 0 && !model_type.IsEmpty()) {
        s.Format(_T("서버 학습 진행 중 [Station %d · %s]: %d%%"),
                 station_id, (LPCTSTR)model_type, progress);
    } else {
        s.Format(_T("서버 학습 진행 중: %d%%"), progress);
    }
    CWnd* w = GetDlgItem(IDC_STATIC_TRAIN_STATUS);
    if (w) w->SetWindowText(s);

    if (progress >= 100) {
        m_training = false;
        m_progress.ShowWindow(SW_HIDE);
        w = GetDlgItem(IDC_BTN_RETRAIN);
        if (w) { w->SetWindowText(_T("재학습 실행")); w->EnableWindow(TRUE); }
        w = GetDlgItem(IDC_STATIC_TRAIN_STATUS);
        if (w) {
            CString done;
            if (station_id > 0 && !model_type.IsEmpty()) {
                done.Format(_T("✔ 서버 학습 완료 [Station %d · %s]"),
                            station_id, (LPCTSTR)model_type);
            } else {
                done = _T("✔ 서버 학습 완료!");
            }
            w->SetWindowText(done);
        }
        // 완료 시 모델 목록 갱신을 서버에 요청 — 방금 배포된 새 모델이 리스트에 나타남
        RequestModelList();
        MessageBox(_T("모델 재학습이 완료되었습니다."), _T("완료"), MB_OK | MB_ICONINFORMATION);
    }
}
