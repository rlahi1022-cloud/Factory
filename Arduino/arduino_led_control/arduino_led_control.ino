/*
 * arduino_led_control.ino — WS2812B(네오픽셀) LED 바 점등 테스트
 * ================================================================
 *
 * 목적:
 *   Python 프로그램(led_test.py)으로부터 시리얼(USB)로 검사 결과를 받아서
 *   WS2812B LED 바(8개)를 색상으로 제어하는 프로그램이다.
 *   실제 공장에서는 이 LED 바가 검사 라인의 경고등 역할을 한다.
 *
 * 동작 규칙:
 *   - '1' 수신 (정상/OK) → 8개 LED 전부 초록색 점등 → 3초 후 소멸
 *   - '0' 수신 (불량/NG) → 8개 LED 전부 빨간색 점등 → 3초 후 소멸
 *
 * 하드웨어 연결 (WS2812B LED 바 ↔ 아두이노):
 *   빨강 선 (VCC)  → 5V
 *   검정 선 (GND)  → GND
 *   흰색 선 (DIN)  → 디지털 6번 핀
 *
 * 필요 라이브러리:
 *   Adafruit NeoPixel (아두이노 IDE → 스케치 → 라이브러리 관리 → 검색 → 설치)
 *
 * 시리얼 통신:
 *   - 통신 속도: 9600 baud
 *   - 수신: '0' 또는 '1' (ASCII 문자 1바이트)
 *   - 응답: "OK", "NG_ON", "NG_OFF"
 */

// Adafruit_NeoPixel 라이브러리를 포함한다.
// 이 라이브러리가 WS2812B LED의 색상과 밝기를 제어하는 함수를 제공한다.
// #include: 외부 라이브러리 파일을 불러오는 C++ 전처리 지시문이다.
#include <Adafruit_NeoPixel.h>

// ── 설정 상수 ──

// LED 데이터 선이 연결된 아두이노 디지털 핀 번호이다.
// WS2812B는 데이터 1개 선으로 여러 LED를 제어한다 (직렬 통신 방식).
const int LED_PIN = 6;

// LED 바에 달린 LED 개수이다.
// 사진에서 확인한 LED 바는 8개짜리이다.
const int LED_COUNT = 8;

// LED 밝기 (0~255). 255가 최대 밝기이다.
// 너무 밝으면 눈이 부시므로 50 정도가 적당하다.
// 실제 공장에서는 밝기를 높여야 멀리서도 보인다.
const int BRIGHTNESS = 50;

// LED가 켜져 있을 시간 (밀리초). 3000ms = 3초.
const unsigned long LED_DURATION = 3000;

// ── 상태 변수 ──

// LED가 현재 켜져 있는지 추적하는 변수이다.
bool led_on = false;

// LED가 켜진 시점의 시간(밀리초)을 저장한다.
unsigned long led_on_time = 0;

// ── NeoPixel 객체 생성 ──
// Adafruit_NeoPixel(LED 개수, 핀 번호, LED 타입)
// NEO_GRB: WS2812B의 색상 순서 (Green-Red-Blue)
// NEO_KHZ800: WS2812B의 통신 속도 (800KHz)
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);


/*
 * setup() — 아두이노 초기 설정 함수
 *
 * 목적:
 *   전원 켜면 한 번 실행. 시리얼 통신 시작 + LED 바 초기화.
 */
void setup() {
  // 시리얼 통신을 9600 baud로 시작한다.
  Serial.begin(9600);

  // NeoPixel LED 바를 초기화한다.
  // begin(): 내부적으로 핀 모드 설정과 타이밍 준비를 한다.
  strip.begin();

  // LED 밝기를 설정한다 (0=꺼짐, 255=최대).
  strip.setBrightness(BRIGHTNESS);

  // 모든 LED를 끈 상태로 시작한다.
  // show(): 설정한 색상을 실제 LED에 반영하는 함수이다.
  // show()를 호출해야 LED가 실제로 바뀐다 (버퍼 → LED 전송).
  strip.clear();   // 모든 LED 색상을 (0,0,0) = 꺼짐으로 설정
  strip.show();    // 설정을 LED에 반영 (실제로 꺼짐)

  // Python에게 준비 완료 메시지를 보낸다.
  Serial.println("Arduino Ready");
}


/*
 * setAllLeds() — 모든 LED를 같은 색으로 설정하는 함수
 *
 * 목적:
 *   8개 LED를 한꺼번에 같은 색으로 켜거나 끈다.
 *
 * 매개변수:
 *   r (uint8_t): 빨강 값 (0~255)
 *   g (uint8_t): 초록 값 (0~255)
 *   b (uint8_t): 파랑 값 (0~255)
 *   예: (255, 0, 0) = 빨강, (0, 255, 0) = 초록, (0, 0, 0) = 꺼짐
 *
 * 반환값: 없음
 */
void setAllLeds(uint8_t r, uint8_t g, uint8_t b) {
  // 0번부터 7번까지 8개 LED를 순회한다.
  for (int i = 0; i < LED_COUNT; i++) {
    // i번째 LED의 색상을 (r, g, b)로 설정한다.
    // strip.Color(r, g, b): RGB 값을 하나의 32비트 색상 코드로 변환한다.
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  // 설정한 색상을 실제 LED에 반영한다.
  // 이 함수를 호출하기 전까지는 LED가 바뀌지 않는다.
  strip.show();
}


/*
 * loop() — 메인 반복 함수
 *
 * 목적:
 *   시리얼로 '0' 또는 '1'을 받아 LED 색상을 제어하고,
 *   3초 후 자동으로 LED를 끈다.
 */
void loop() {

  // ── 시리얼 데이터 수신 처리 ──
  if (Serial.available() > 0) {
    char received = Serial.read();

    // '1' 수신: 정상(OK) → 초록색 점등
    if (received == '1') {
      // 8개 LED를 초록색(0, 255, 0)으로 켠다.
      setAllLeds(0, 255, 0);
      // LED 상태를 켜짐으로 기록한다.
      led_on = true;
      // LED 켜진 시각을 기록한다 (3초 후 자동 소멸용).
      led_on_time = millis();
      // Python에게 응답한다.
      Serial.println("OK");
    }

    // '0' 수신: 불량(NG) → 빨간색 점등
    else if (received == '0') {
      // 8개 LED를 빨간색(255, 0, 0)으로 켠다.
      setAllLeds(255, 0, 0);
      led_on = true;
      led_on_time = millis();
      Serial.println("NG_ON");
    }
    // 그 외 문자('\n', '\r' 등)는 무시한다.
  }

  // ── LED 자동 소멸 (3초 후) ──
  // LED가 켜진 상태이고 3초가 지났으면 끈다.
  if (led_on && (millis() - led_on_time >= LED_DURATION)) {
    // 모든 LED를 끈다 (0, 0, 0) = 검정 = 꺼짐.
    setAllLeds(0, 0, 0);
    led_on = false;

    // 불량이었으면 "NG_OFF" 응답을 보낸다.
    // (정상 초록 소멸 시에도 보내지만, Python 로그에서는 무시됨)
    Serial.println("NG_OFF");
  }
}
