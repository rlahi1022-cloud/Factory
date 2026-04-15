# Factory — CNN 기반 페트병 공정 모니터링 시스템

## 📌 프로젝트 개요

CNN 기반 머신비전 기술을 활용하여
페트병 생산 공정에서 발생하는 불량을 실시간으로 탐지하고,

Edge AI 추론 서버와 이벤트 기반 Main Server를 통해
데이터를 수집·처리·저장·시각화하는 공정 모니터링 시스템이다.

## 👥 프로젝트 정보
- 팀원: 김혜윤, 심동주, 정지훈, 임완, 오인효
- 기간: 2026.04.13 ~ 2026.04.25 (12일)
- 장소: 광주인력개발원

---

## 🧩 시스템 구성

```
Camera → AI Server → Main Server → DB → MFC Client
```

### 구성 요소

* **AiServer (Python)**

  * asyncio.Queue 기반 비동기 파이프라인
  * YOLO11 + PatchCore 기반 추론
  * NG 데이터만 서버 전송 (네트워크 최적화)

* **MainServer (C++)**

  * epoll 기반 비동기 서버
  * EventBus 아키텍처
  * DB 저장 / 통계 / GUI 분기 처리

* **MFC Client**

  * 실시간 검사 결과 모니터링
  * 통계 및 이력 조회 UI

* **Database (MariaDB)**

  * 검사 결과 저장 (inspection_id 기반 추적)

---

## 🔄 전체 데이터 흐름

1. 카메라에서 이미지 캡처
2. AI 서버에서 OK / NG 판정
3. 모든 검사 결과 → `INSPECT_META` 전송
4. NG 발생 시 → `STATION_NG` 전송 (ACK 기반)
5. Main Server에서 DB 저장 및 이벤트 처리
6. MFC 클라이언트에 실시간 결과 반영

---

## ⚙️ 핵심 설계 특징

* EventBus 기반 서버 아키텍처 (확장성)
* asyncio.Queue 기반 비동기 처리 (Backpressure 대응)
* NG 중심 전송 구조 (트래픽 최적화)
* inspection_id 기반 end-to-end 추적
* ACK / 재전송 기반 데이터 신뢰성 확보

---

## 📂 디렉터리 구조

```
Factory/
├── MainServer/
└── AiServer/
```

👉 상세 구조: `Directory_README.md` 참고

---

## 📡 통신 구조

* TCP 기반 통신
* JSON + Binary Image 패킷 구조
* ACK / RETRY / NACK 지원

👉 상세 프로토콜: `Protocol_README.md` 참고

---

## 🗄️ DB

* MariaDB 사용
* 검사 결과 및 통계 데이터 저장

👉 접속 정보: `DB_README.md` 참고

---

## 🚀 실행 방법

### Main Server

cd MainServer
mkdir build && cd build
cmake ..
cmake --build .
./factory_main_server


### AI Server

cd AiServer
python -m Station1.Station1Main
python -m Station2.Station2Main

---

## 📌 현재 상태

* 아키텍처 설계 완료
* 프로토콜 정의 완료
* 서버 뼈대 구현 완료
* DB 환경 구축 완료

---

## 🔜 향후 계획

* Edge → Main Server TCP 연동 테스트
* DB INSERT 및 ACK 검증
* MFC 실시간 연동
* 모델 정확도 개선 및 최적화