#include "pch.h"
#include "PageModel.h"

IMPLEMENT_DYNAMIC(CPageModel, CDialogEx)
BEGIN_MESSAGE_MAP(CPageModel, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_SELECT_FOLDER, OnBtnSelectFolder)
    ON_BN_CLICKED(IDC_BTN_RETRAIN,       OnBtnRetrain)
    ON_BN_CLICKED(IDC_BTN_CLEAR_FILES,   OnBtnClear)
    ON_WM_TIMER()
END_MESSAGE_MAP()

CPageModel::CPageModel(CWnd* p):CDialogEx(IDD_PAGE_MODEL,p),m_training(false),m_prog(0){}

void CPageModel::DoDataExchange(CDataExchange* pDX){
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX,IDC_LIST_MODELS,   m_listModels);
    DDX_Control(pDX,IDC_LIST_UPLOADED, m_listFiles);
    DDX_Control(pDX,IDC_PROGRESS_TRAINING,m_progress);
}

BOOL CPageModel::OnInitDialog(){
    CDialogEx::OnInitDialog();
    m_listModels.SetExtendedStyle(LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
    m_listModels.InsertColumn(0,_T("ID"),      LVCFMT_LEFT,30);
    m_listModels.InsertColumn(1,_T("스테이션"),LVCFMT_LEFT,55);
    m_listModels.InsertColumn(2,_T("모델"),    LVCFMT_LEFT,70);
    m_listModels.InsertColumn(3,_T("버전"),    LVCFMT_LEFT,60);
    m_listModels.InsertColumn(4,_T("정확도"),  LVCFMT_LEFT,55);
    m_listModels.InsertColumn(5,_T("배포일시"),LVCFMT_LEFT,130);
    m_listModels.InsertColumn(6,_T("활성"),    LVCFMT_LEFT,50);
    m_listFiles.SetExtendedStyle(LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
    m_listFiles.InsertColumn(0,_T("파일명"),LVCFMT_LEFT,180);
    m_listFiles.InsertColumn(1,_T("크기"),  LVCFMT_LEFT,70);
    CComboBox* cb=(CComboBox*)GetDlgItem(IDC_COMBO_TARGET);
    if(cb){cb->AddString(_T("Station #1 — PatchCore"));cb->AddString(_T("Station #2 — YOLO11"));cb->SetCurSel(0);}
    CEdit* ed=(CEdit*)GetDlgItem(IDC_EDIT_PRODUCT_NAME);
    if(ed) ed->SetWindowText(_T("samdasoo_500ml"));
    m_progress.SetRange(0,100);m_progress.SetPos(0);m_progress.ShowWindow(SW_HIDE);
    m_models={{1,1,_T("PatchCore"),_T("v1.2.0"),97.3,_T("2026-04-20 09:00"),true},
              {2,1,_T("PatchCore"),_T("v1.1.0"),95.1,_T("2026-04-18 14:30"),false},
              {3,2,_T("YOLO11"),  _T("v1.0.0"),94.8,_T("2026-04-20 09:00"),true},
              {4,2,_T("PatchCore"),_T("v1.1.0"),96.5,_T("2026-04-20 09:00"),true}};
    FillModels();

    // ── 학습 PC 정보 표시 (목업 대비 추가) ──
    // 목업의 "학습 PC 정보" GroupBox에 해당하는 정적 정보를 채웁니다.
    auto set=[&](int id, LPCTSTR v){ CWnd* w=GetDlgItem(id); if(w) w->SetWindowText(v); };
    set(IDC_STATIC_TRAIN_SERVER,    _T("Ubuntu 24.04 + CUDA 12.x"));
    set(IDC_STATIC_TRAIN_GPU,       _T("NVIDIA RTX 계열"));
    set(IDC_STATIC_TRAIN_FRAMEWORK, _T("PyTorch + Anomalib / Ultralytics"));

    return TRUE;
}

void CPageModel::FillModels(){
    m_listModels.DeleteAllItems();
    for(int i=0;i<(int)m_models.size();++i){
        auto& m=m_models[i]; CString s;
        s.Format(_T("%d"),m.id);m_listModels.InsertItem(i,s);
        s.Format(_T("#%d"),m.station);m_listModels.SetItemText(i,1,s);
        m_listModels.SetItemText(i,2,m.type);m_listModels.SetItemText(i,3,m.ver);
        s.Format(_T("%.1f%%"),m.acc);m_listModels.SetItemText(i,4,s);
        m_listModels.SetItemText(i,5,m.deployed);
        m_listModels.SetItemText(i,6,m.active?_T("● 활성"):_T("○ 비활성"));
    }
}
void CPageModel::FillFiles(){
    m_listFiles.DeleteAllItems();
    for(int i=0;i<(int)m_files.size();++i){
        m_listFiles.InsertItem(i,m_files[i].name);
        m_listFiles.SetItemText(i,1,m_files[i].size);}
    CWnd* w=GetDlgItem(IDC_BTN_RETRAIN);
    if(w) w->EnableWindow(!m_training&&!m_files.empty());
}
void CPageModel::OnBtnSelectFolder(){
    m_files={{_T("normal_001.jpg"),_T("245 KB")},{_T("normal_002.jpg"),_T("238 KB")},
             {_T("normal_003.jpg"),_T("251 KB")},{_T("normal_004.jpg"),_T("240 KB")},
             {_T("normal_005.jpg"),_T("247 KB")}};
    FillFiles();
}
void CPageModel::OnBtnRetrain(){
    if(m_files.empty()||m_training)return;
    m_training=true;m_prog=0;m_progress.SetPos(0);m_progress.ShowWindow(SW_SHOW);
    CWnd* w=GetDlgItem(IDC_BTN_RETRAIN);if(w){w->SetWindowText(_T("학습 중..."));w->EnableWindow(FALSE);}
    SetTimer(IDT_TRAINING,400,nullptr);
}
void CPageModel::OnBtnClear(){m_files.clear();FillFiles();}
void CPageModel::OnTimer(UINT_PTR id){
    if(id==IDT_TRAINING){
        m_prog+=5;m_progress.SetPos(m_prog);
        CString s;s.Format(_T("Epoch %d/20 | 이미지 %d장"),m_prog/5,(int)m_files.size());
        CWnd* w=GetDlgItem(IDC_STATIC_TRAIN_STATUS);if(w)w->SetWindowText(s);
        if(m_prog>=100){
            KillTimer(IDT_TRAINING);m_training=false;m_progress.ShowWindow(SW_HIDE);
            w=GetDlgItem(IDC_BTN_RETRAIN);if(w){w->SetWindowText(_T("재학습 실행"));w->EnableWindow(TRUE);}
            w=GetDlgItem(IDC_STATIC_TRAIN_STATUS);if(w)w->SetWindowText(_T("✔ 학습 완료!"));
            MessageBox(_T("모델 재학습이 완료되었습니다."),_T("완료"),MB_OK|MB_ICONINFORMATION);
        }
    }
    CDialogEx::OnTimer(id);
}
