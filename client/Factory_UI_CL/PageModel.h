#pragma once
#include "pch.h"
#include "Resource.h"
#include <vector>

class CPageModel : public CDialogEx {
    DECLARE_DYNAMIC(CPageModel)
public:
    CPageModel(CWnd* p=nullptr);
    enum { IDD = IDD_PAGE_MODEL };
    void OnModelListRes(const std::string& json);
    void OnRetrainRes(const std::string& json);
    void OnRetrainProgress(int progress);
protected:
    struct ModelRow{int id,station;CString type,ver;double acc;CString deployed;bool active;};
    struct UpFile{CString name,size;};
    CListCtrl m_listModels, m_listFiles;
    CProgressCtrl m_progress;
    std::vector<ModelRow> m_models;
    std::vector<UpFile> m_files;
    bool m_training; int m_prog;
    void FillModels(); void FillFiles();
    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;
    afx_msg void OnBtnSelectFolder();
    afx_msg void OnBtnRetrain();
    afx_msg void OnBtnClear();
    afx_msg void OnTimer(UINT_PTR id);
    DECLARE_MESSAGE_MAP()
};
