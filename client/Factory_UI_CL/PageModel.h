#pragma once
// ============================================================================
// PageModel.h — 모델 관리 페이지
// ============================================================================
#include "pch.h"
#include "Resource.h"
#include "NetworkClient.h"
#include "PacketBuilder.h"
#include <vector>
#include <string>

#define IDT_PAGEMODEL_TRAINING  101

class CPageModel : public CDialogEx {
    DECLARE_DYNAMIC(CPageModel)
public:
    CPageModel(CWnd* p = nullptr);
    enum { IDD = IDD_PAGE_MODEL };

    void OnModelListRes(const std::string& json);
    void OnRetrainRes(const std::string& json);
    // progress: 0~100, station_id/model_type: 진행 중인 학습의 스테이션/모델 타입 식별자
    void OnRetrainProgress(int progress, int station_id = 0, const CString& model_type = _T(""));
    // v0.13.0: 업로드 ACK(159) 수신 — 진행률 표시 업데이트용
    void OnRetrainUploadAck(const std::string& json);

    void SetNetworkClient(CNetworkClient* net) { m_net = net; }
    void RequestModelList();  // 서버에 모델 목록 요청 (MainTabDlg에서 호출)

protected:
    CNetworkClient* m_net = nullptr;

    struct ModelRow {
        int id; int station; CString type; CString ver;
        double acc; CString deployed; bool active;
    };
    struct UpFile { CString name; CString size; };

    CListCtrl     m_listModels;
    CListCtrl     m_listFiles;
    CProgressCtrl m_progress;

    std::vector<ModelRow> m_models;
    std::vector<UpFile>   m_files;
    bool m_training;
    int  m_prog;

    // v0.13.0 업로드 상태
    CString m_folderPath;     // 선택된 폴더 절대 경로 (파일 읽기용)
    CString m_sessionId;      // 현재 업로드 세션 ID
    int     m_uploadedCount;  // 업로드 성공 건수
    int     m_uploadTotal;    // 전체 업로드 대상 건수 (= m_files.size())
    int     m_currentStation;   // 업로드 중인 스테이션
    CString m_currentModelType; // 업로드 중인 모델 타입
    CString m_currentProduct;   // 업로드 중인 제품명

    void FillModels();
    void FillFiles();

    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;

    afx_msg void OnBtnSelectFolder();
    afx_msg void OnBtnRetrain();
    afx_msg void OnBtnClear();
    afx_msg void OnTimer(UINT_PTR id);

    DECLARE_MESSAGE_MAP()
};
