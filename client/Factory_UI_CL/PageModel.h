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
    void OnRetrainProgress(int progress);

    void SetNetworkClient(CNetworkClient* net) { m_net = net; }

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

    void FillModels();
    void FillFiles();
    void RequestModelList();  // 서버에 모델 목록 요청

    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;

    afx_msg void OnBtnSelectFolder();
    afx_msg void OnBtnRetrain();
    afx_msg void OnBtnClear();
    afx_msg void OnTimer(UINT_PTR id);

    DECLARE_MESSAGE_MAP()
};
