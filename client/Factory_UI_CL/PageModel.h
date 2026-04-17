#pragma once
// ============================================================================
// PageModel.h — 모델 관리 페이지
// ============================================================================
#include "pch.h"
#include "Resource.h"
#include <vector>
#include <string>

// 타이머 ID — 클래스 밖에 정의하여 enum 충돌 방지
#define IDT_PAGEMODEL_TRAINING  101

class CPageModel : public CDialogEx {
    DECLARE_DYNAMIC(CPageModel)
public:
    CPageModel(CWnd* p = nullptr);
    enum { IDD = IDD_PAGE_MODEL };

    // 서버 응답 수신 핸들러 (MainTabDlg에서 호출)
    void OnModelListRes(const std::string& json);   // 프로토콜 151
    void OnRetrainRes(const std::string& json);     // 프로토콜 153
    void OnRetrainProgress(int progress);           // 프로토콜 154

protected:
    struct ModelRow {
        int     id;
        int     station;
        CString type;
        CString ver;
        double  acc;
        CString deployed;
        bool    active;
    };
    struct UpFile {
        CString name;
        CString size;
    };

    CListCtrl     m_listModels;
    CListCtrl     m_listFiles;
    CProgressCtrl m_progress;

    std::vector<ModelRow> m_models;
    std::vector<UpFile>   m_files;
    bool m_training;
    int  m_prog;

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
