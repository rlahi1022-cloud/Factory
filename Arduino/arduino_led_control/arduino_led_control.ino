/*
 * arduino_led_control.ino — Factory QC 신호등 (v3 — 이상/정상 단순 모드)
 * =======================================================================
 *
 * 목적:
 *   AI 추론서버(Station1/Station2)가 시리얼(USB)로 보내는 검사 결과 명령을 받아
 *   WS2812B 네오픽셀 LED 바(8개)를 색상으로 제어한다.
 *
 * 단순 동작 규칙 (v3):
 *   - 기본(대기) 상태       → 초록색 LED 상시 점등 (정상)
 *   - 이상(NG) 명령 수신 시 → 빨간색 LED 로 전환 (경고)
 *   - 이상 3초 유지 후 자동으로 초록색으로 복귀
 *
 * 지원 명령 (v0.14.x Python 프로토콜 — SerialCtrl.py):
 *   "REJECT:<defect>\n"        Station1 입고검사 NG   → 빨간색 3초
 *                              예: "REJECT:anomaly\n"
 *   "ALERT:<d1,d2,..>\n"       Station2 조립검사 NG  → 빨간색 3초
 *                              예: "ALERT:cap_missing\n"
 *   "OK\n"                     명시적 정상 통지        → 즉시 초록색 복귀
 *
 * 하위호환 (구 프로토콜 — led_test.py 기타):
 *   '1'  → 초록색 (즉시 복귀)
 *   '0'  → 빨간색 (3초 후 복귀)
 *
 * 하드웨어 연결 (WS2812B LED 바 ↔ Arduino):
 *   빨강 선 (VCC)  → 5V
 *   검정 선 (GND)  → GND
 *   흰색 선 (DIN)  → 디지털 6번 핀
 *
 * 필요 라이브러리:
 *   Adafruit NeoPixel — 아두이노 IDE 라이브러리 관리자에서 설치
 *
 * 시리얼 통신:
 *   9600 baud, 8N1 (Python SerialCtrl 기본값과 동일)
 *
 * 향후 확장 (TODO):
 *   - Station1 서보모터 리젝트 (NG 제품 컨베이어 밀어내기)
 *   - 부저(passive buzzer) — NG 시 경고음
 *   - Station2 LCD (I2C 16x2) — 결함 유형 표시
 */

#include <Adafruit_NeoPixel.h>

// ============================================================================
// 설정 상수
// ============================================================================

const int  LED_PIN       = 6;        // WS2812B 데이터 선 핀
const int  LED_COUNT     = 8;        // LED 개수
const int  BRIGHTNESS    = 50;       // 밝기 (0~255) — 실공장에선 더 높게

// NG 판정 시 빨간색 LED 유지 시간 (이 시간 후 자동으로 초록색 복귀)
const unsigned long NG_DURATION = 3000;  // 3초

// 수신 버퍼 크기 — "ALERT:cap_missing,label_tilt,fill_low\n" 정도 여유 있게
const int  RX_BUF_SIZE   = 128;

// ============================================================================
// 색상 팔레트 (RGB)
// ============================================================================

struct RGB { uint8_t r, g, b; };
const RGB COLOR_OK = {  0, 255,   0};   // 초록 — 정상 (기본 상태)
const RGB COLOR_NG = {255,   0,   0};   // 빨강 — 이상 (NG)

// ============================================================================
// 상태 변수
// ============================================================================

// 현재 상태 — true 면 "이상(NG) 표시 중", false 면 "정상(초록)"
bool ng_active = false;

// NG 상태로 전환된 시각 (millis 기준). NG_DURATION 지나면 자동 초록 복귀.
unsigned long ng_start_time = 0;

// 시리얼 수신 버퍼 — '\n' 까지 누적해 한 줄 단위로 처리
char rx_buf[RX_BUF_SIZE];
int  rx_len = 0;

// NeoPixel 스트립
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);


// ============================================================================
// setup — 초기화
// ============================================================================
void setup() {
  Serial.begin(9600);

  strip.begin();
  strip.setBrightness(BRIGHTNESS);

  // 부팅 즉시 초록색(정상) 점등 — 대기 상태
  setAllLeds(COLOR_OK);

  Serial.println("Arduino Ready (Factory QC v3 — always green, red on NG)");
}


// ============================================================================
// setAllLeds — 8개 LED 전부 동일 색상으로 점등
// ============================================================================
void setAllLeds(const RGB& c) {
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, strip.Color(c.r, c.g, c.b));
  }
  strip.show();
}


// ============================================================================
// enterNgState — NG 상태로 전환 (빨간색 + 타이머 시작)
//   이미 NG 상태여도 재호출하면 타이머를 리셋해서 새로 3초를 카운트.
// ============================================================================
void enterNgState() {
  setAllLeds(COLOR_NG);
  ng_active     = true;
  ng_start_time = millis();
}


// ============================================================================
// returnToOkState — 정상 상태로 즉시 복귀 (초록색)
// ============================================================================
void returnToOkState() {
  setAllLeds(COLOR_OK);
  ng_active = false;
}


// ============================================================================
// handleCommand — 누적된 한 줄(rx_buf) 을 파싱해서 LED 동작
// ============================================================================
void handleCommand(const char* cmd, int len) {
  // 빈 줄 / 제어문자 단독 → 무시 (초록 유지)
  if (len <= 0) return;

  // ── REJECT:<defect> ───────────────────────────────────────────
  // Station1 입고검사 NG — 빨간 LED 3초
  if (len >= 6 && strncmp(cmd, "REJECT", 6) == 0) {
    enterNgState();
    Serial.print("NG_REJECT");
    if (len > 7 && cmd[6] == ':') {
      Serial.print(' ');
      Serial.write((const uint8_t*)(cmd + 7), len - 7);
    }
    Serial.println();
    return;
  }

  // ── ALERT:<defects,list> ──────────────────────────────────────
  // Station2 조립검사 NG — 빨간 LED 3초
  if (len >= 5 && strncmp(cmd, "ALERT", 5) == 0) {
    enterNgState();
    Serial.print("NG_ALERT");
    if (len > 6 && cmd[5] == ':') {
      Serial.print(' ');
      Serial.write((const uint8_t*)(cmd + 6), len - 6);
    }
    Serial.println();
    return;
  }

  // ── "OK" 문자열 ──────────────────────────────────────────────
  // 명시적 정상 통지 — 즉시 초록으로 복귀 (NG 타이머 중이었어도 강제 해제)
  if (len >= 2 && cmd[0] == 'O' && cmd[1] == 'K') {
    returnToOkState();
    Serial.println("OK");
    return;
  }

  // ── 하위호환: 단일 문자 '0'/'1' ─────────────────────────────
  if (len == 1) {
    if (cmd[0] == '1') {
      returnToOkState();
      Serial.println("OK");
      return;
    }
    if (cmd[0] == '0') {
      enterNgState();
      Serial.println("NG_ON");
      return;
    }
  }

  // 알 수 없는 명령 — 에코만 (디버깅 용)
  Serial.print("UNKNOWN: ");
  Serial.write((const uint8_t*)cmd, len);
  Serial.println();
}


// ============================================================================
// loop — 메인 반복
// ============================================================================
void loop() {
  // ── 시리얼 수신 누적 (라인 단위) ────────────────────────────
  while (Serial.available() > 0) {
    char ch = Serial.read();

    // 줄바꿈('\n') 또는 CR('\r') → 한 줄 완성 → 명령 처리
    if (ch == '\n' || ch == '\r') {
      if (rx_len > 0) {
        rx_buf[rx_len] = '\0';   // null 종단
        handleCommand(rx_buf, rx_len);
        rx_len = 0;              // 버퍼 리셋
      }
      continue;
    }

    // 그 외 문자 — 버퍼에 누적 (overflow 방어)
    if (rx_len < RX_BUF_SIZE - 1) {
      rx_buf[rx_len++] = ch;
    } else {
      rx_len = 0;
      Serial.println("ERR: rx buffer overflow");
    }
  }

  // ── NG 상태 자동 해제 (3초 후 초록으로 복귀) ────────────────
  // NG 판정 직후의 연속 REJECT 스트림 (카메라 없을 때 초당 2건 등) 이 이어지면
  // enterNgState 가 호출될 때마다 타이머가 리셋되므로 "이상이 계속되는 동안엔
  // 계속 빨강" 이고, 이상 명령이 멈추면 3초 후 초록으로 자연스럽게 돌아간다.
  if (ng_active && (millis() - ng_start_time >= NG_DURATION)) {
    returnToOkState();
    Serial.println("LED_OK_RESTORED");
  }
}
