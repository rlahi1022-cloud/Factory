#pragma once
#include "pch.h"
#include "InspectionData.h"
#include <atlimage.h>   // CImage — PNG/JPEG/BMP 디코더용 (ATL, MFC 프로젝트에 기본 포함)
#include <vector>
#include <deque>

// 카메라 뷰 (Pylon 이미지 플레이스홀더 + 서버 원시 이미지 렌더링)
class CCameraView : public CStatic {
    DECLARE_DYNAMIC(CCameraView)
public:
    CCameraView();
    void SetInspection(int station, bool isNG, double score, EDefect defect);
    void Tick();  // NG 깜빡임 애니메이션

    // SetImage: 서버에서 원시 이미지 바이트(JPEG/PNG)를 디코드해서 화면에 표시
    // 빈 벡터를 넣으면 이미지 해제 (플레이스홀더 배경으로 복귀)
    void SetImage(const std::vector<BYTE>& bytes);
protected:
    int     m_station;
    bool    m_isNG;
    double  m_score;
    EDefect m_defect;
    bool    m_flash;
    CImage  m_img;          // 디코드된 이미지 (비었으면 IsNull() == true)
    CRITICAL_SECTION m_cs;  // m_img 스레드 보호
    void DrawBg(CDC& dc, CRect& rc);
    void DrawBadge(CDC& dc, CRect& rc);
    afx_msg void OnPaint();
    DECLARE_MESSAGE_MAP()
};

// 히트맵 뷰
class CHeatmapView : public CStatic {
    DECLARE_DYNAMIC(CHeatmapView)
public:
    CHeatmapView();
    void SetActive(bool active);
    // SetImage: 서버 원시 Anomaly Map PNG 바이너리 → 배경에 렌더링
    void SetImage(const std::vector<BYTE>& bytes);
protected:
    bool   m_active;
    CImage m_img;           // 디코드된 히트맵 (비었으면 플레이스홀더 배경)
    CRITICAL_SECTION m_cs;  // m_img 스레드 보호
    afx_msg void OnPaint();
    DECLARE_MESSAGE_MAP()
};

// Pred Mask 뷰 — 원본 이미지 위에 이상 마스크 윤곽선(빨간 원)을 오버레이
class CPredMaskView : public CStatic {
    DECLARE_DYNAMIC(CPredMaskView)
public:
    CPredMaskView();
    // SetMask: 마스크 활성화 여부 및 이상 영역 위치(0.0~1.0 비율) 설정
    // is_active  → true 이면 마스크 원 표시
    // cx1, cy1   → 첫 번째 이상 영역 중심 (비율)
    // cx2, cy2   → 두 번째 이상 영역 중심 (비율, 0이면 미표시)
    void SetMask(bool is_active,
                 double cx1 = 0.55, double cy1 = 0.22,
                 double cx2 = 0.52, double cy2 = 0.52);
    // SetImage: 서버 원시 Pred Mask PNG 바이너리 → 배경에 렌더링
    void SetImage(const std::vector<BYTE>& bytes);
protected:
    bool   m_active;
    double m_cx1, m_cy1;    // 이상 영역 1 중심 (비율)
    double m_cx2, m_cy2;    // 이상 영역 2 중심 (비율)
    CImage m_img;           // 디코드된 마스크 이미지
    CRITICAL_SECTION m_cs;  // m_img 스레드 보호
    void draw_bg(CDC& dc, CRect& rc);
    void draw_label(CDC& dc, CRect& rc);
    afx_msg void OnPaint();
    DECLARE_MESSAGE_MAP()
};

// ── 공통 헬퍼: 바이트 벡터 → CImage 디코드 ──────────────────────────────────
// 바이트를 HGLOBAL에 복사 → IStream 생성 → CImage::Load
// 실패 시 out 이미지는 Destroy()된 상태가 됨.
namespace CameraViewUtil {
    bool LoadImageFromBytes(const std::vector<BYTE>& bytes, CImage& out);
}

// NG 이벤트 이력 리스트 뷰 ───────────────────────────────────────────────────
// v0.14.5: 종합현황(PageHome) 리스트처럼 썸네일 없이 텍스트 컬럼만 표시.
//   컬럼: ID | 스테이션 | 시각 | 결과 | 점수
//   맨 위 고정 헤더 + 아래 행들은 세로 스크롤.
//   실이미지는 상단 3뷰(CCameraView/CHeatmapView/CPredMaskView) 에 맡기고,
//   본 리스트는 메타정보만 표시해 CImage 얕은복사/DC 충돌 문제를 원천 제거.
// 순수 UI — 네트워크 직접 호출 없음. PageStation1/2의 AddNgEntry를 통해 주입.
class CNgHistoryList : public CStatic {
    DECLARE_DYNAMIC(CNgHistoryList)
public:
    CNgHistoryList();

    // v0.14.5: 썸네일 제거 — Entry 는 POD 수준. 복사/이동 안전.
    struct Entry {
        int     id        = 0;
        int     stationId = 0;
        double  score     = 0.0;
        CString time;                // "HH:MM:SS" 또는 "#id"
        // v0.16.0: Station2 YOLO 디텍션 결과 (클래스/신뢰도/판정) — Station1은 비어있음
        CString detClass;            // 첫 번째 NG 디텍션 클래스명 (없으면 빈 문자열)
        double  detConf   = 0.0;     // 신뢰도
        bool    detOk     = true;    // 판정
    };

    // 새 NG 1건을 리스트 맨 위에 추가. 초과분은 꼬리부터 버림.
    // 이미지 bytes 파라미터는 유지하되 내부적으로 사용하지 않음 (텍스트 전용).
    // 호출부(PageStation1/2) API 호환 유지 — 실이미지는 상단 3뷰로만 보낸다.
    void AddEntry(int id, int stationId, double score,
                  const CString& timeLabel,
                  const std::vector<BYTE>& img,
                  const std::vector<BYTE>& heat,
                  const std::vector<BYTE>& mask);

    // v0.16.0: YOLO 디텍션 포함 Entry 직접 주입 오버로드 (Station2 전용)
    void AddEntry(int id, int stationId, double score,
                  const CString& timeLabel,
                  const std::vector<BYTE>& img,
                  const std::vector<BYTE>& heat,
                  const std::vector<BYTE>& mask,
                  const Entry& entryOverride);

    void Clear();
    int  Count() const { return static_cast<int>(m_entries.size()); }

protected:
    std::deque<Entry>  m_entries;   // deque: 앞뒤 삽입/제거 O(1), 이터레이터 안정
    CRITICAL_SECTION   m_cs;        // m_entries 스레드 보호
    int m_maxEntries = 10;
    int m_rowH       = 36;           // v0.16.0b: 행 높이 확대 (28 → 36)
    int m_headerH    = 32;           // v0.16.0b: 헤더 높이 확대 (26 → 32)
    int m_scrollY    = 0;            // 현재 세로 스크롤 오프셋 (px, 헤더 아래 기준)

    void UpdateScrollInfo();
    int  TotalContentHeight() const { return m_rowH * static_cast<int>(m_entries.size()); }
    void DrawHeader(CDC& dc, const CRect& rc);
    void DrawRow(CDC& dc, const Entry& e, const CRect& rowRc);

    virtual void PreSubclassWindow() override;

    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pSB);
    afx_msg BOOL OnMouseWheel(UINT fFlags, short zDelta, CPoint pt);

    DECLARE_MESSAGE_MAP()
};
