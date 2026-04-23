/*
 * arduino_led_control.ino — Factory QC 신호등 (v2 — Python 프로토콜 대응)
 * =======================================================================
 *
 * 목적:
 *   AI 추론서버(Station1/Station2)가 시리얼(USB)로 보내는 검사 결과 명령을 받아
 *   WS2812B 네오픽셀 LED 바(8개)를 색상으로 제어한다.
 *
 * 지원 명령 (v0.14.x Python 프로토콜 — SerialCtrl.py):
 *   "REJECT:<defect>\n"        Station1 입고검사 NG
 *                              예: "REJECT:anomaly\n", "REJECT:crack\n"
 *                              → 빨간색 LED 3초 점등 (리젝트 경고)
 *
 *   "ALERT:<d1,d2,..>\n"       Station2 조립검사 NG (복수 결함 가능)
 *                              예: "ALERT:cap_missing,label_tilt\n"
 *                              → 주황색 LED 3초 점등 (조립 불량 경고)
 *
 *   "OK\n"                     (선택) 정상 판정 직접 통지
 *                              → 초록색 LED 3초 점등
 *
 * 하위호환 (구 프로토콜 — led_test.py 기타):
 *   '1'  → 초록색 LED (정상)
 *   '0'  → 빨간색 LED (불량)
 *
 * 동작 규칙:
 *   - 새 명령 수신 시 3초 동안 색상 유지 후 자동 소멸
 *   - 3초 유지 중 새 명령이 와도 즉시 갱신 (타이머 리셋)
 *   - Python 으로 응답 메시지 송신 (로그용)
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
 *   - 부저(passive buzzer) 연동
 *   - Station2 LCD (I2C 16x2) 에 결함 유형 한글/영문 표시
 *   - 결함 유형별 다른 색상/점멸 패턴 매핑
 */

#include <Adafruit_NeoPixel.h>

// ============================================================================
// 설정 상수
// ============================================================================

const int  LED_PIN       = 6;        // WS2812B 데이터 선 핀
const int  LED_COUNT     = 8;        // LED 개수
const int  BRIGHTNESS    = 50;       // 밝기 (0~255) — 실공장에선 더 높게
const unsigned long LED_DURATION = 3000;  // 점등 유지 시간 (3초)

// 수신 버퍼 크기 — "ALERT:cap_missing,label_tilt,fill_low\n" 정도 여유 있게
const int  RX_BUF_SIZE   = 128;

// ============================================================================
// 색상 팔레트 (RGB)
// ============================================================================

struct RGB { uint8_t r, g, b; };
const RGB COLOR_OK     = {  0, 255,   0};   // 초록 — 정상
const RGB COLOR_REJECT = {255,   0,   0};   // 빨강 — Station1 리젝트
const RGB COLOR_ALERT  = {255, 120,   0};   // 주황 — Station2 조립 불량
const RGB COLOR_OFF    = {  0,   0,   0};   // 꺼짐

// ============================================================================
// 상태 변수
// ============================================================================

bool led_on = false;
unsigned long led_on_time = 0;

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
  strip.clear();
  strip.show();

  Serial.println("Arduino Ready (Factory QC v2)");
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
// setLedWithTimer — 색상 점등 + 자동 소멸 타이머 시작
//   이미 켜진 상태여도 새 명령이 오면 색상 교체 + 타이머 리셋
// ============================================================================
void setLedWithTimer(const RGB& c) {
  setAllLeds(c);
  led_on      = true;
  led_on_time = millis();
}


// ============================================================================
// handleCommand — 누적된 한 줄(rx_buf) 을 파싱해서 LED 동작
// ============================================================================
void handleCommand(const char* cmd, int len) {
  // 빈 줄 / 제어문자 단독 → 무시
  if (len <= 0) return;

  // ── REJECT:<defect> ───────────────────────────────────────────
  // Station1 입고검사 NG — 빨간 LED 3초
  if (len >= 6 && strncmp(cmd, "REJECT", 6) == 0) {
    setLedWithTimer(COLOR_REJECT);
    // 결함 유형 반환 (디버그 편의 — Python 로그에 echo)
    Serial.print("NG_REJECT");
    if (len > 7 && cmd[6] == ':') {
      Serial.print(' ');
      Serial.write((const uint8_t*)(cmd + 7), len - 7);
    }
    Serial.println();
    return;
  }

  // ── ALERT:<defects,list> ──────────────────────────────────────
  // Station2 조립검사 NG — 주황 LED 3초
  if (len >= 5 && strncmp(cmd, "ALERT", 5) == 0) {
    setLedWithTimer(COLOR_ALERT);
    Serial.print("NG_ALERT");
    if (len > 6 && cmd[5] == ':') {
      Serial.print(' ');
      Serial.write((const uint8_t*)(cmd + 6), len - 6);
    }
    Serial.println();
    return;
  }

  // ── "OK" 문자열 (선택) ────────────────────────────────────────
  // 명시적 정상 통지 — 초록 LED 3초
  if (len >= 2 && cmd[0] == 'O' && cmd[1] == 'K') {
    setLedWithTimer(COLOR_OK);
    Serial.println("OK");
    return;
  }

  // ── 하위호환: 단일 문자 '0'/'1' ─────────────────────────────
  // 예전 스케치와 동일 (led_test.py 등 구 도구)
  if (len == 1) {
    if (cmd[0] == '1') {
      setLedWithTimer(COLOR_OK);
      Serial.println("OK");
      return;
    }
    if (cmd[0] == '0') {
      setLedWithTimer(COLOR_REJECT);
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
      // 버퍼 꽉 참 → 비정상 입력 → 리셋해서 다음 줄 대기
      rx_len = 0;
      Serial.println("ERR: rx buffer overflow");
    }
  }

  // ── LED 자동 소멸 (3초 후) ──────────────────────────────────
  if (led_on && (millis() - led_on_time >= LED_DURATION)) {
    setAllLeds(COLOR_OFF);
    led_on = false;
    Serial.println("LED_OFF");
  }
}
