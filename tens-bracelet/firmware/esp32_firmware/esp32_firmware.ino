/*
  TENS_Bracelet - Firmware ESP32 DevKit V1
  Control de 3 electrodos mediante modulo de reles (logica activa en LOW).

  IMPORTANTE: Web Bluetooth solo soporta Bluetooth Low Energy (BLE/GATT).
  BluetoothSerial.h usa Bluetooth Classic (SPP), que NO es visible para
  navigator.bluetooth. Por eso este firmware expone un "puerto serie BLE"
  (Nordic UART Service). Los comandos son identicos: 1, 2, 3, 0, S.

  Comandos:
    '1' / '2' / '3' -> activa unicamente ese electrodo
    '0'             -> PARO DE EMERGENCIA (todos los reles abiertos)
    'S'             -> secuencia automatica 1 -> 2 -> 3 cada 2 s (sin delay)
    '?'             -> consulta de estado

  Seguridad adicional:
    - Pines en HIGH (rele abierto) antes de iniciar cualquier comunicacion.
    - Al desconectarse el cliente BLE se apagan todos los reles.
    - Conmutacion "abrir antes de cerrar" con tiempo muerto de 80 ms.
    - Apagado automatico tras MAX_SESSION_MS de sesion continua.

  Compatible con ESP32 Arduino core 2.x y 3.x.
*/

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ---------- Hardware ----------
static const uint8_t RELAY_PINS[3]  = {16, 17, 18};  // Electrodos 1, 2, 3
static const uint8_t RELAY_RESERVED = 19;            // Reservado / libre
static const uint8_t RELAY_ON  = LOW;                // LOW  = rele cerrado
static const uint8_t RELAY_OFF = HIGH;               // HIGH = rele abierto

// ---------- Tiempos ----------
static const unsigned long SEQ_INTERVAL_MS = 2000UL;
static const unsigned long DEAD_TIME_MS    = 80UL;
static const unsigned long MAX_SESSION_MS  = 15UL * 60UL * 1000UL;

// ---------- BLE (Nordic UART Service) ----------
#define DEVICE_NAME   "TENS_Bracelet"
#define SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_RX_UUID  "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // cliente -> ESP32
#define CHAR_TX_UUID  "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // ESP32 -> cliente

BLEServer*         pServer = nullptr;
BLECharacteristic* pTx     = nullptr;

volatile bool deviceConnected = false;
volatile bool disconnectFlag  = false;

// ---------- Cola de comandos (productor: callback BLE, consumidor: loop) ----------
static const uint8_t CMD_QUEUE_SIZE = 32;
volatile char    cmdQueue[CMD_QUEUE_SIZE];
volatile uint8_t cmdHead = 0;
volatile uint8_t cmdTail = 0;

void pushCommand(char c) {
  uint8_t next = (cmdHead + 1) % CMD_QUEUE_SIZE;
  if (next == cmdTail) return;  // cola llena: descartar
  cmdQueue[cmdHead] = c;
  cmdHead = next;
}

bool popCommand(char &c) {
  if (cmdTail == cmdHead) return false;
  c = cmdQueue[cmdTail];
  cmdTail = (cmdTail + 1) % CMD_QUEUE_SIZE;
  return true;
}

// ---------- Estado ----------
enum Mode { MODE_OFF, MODE_MANUAL, MODE_SEQUENCE };

Mode          mode              = MODE_OFF;
int8_t        currentElectrode  = -1;   // electrodo con rele cerrado (-1 = ninguno)
int8_t        pendingElectrode  = -1;   // electrodo esperando el tiempo muerto
unsigned long pendingAt         = 0;
int8_t        seqIndex          = 0;
unsigned long lastSeqSwitch     = 0;
unsigned long sessionStart      = 0;

// ---------- Utilidades ----------
void notifyClient(const char *msg) {
  Serial.println(msg);
  if (deviceConnected && pTx != nullptr) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%s\n", msg);
    pTx->setValue((uint8_t *)buf, strlen(buf));
    pTx->notify();
  }
}

void relaysAllOff() {
  for (uint8_t i = 0; i < 3; i++) digitalWrite(RELAY_PINS[i], RELAY_OFF);
  digitalWrite(RELAY_RESERVED, RELAY_OFF);
  currentElectrode = -1;
}

void stopAll(const char *reason) {
  relaysAllOff();
  pendingElectrode = -1;
  mode = MODE_OFF;
  notifyClient(reason);
}

// Abre todos los reles y programa el cierre del indicado tras el tiempo muerto.
void activateElectrode(int8_t idx) {
  relaysAllOff();
  pendingElectrode = idx;
  pendingAt = millis() + DEAD_TIME_MS;
}

void announceElectrode(int8_t idx) {
  char msg[16];
  if (mode == MODE_SEQUENCE) snprintf(msg, sizeof(msg), "SEQ E%d", idx + 1);
  else                       snprintf(msg, sizeof(msg), "E%d ON", idx + 1);
  notifyClient(msg);
}

void reportStatus() {
  char msg[16];
  if (mode == MODE_OFF) {
    snprintf(msg, sizeof(msg), "STATE OFF");
  } else if (mode == MODE_SEQUENCE) {
    snprintf(msg, sizeof(msg), "STATE SEQ");
  } else {
    int8_t e = (currentElectrode >= 0) ? currentElectrode : pendingElectrode;
    snprintf(msg, sizeof(msg), "STATE E%d", e + 1);
  }
  notifyClient(msg);
}

void startSessionIfIdle() {
  if (mode == MODE_OFF) sessionStart = millis();
}

void handleCommand(char c) {
  switch (c) {
    case '0':
      stopAll("STOP");
      break;

    case '1':
    case '2':
    case '3': {
      int8_t idx = c - '1';
      if (mode == MODE_MANUAL && (currentElectrode == idx || pendingElectrode == idx)) break;
      startSessionIfIdle();
      mode = MODE_MANUAL;
      activateElectrode(idx);
      break;
    }

    case 'S':
    case 's':
      if (mode == MODE_SEQUENCE) break;
      startSessionIfIdle();
      mode = MODE_SEQUENCE;
      seqIndex = 0;
      lastSeqSwitch = millis();
      activateElectrode(0);
      break;

    case '?':
      reportStatus();
      break;

    default:
      break;  // ignora CR, LF, espacios y cualquier otro caracter
  }
}

// ---------- Callbacks BLE ----------
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    deviceConnected = true;
  }
  void onDisconnect(BLEServer *server) override {
    deviceConnected = false;
    disconnectFlag = true;
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    auto value = characteristic->getValue();
    for (size_t i = 0; i < value.length(); i++) {
      pushCommand((char)value[i]);
    }
  }
};

// ---------- Setup ----------
void setup() {
  // 1) Estado seguro ANTES de cualquier otra cosa: reles abiertos (HIGH).
  for (uint8_t i = 0; i < 3; i++) {
    digitalWrite(RELAY_PINS[i], RELAY_OFF);
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], RELAY_OFF);
  }
  digitalWrite(RELAY_RESERVED, RELAY_OFF);
  pinMode(RELAY_RESERVED, OUTPUT);
  digitalWrite(RELAY_RESERVED, RELAY_OFF);

  // 2) Comunicaciones
  Serial.begin(115200);
  Serial.println("TENS_Bracelet: arranque seguro, todos los reles abiertos");

  BLEDevice::init(DEVICE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *service = pServer->createService(SERVICE_UUID);

  pTx = service->createCharacteristic(CHAR_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  pTx->addDescriptor(new BLE2902());

  BLECharacteristic *rx = service->createCharacteristic(
      CHAR_RX_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCallbacks());

  service->start();

  BLEAdvertising *adv = pServer->getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();

  Serial.println("BLE anunciando como " DEVICE_NAME);
}

// ---------- Loop (no bloqueante) ----------
void loop() {
  unsigned long now = millis();

  // Cliente desconectado: apagar todo y volver a anunciar
  if (disconnectFlag) {
    disconnectFlag = false;
    relaysAllOff();
    pendingElectrode = -1;
    mode = MODE_OFF;
    Serial.println("Cliente desconectado: reles abiertos");
    pServer->getAdvertising()->start();
  }

  // Comandos BLE
  char c;
  while (popCommand(c)) handleCommand(c);

  // Comandos por USB (depuracion)
  while (Serial.available() > 0) handleCommand((char)Serial.read());

  now = millis();

  // Cierre del rele pendiente tras el tiempo muerto
  if (pendingElectrode >= 0 && (long)(now - pendingAt) >= 0) {
    digitalWrite(RELAY_PINS[pendingElectrode], RELAY_ON);
    currentElectrode = pendingElectrode;
    pendingElectrode = -1;
    announceElectrode(currentElectrode);
  }

  // Secuencia automatica
  if (mode == MODE_SEQUENCE && pendingElectrode < 0 && (now - lastSeqSwitch) >= SEQ_INTERVAL_MS) {
    seqIndex = (seqIndex + 1) % 3;
    lastSeqSwitch = now;
    activateElectrode(seqIndex);
  }

  // Limite de sesion continua
  if (mode != MODE_OFF && (now - sessionStart) >= MAX_SESSION_MS) {
    stopAll("AUTO-OFF");
  }
}
