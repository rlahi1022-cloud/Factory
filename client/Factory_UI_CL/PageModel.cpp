// ============================================================================
// PageModel.cpp — 모델 관리 페이지 구현부
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
        cb->AddString(_T("Station #1 — PatchCore"));
        cb->AddString(_T("Station #2 — YOLO11"));
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

    CString folderPath = dlg.GetPathName();
    m_files.clear();

    // 선택한 폴더의 이미지 파일 목록 수집
    CFileFind finder;
    BOOL found = finder.FindFile(folderPath + _T("\\*.jpg"));
    if (!found) found = finder.FindFile(folderPath + _T("\\*.png"));

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

    if (m_files.empty()) {
        MessageBox(_T("선택한 폴더에 이미지 파일이 없습니다."),
                   _T("알림"), MB_OK | MB_ICONINFORMATION);
    }

    FillFiles();
}

void CPageModel::OnBtnRetrain()
{
    if (m_files.empty() || m_training) return;

    // 학습 대상 정보 추출
    int station_id = 1;
    CString model_type = _T("PatchCore");
    CComboBox* cb = (CComboBox*)GetDlgItem(IDC_COMBO_TARGET);
    if (cb) {
        int sel = cb->GetCurSel();
        if (sel == 1) { station_id = 2; model_type = _T("YOLO11"); }
    }

    CString product_name;
    CEdit* ed = (CEdit*)GetDlgItem(IDC_EDIT_PRODUCT_NAME);
    if (ed) ed->GetWindowText(product_name);

    // 서버에 RETRAIN_REQ(152) 전송
    if (m_net && m_net->IsConnected()) {
        CString req = CPacketBuilder::BuildRetrainReq(
            station_id, model_type, product_name, (int)m_files.size());
        m_net->SendJson(req);

        // UI 전환 — 서버 응답(153) 대기 상태
        m_training = true;
        m_prog = 0;
        m_progress.SetPos(0);
        m_progress.ShowWindow(SW_SHOW);
        CWnd* w = GetDlgItem(IDC_BTN_RETRAIN);
        if (w) { w->SetWindowText(_T("서버 요청 중...")); w->EnableWindow(FALSE); }
        w = GetDlgItem(IDC_STATIC_TRAIN_STATUS);
        if (w) w->SetWindowText(_T("서버에 재학습 요청 전송 중..."));
    } else {
        MessageBox(_T("서버에 연결되어 있지 않습니다."),
                   _T("오류"), MB_OK | MB_ICONWARNING);
    }
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

    int arrStart = jsonA.Find("\"models\"");
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
        row.type     = CString(CPacketBuilder::ExtractString(obj, "type"));
        row.ver      = CString(CPacketBuilder::ExtractString(obj, "version"));
        row.acc      = CPacketBuilder::ExtractDouble(obj, "accuracy");
        row.deployed = CString(CPacketBuilder::ExtractString(obj, "deployed_at"));

        CStringA activeKey = "\"active\":";
        int aPos = obj.Find(activeKey);
        if (aPos >= 0) {
            CStringA rest = obj.Mid(aPos + activeKey.GetLength());
            rest.TrimLeft();
            row.active = (rest.Left(4) == "true");
        } else {
            row.active = false;
        }

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
    CStringA successStr = CPacketBuilder::ExtractString(jsonA, "success");
    bool success = (successStr == "true");

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
        CStringA msgA = CPacketBuilder::ExtractString(jsonA, "message");
        CString msg(msgA);
        if (msg.IsEmpty()) msg = _T("서버에서 재학습 요청을 거부했습니다.");
        MessageBox(msg, _T("재학습 실패"), MB_OK | MB_ICONWARNING);
    }
}

// ============================================================================
// OnRetrainProgress — 재학습 진행률 푸시 수신 (프로토콜 154)
// ============================================================================
void CPageModel::OnRetrainProgress(int progress)
{
    m_prog = progress;
    m_progress.SetPos(progress);

    CString s;
    s.Format(_T("서버 학습 진행 중: %d%%"), progress);
    CWnd* w = GetDlgItem(IDC_STATIC_TRAIN_STATUS);
    if (w) w->SetWindowText(s);

    if (progress >= 100) {
        m_training = false;
        m_progress.ShowWindow(SW_HIDE);
        w = GetDlgItem(IDC_BTN_RETRAIN);
        if (w) { w->SetWindowText(_T("재학습 실행")); w->EnableWindow(TRUE); }
        w = GetDlgItem(IDC_STATIC_TRAIN_STATUS);
        if (w) w->SetWindowText(_T("✔ 서버 학습 완료!"));
        MessageBox(_T("모델 재학습이 완료되었습니다."), _T("완료"), MB_OK | MB_ICONINFORMATION);
    }
}
