#include <SoftwareSerial.h>                                   // Библиотека програмной реализации обмена по UART-протоколу
#include <OneWire.h>
#include <DallasTemperature.h>

#define TEMPERATURE 10
#define PTS 5
#define BRAKE 4
#define IMMO 3
#define ALARM 2

#define ENGINE_DEFAULT_WARM_TIME 15L * 60L * 1000L             //Время прогрева двигателяпо умолчанию
#define UPDATE_INTERVAL 15L * 1000L                            // Проверять SMS каждые 15 секунд

OneWire oneWire(TEMPERATURE);
DallasTemperature sensors(&oneWire);
SoftwareSerial SIM800(12, 11);                                  // RX, TX

String _response     = "";                                     // Переменная для хранения ответа модуля
unsigned long lastUpdate      = millis();                               // Время последнего обновления
long engineWarmTime  = ENGINE_DEFAULT_WARM_TIME;                      // Время работы автозапуска 15 минут
unsigned long engineWarmStart = 0L;                                     // Время начала прогрева двигателя

String phones = "+7xxxxxxxxxx";   // Белый список телефонов

void setup() {
  pinMode(PTS, OUTPUT);
  pinMode(BRAKE, OUTPUT);
  pinMode(IMMO, OUTPUT);
  pinMode(ALARM, OUTPUT);

  Serial.begin(9600);                                         // Скорость обмена данными с компьютером
  SIM800.begin(9600);                                         // Скорость обмена данными с модемом
  Serial.println("Start!");

  sendATCommand("AT", true);                                  // Отправили AT для настройки скорости обмена данными
  sendATCommand("AT+CMGDA=\"DEL ALL\"", true);               // Удаляем все SMS, чтобы не забивать память

  // Команды настройки модема при каждом запуске
  _response = sendATCommand("AT+CLIP=1", true);             // Включаем АОН
  //_response = sendATCommand("AT+DDET=1", true);             // Включаем DTMF
  sendATCommand("AT+CMGF=1;&W", true);                        // Включаем текстовый режима SMS (Text mode) и сразу сохраняем значение (AT&W)!
  lastUpdate = millis();                                      // Обнуляем таймер
  sensors.requestTemperatures();
}

bool hasmsg = false;                                              // Флаг наличия сообщений к удалению
void loop() {
  unsigned long currentMillis = millis();                                                                //Проверка окончания цикла работы двигателя
  if (engineWarmStart > 0 && (unsigned long)(currentMillis - engineWarmStart) >= engineWarmTime) {
    stopEngine(phones);
  } else if ((unsigned long)(currentMillis - lastUpdate) >= UPDATE_INTERVAL) {             // Пора проверить наличие новых сообщений
    do {
      _response = sendATCommand("AT+CMGL=\"REC UNREAD\",1", true);// Отправляем запрос чтения непрочитанных сообщений
      if (_response.indexOf("+CMGL: ") > -1) {                    // Если есть хоть одно, получаем его индекс
        int msgIndex = _response.substring(_response.indexOf("+CMGL: ") + 7, _response.indexOf("\"REC UNREAD\"", _response.indexOf("+CMGL: "))).toInt();
        char i = 0;                                               // Объявляем счетчик попыток
        do {
          i++;                                                    // Увеличиваем счетчик
          _response = sendATCommand("AT+CMGR=" + (String)msgIndex + ",1", true);  // Пробуем получить текст SMS по индексу
          _response.trim();                                       // Убираем пробелы в начале/конце
          if (_response.endsWith("OK")) {                         // Если ответ заканчивается на "ОК"
            if (!hasmsg) hasmsg = true;                           // Ставим флаг наличия сообщений для удаления
            sendATCommand("AT+CMGR=" + (String)msgIndex, true);   // Делаем сообщение прочитанным
            sendATCommand("\n", true);                            // Перестраховка - вывод новой строки
            parseSMS(_response);                                  // Отправляем текст сообщения на обработку
            break;                                                // Выход из do{}
          } else {                                                // Если сообщение не заканчивается на OK
            Serial.println ("Error answer");                      // Какая-то ошибка
            sendATCommand("\n", true);                            // Отправляем новую строку и повторяем попытку
          }
        } while (i < 10);
        break;
      } else {
        lastUpdate = millis();                                    // Обнуляем таймер
        if (hasmsg) {
          sendATCommand("AT+CMGDA=\"DEL READ\"", true);           // Удаляем все прочитанные сообщения
          hasmsg = false;
        }
        break;
      }
    } while (1);
  }

  if (SIM800.available())   {                         // Если модем, что-то отправил...
    _response = waitResponse();                       // Получаем ответ от модема для анализа
    _response.trim();                                 // Убираем лишние пробелы в начале и конце
    Serial.println(_response);                        // Если нужно выводим в монитор порта
    if (_response.indexOf("+CMTI:") > -1) {           // Пришло сообщение об отправке SMS
      lastUpdate = millis() - UPDATE_INTERVAL;          // Теперь нет необходимости обрабатываеть SMS здесь, достаточно просто

      // сбросить счетчик автопроверки и в следующем цикле все будет обработано
    } else if (_response.indexOf("RING") > -1) {
      parseIncomingCall(_response);
    }
  }
  if (Serial.available())  {                          // Ожидаем команды по Serial...
    SIM800.write(Serial.read());                      // ...и отправляем полученную команду модему
  };
}

String sendATCommand(String cmd, bool waiting) {
  String _resp = "";                                              // Переменная для хранения результата
  Serial.println(cmd);                                            // Дублируем команду в монитор порта
  SIM800.println(cmd);                                            // Отправляем команду модулю
  if (waiting) {                                                  // Если необходимо дождаться ответа...
    _resp = waitResponse();                                       // ... ждем, когда будет передан ответ
    // Если Echo Mode выключен (ATE0), то эти 3 строки можно закомментировать
    if (_resp.startsWith(cmd)) {                                  // Убираем из ответа дублирующуюся команду
      _resp = _resp.substring(_resp.indexOf("\r", cmd.length()) + 2);
    }
    Serial.println(_resp);                                        // Дублируем ответ в монитор порта
  }
  return _resp;                                                   // Возвращаем результат. Пусто, если проблема
}

String waitResponse() {                                           // Функция ожидания ответа и возврата полученного результата
  String _resp = "";                                              // Переменная для хранения результата
  unsigned long _timeout = 10000;
  unsigned long previousResponseCounter = millis();
  while (!SIM800.available() && _timeout > (unsigned char)(millis() - previousResponseCounter )) {} // check for rollover

  if (SIM800.available()) {                                       // Если есть, что считывать...
    _resp = SIM800.readString();                                  // ... считываем и запоминаем
  } else {                                                          // Если пришел таймаут, то...
    Serial.println("Timeout...");                                 // ... оповещаем об этом и...
  }
  return _resp;                                                   // ... возвращаем результат. Пусто, если проблема
}

void parseIncomingCall(String msg) {
  String msgRing = msg.substring(msg.indexOf("+CLIP: "));
  int firstIndex = msgRing.indexOf("\"") + 1;
  int secondIndex = msgRing.indexOf("\",", firstIndex);
  String ringPhone = msgRing.substring(firstIndex, secondIndex);
  Serial.println("Incoming ring: " + ringPhone);
  if (phones.indexOf(ringPhone) > -1) {
    sendATCommand("ATH", true);
    sendATCommand("AT+CMGDA=\"DEL ALL\"", true);
  }
}

void parseSMS(String msg) {                                   // Парсим SMS
  String msgheader  = "";
  String msgbody    = "";
  String msgphone   = "";

  msg = msg.substring(msg.indexOf("+CMGR: "));
  msgheader = msg.substring(0, msg.indexOf("\r"));            // Выдергиваем телефон

  msgbody = msg.substring(msgheader.length() + 2);
  msgbody = msgbody.substring(0, msgbody.lastIndexOf("OK"));  // Выдергиваем текст SMS
  msgbody.trim();

  int firstIndex = msgheader.indexOf("\",\"") + 3;
  int secondIndex = msgheader.indexOf("\",\"", firstIndex);
  msgphone = msgheader.substring(firstIndex, secondIndex);

  Serial.println("Phone: " + msgphone);                       // Выводим номер телефона
  Serial.println("Message: " + msgbody);                      // Выводим текст SMS

  if (msgphone.length() > 6 && phones.indexOf(msgphone) > -1) { // Если телефон в белом списке, то...
    msgbody.toLowerCase();
    if (msgbody.equalsIgnoreCase("status")) {
      getStatus(msgphone);
    } else if (msgbody.startsWith("start")) {
      if (msgbody.equalsIgnoreCase("start")) {
        startEngine(msgphone, 0);
      } else {
        long workTimeTmp = msgbody.substring(msgbody.indexOf(" ") + 1).toInt() * 1000L * 60L;
        startEngine(msgphone, workTimeTmp);
      }
    } else if (msgbody.equalsIgnoreCase("stop")) {
      stopEngine(msgphone);
    }
  } else {
    Serial.println("Unknown phonenumber");
  }
}

String getTemperature() {
  sensors.requestTemperatures(); // Send the command to get temperatures
  float temperature = sensors.getTempCByIndex(0);
  char tmp[20];
  dtostrf(temperature, 2, 1, tmp);
  return tmp;
}

void startEngine(String responsePhone, long workTime) {
  if (engineWarmStart > 0) {
    sendSMS(responsePhone, "Engine already started\nWork time remain: " + getRemainWorkTime() + "m\nTemperature: " + getTemperature() + "C");
    return;
  }
  if (workTime > 0) {
    engineWarmTime = workTime;
  } else {
    engineWarmTime = ENGINE_DEFAULT_WARM_TIME;
  }
  engineWarmStart = millis();
  digitalWrite(IMMO, 1);
  delay(2000);
  digitalWrite(BRAKE, 1);
  delay(2000);
  digitalWrite(PTS, 1);
  delay(2000);
  digitalWrite(PTS, 0);
  delay(5000);
  digitalWrite(IMMO, 0);
  digitalWrite(BRAKE, 0);
  sendSMS(responsePhone, "Engine started\nWork time remain: " + getRemainWorkTime() + "m\nTemperature: " + getTemperature() + "C");
}

void stopEngine(String responsePhone) {
  if (engineWarmStart == 0) {
    sendSMS(responsePhone, "Engine is not started!\nTemperature: " + getTemperature() + "C");
    return;
  }
  digitalWrite(PTS, 1);
  delay(1000);
  digitalWrite(PTS, 0);
  sendSMS(responsePhone, "Engine stopped\nWork time: " + getWorkTime() + "m\nTemperature: " + getTemperature() + "C");
  engineWarmStart = 0L;
}

void getStatus(String responsePhone) {
  if (engineWarmStart > 0) {
    sendSMS(responsePhone, "Engine is on\nWork time remain: " + getRemainWorkTime() + "m\nTemperature: " + getTemperature() + "C");
  } else {
    sendSMS(responsePhone, "Engine is off\nTemperature: " + getTemperature() + "C");
  }
}

String getRemainWorkTime() {
  char tmp[20];
  sprintf(tmp, "%d", (engineWarmTime - (millis() - engineWarmStart)) / 60L / 1000L);
  return tmp;
}

String getWorkTime() {
  char tmp[20];
  sprintf(tmp, "%d", (millis() - engineWarmStart) / 60L / 1000L);
  return tmp;
}

void sendSMS(String phone, String message)
{
  sendATCommand("AT+CMGS=\"" + phone + "\"", true);             // Переходим в режим ввода текстового сообщения
  sendATCommand(message + "\r\n" + (String)((char)26), true);   // После текста отправляем перенос строки и Ctrl+Z
}

