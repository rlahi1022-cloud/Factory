// ============================================================================
// pch.h — 미리 컴파일된 헤더 (Precompiled Header)
// ============================================================================
// 목적:
//   자주 사용하지만 거의 변경되지 않는 헤더들을 한 번만 컴파일하여
//   빌드 속도를 크게 향상시킵니다.
//   모든 .cpp 파일의 첫 줄에 #include "pch.h"가 있어야 합니다.
//
// 중요: WinSock2.h는 반드시 afxwin.h(→Windows.h) 보다 먼저 include 해야 합니다!
//       그렇지 않으면 WinSock1과 WinSock2 정의가 충돌하여 컴파일 에러가 발생합니다.
// ============================================================================

#pragma once

#ifndef VC_EXTRA_LEAN
#define VC_EXTRA_LEAN
#endif

#include "targetver.h"

// ── WinSock2 (TCP 네트워크 통신) ──────────────────────────────────────────
// ※ 반드시 <afxwin.h> 보다 먼저 포함해야 합니다! (WinSock 버전 충돌 방지)
// WinSock2.h: Windows 소켓 프로그래밍 API (connect, send, recv 등)
// WS2tcpip.h: inet_pton 등 최신 IP 주소 변환 함수
// Ws2_32.lib: WinSock2 라이브러리 링크 지시
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS
#define _AFX_ALL_WARNINGS

// ── MFC 핵심 헤더 ─────────────────────────────────────────────────────────
#include <afxwin.h>         // MFC 핵심: CWnd, CString, CDC 등
#include <afxext.h>         // MFC 확장: CToolBar, CStatusBar 등
#include <afxdialogex.h>    // CDialogEx: 향상된 다이얼로그 기본 클래스
#include <afxcmn.h>         // 공용 컨트롤: CListCtrl, CTabCtrl, CProgressCtrl 등
#include <afxdlgs.h>        // 공용 다이얼로그: CFileDialog 등

// ── C++ 표준 라이브러리 ───────────────────────────────────────────────────
#include <vector>           // 가변 크기 배열 (std::vector)
#include <memory>           // 스마트 포인터 (std::unique_ptr, std::shared_ptr)
#include <string>           // C++ 문자열 (std::string)
#include <map>              // 정렬된 키-값 맵 (std::map)
#include <algorithm>        // 정렬, 검색 등 알고리즘 (std::sort, std::find 등)
#include <cstdlib>          // C 표준 라이브러리 (rand, srand, atoi 등)
#include <ctime>            // 시간 관련 (time, localtime 등)
#include <cmath>            // 수학 함수 (sin, cos, sqrt 등)
