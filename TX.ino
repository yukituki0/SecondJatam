/*
 * =============================================
 * 송신기 (TX) - XIAO ESP32S3 + Wio-SX1262
 * 
 * serial terminal에 's' 입력 → LoRa "START" 송신
 * serial terminal에 'e' 입력 → LoRa "STOP" 송신
 * 
 * 연결 핀:
 *   없음
 * =============================================
 */

#include <RadioLib.h>

SX1262 radio = new Module(41, 39, 42, 40);

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println("=== 송신기 초기화 중 ===");
  int state = radio.begin(921.9, 125.0, 7, 5, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 14, 8);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("LoRa 초기화 성공!");
  } else {
    Serial.print("LoRa 초기화 실패, 에러: ");
    Serial.println(state);
    while (true);
  }

  Serial.println("=== 송신기 준비 완료 ===");
  Serial.println("시리얼 모니터에 명령어 입력:");
  Serial.println("  's' → 측정 시작 신호 전송");
  Serial.println("  'e' → 측정 종료 신호 전송");
}

void loop() {
  if (Serial.available()) {
    char cmd = Serial.read();

    if (cmd == 's') {
      Serial.print("시작 신호 전송 중... ");
      int state = radio.transmit("START");

      if (state == RADIOLIB_ERR_NONE) {
        Serial.println("전송 성공!");
      } else {
        Serial.print("전송 실패, 에러: ");
        Serial.println(state);
      }

    } else if (cmd == 'e') {
      Serial.print("종료 신호 전송 중... ");
      int state = radio.transmit("STOP");

      if (state == RADIOLIB_ERR_NONE) {
        Serial.println("전송 성공!");
      } else {
        Serial.print("전송 실패, 에러: ");
        Serial.println(state);
      }
    }
  }
}
