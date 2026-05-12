/*
 * =============================================
 * 수신기 (RX) - XIAO ESP32S3 + Wio-SX1262
 *             + MPU9250 + HW-203 SD카드
 * 
 * LoRa "START" 수신 → MPU9250 측정 시작 → SD카드에 CSV 저장
 * LoRa "STOP"  수신 → 측정 종료
 * 
 * 연결 핀:
 *   MPU9250: SCL=A5, SDA=A4, AD0=GND (주소 0x68)
 *   SD카드:  CS=D2, MOSI=D10, SCK=D8, MISO=D9
 *   SX1262:  CS=41, DIO1=39, RST=40, BUSY=38
 * =============================================
 */

#include <RadioLib.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>

// =============================================
// 핀 정의
// =============================================
#define SD_CS_PIN   D2   // D2

// =============================================
// LoRa 설정 (Wio-SX1262)
// =============================================
SX1262 radio = new Module(41, 39, 42, 40);

// =============================================
// MPU9250 설정
// =============================================
#define MPU9250_ADDR  0x68
#define ACCEL_XOUT_H  0x3B
#define GYRO_XOUT_H   0x43
#define PWR_MGMT_1    0x6B
#define ACCEL_CONFIG  0x1C
#define GYRO_CONFIG   0x1B

// 가속도 스케일: ±2g → 16384 LSB/g
// 자이로 스케일: ±250°/s → 131 LSB/°/s
#define ACCEL_SCALE   16384.0
#define GYRO_SCALE    131.0

// =============================================
// 센서 데이터 구조체 (전역 선언)
// =============================================
struct SensorData {
  float accX, accY, accZ;
  float gyroX, gyroY, gyroZ;
};

// =============================================
// 전역 변수
// =============================================
bool isRecording = false;
volatile bool loraReceived = false;
unsigned long lastMeasureTime = 0;
const unsigned long MEASURE_INTERVAL = 100;  // 100Hz = 10ms
unsigned long sampleCount = 0;             // flush 주기 카운터
unsigned long startTime = 0;
int fileIndex = 0;
String fileName = "";
File dataFile;
void setFlag(void) {
  loraReceived = true;
}

// =============================================
// MPU9250 초기화
// =============================================
void initMPU9250() {
  Wire.begin();

  // 슬립 모드 해제
  Wire.beginTransmission(MPU9250_ADDR);
  Wire.write(PWR_MGMT_1);
  Wire.write(0x00);
  Wire.endTransmission();
  delay(100);

  // 가속도 범위: ±2g
  Wire.beginTransmission(MPU9250_ADDR);
  Wire.write(ACCEL_CONFIG);
  Wire.write(0x00);
  Wire.endTransmission();

  // 자이로 범위: ±250°/s
  Wire.beginTransmission(MPU9250_ADDR);
  Wire.write(GYRO_CONFIG);
  Wire.write(0x00);
  Wire.endTransmission();

  Serial.println("MPU9250 초기화 완료");
}

SensorData readMPU9250() {
  SensorData data;

  // 가속도 읽기
  Wire.beginTransmission(MPU9250_ADDR);
  Wire.write(ACCEL_XOUT_H);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU9250_ADDR, 6, true);

  int16_t rawAccX = (Wire.read() << 8) | Wire.read();
  int16_t rawAccY = (Wire.read() << 8) | Wire.read();
  int16_t rawAccZ = (Wire.read() << 8) | Wire.read();

  data.accX = rawAccX / ACCEL_SCALE;
  data.accY = rawAccY / ACCEL_SCALE;
  data.accZ = rawAccZ / ACCEL_SCALE;

  // 자이로 읽기
  Wire.beginTransmission(MPU9250_ADDR);
  Wire.write(GYRO_XOUT_H);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU9250_ADDR, 6, true);

  int16_t rawGyroX = (Wire.read() << 8) | Wire.read();
  int16_t rawGyroY = (Wire.read() << 8) | Wire.read();
  int16_t rawGyroZ = (Wire.read() << 8) | Wire.read();

  data.gyroX = rawGyroX / GYRO_SCALE;
  data.gyroY = rawGyroY / GYRO_SCALE;
  data.gyroZ = rawGyroZ / GYRO_SCALE;

  return data;
}

// =============================================
// SD카드 파일 생성 (중복 방지)
// =============================================
String getNewFileName() {
  int idx = 0;
  String name;
  do {
    name = "/data_" + String(idx) + ".csv";
    idx++;
  } while (SD.exists(name));
  return name;
}

// =============================================
// 측정 시작
// =============================================
void startRecording() {
  fileName = getNewFileName();
  dataFile = SD.open(fileName, FILE_WRITE);

  if (!dataFile) {
    Serial.println("파일 열기 실패!");
    return;
  }

  // CSV 헤더 작성
  dataFile.println("Time(ms),AccX(g),AccY(g),AccZ(g),GyroX(deg/s),GyroY(deg/s),GyroZ(deg/s)");
  dataFile.flush();

  startTime = millis();
  sampleCount = 0;
  isRecording = true;

  Serial.println("=== 측정 시작 ===");
  Serial.print("저장 파일: ");
  Serial.println(fileName);
}

// =============================================
// 측정 종료
// =============================================
void stopRecording() {
  if (isRecording) {
    dataFile.flush();
    dataFile.close();
    isRecording = false;

    Serial.println("=== 측정 종료 ===");
    Serial.print("파일 저장 완료: ");
    Serial.println(fileName);
  }
}

// =============================================
// setup()
// =============================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== 수신기 초기화 중 ===");

  // WiFi 끄기 (전력 절약 및 간섭 방지)
  WiFi.mode(WIFI_OFF);
  btStop(); // 블루투스도 끄기
  Serial.println("WiFi/BT 비활성화 완료");

  // MPU9250 초기화
  initMPU9250();

  // SD카드 초기화
  SPI.begin(D8, D9, D10, D2); // SCK=D8, MISO=D9, MOSI=D10, CS=D2
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD카드 초기화 실패!");
    while (true);
  }
  Serial.println("SD카드 초기화 완료");

  // LoRa 초기화
  int state = radio.begin(921.9, 125.0, 7, 5, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 14, 8);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("LoRa 초기화 완료");
  } else {
    Serial.print("LoRa 초기화 실패, 에러: ");
    Serial.println(state);
    while (true);
  }

  radio.setDio1Action(setFlag);
  radio.startReceive();

  Serial.println("=== 수신기 준비 완료 ===");
  Serial.println("송신기에서 신호를 기다리는 중...");
}

// =============================================
// loop()
// =============================================
void loop() {
  // LoRa 신호 수신 확인
  if (loraReceived) {
    loraReceived = false;
    String received = "";
    int state = radio.readData(received);

    if (state == RADIOLIB_ERR_NONE) {
      Serial.print("LoRa 수신: ");
      Serial.println(received);

      if (received == "START" && !isRecording) {
        startRecording();
      } else if (received == "STOP" && isRecording) {
        stopRecording();
      }
    }
    radio.startReceive(); // 다시 수신 대기
  }

  // 측정 중이면 데이터 저장 (100Hz)
  if (isRecording) {
    unsigned long now = millis();
    if (now - lastMeasureTime >= MEASURE_INTERVAL) {
      lastMeasureTime = now;

      SensorData data = readMPU9250();
      unsigned long elapsed = now - startTime;

      // CSV에 데이터 기록
      dataFile.print(elapsed);       dataFile.print(",");
      dataFile.print(data.accX, 4);  dataFile.print(",");
      dataFile.print(data.accY, 4);  dataFile.print(",");
      dataFile.print(data.accZ, 4);  dataFile.print(",");
      dataFile.print(data.gyroX, 4); dataFile.print(",");
      dataFile.print(data.gyroY, 4); dataFile.print(",");
      dataFile.println(data.gyroZ, 4);

      // 100샘플(1초)마다 한 번만 flush (SD카드 부담 감소)
      sampleCount++;
      if (sampleCount % 100 == 0) {
        dataFile.flush();
      }

      // 시리얼 모니터 출력 (10샘플마다 한 번 - 시리얼 병목 방지)
      if (sampleCount % 10 == 0) {
        Serial.print(elapsed);
        Serial.print("ms | Acc: ");
        Serial.print(data.accX, 3); Serial.print(", ");
        Serial.print(data.accY, 3); Serial.print(", ");
        Serial.print(data.accZ, 3);
        Serial.print(" | Gyro: ");
        Serial.print(data.gyroX, 3); Serial.print(", ");
        Serial.print(data.gyroY, 3); Serial.print(", ");
        Serial.println(data.gyroZ, 3);
      }
    }
  }
}
