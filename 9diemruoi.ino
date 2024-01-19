#include <WiFi.h>
#include <time.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "SimpleDHT.h"  
#include "Font_Data.h"

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define CLK_PIN   18
#define DATA_PIN  19
#define CS_PIN    5

int pinDHT11 = 23;
SimpleDHT11 dht11(pinDHT11);

MD_Parola myDisplay = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
String inputString = "";
boolean stringComplete = false;
String commandString = "";
boolean isConnected = false;
String text = "";

#define SPEED_TIME  75
#define PAUSE_TIME  0
#define MAX_MESG  20

void displayTask(void *pvParameters);
void timeTask(void *pvParameters);
void processCommandTask(void *pvParameters);

TaskHandle_t DisplayTask;
TaskHandle_t TimeTask;
TaskHandle_t ProcessCommandTask;
SemaphoreHandle_t mutex;

const char* ssid = "Phat";
const char* password = "46552003";

int timezone = 7 * 3600; // UTC+7 giờ Việt Nam
int dst = 0;             // Không có Daylight Saving Time
uint16_t  h, m;
uint8_t dow;
int  day;
uint8_t month;
String  year;
char szTime[9];    // hh:mm\0
char szMesg[MAX_MESG+1] = "";

uint8_t degC[] = { 6, 3, 3, 56, 68, 68, 68 }; // Deg C

char *mon2str(uint8_t mon, char *psz, uint8_t len)
{
  static const char str[][4] PROGMEM =
  {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };

  *psz = '\0';
  mon--;
  if (mon < 12)
  {
    strncpy_P(psz, str[mon], len);
    psz[len] = '\0';
  }

  return(psz);
}

char *dow2str(uint8_t code, char *psz, uint8_t len)
{
  static const char str[][10] PROGMEM =
  {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
  };

  *psz = '\0';
  code--;
  if (code < 7)
  {
    strncpy_P(psz, str[code], len);
    psz[len] = '\0';
  }

  return(psz);
}

void getTime(char *psz, bool f = true)
{
  time_t now = time(nullptr);
  struct tm* p_tm = localtime(&now);
  h = p_tm->tm_hour;
  m = p_tm->tm_min;
  sprintf(psz, "%02d%02d", h, m);
}

void getDate(char *psz)
{
  char  szBuf[10];
  time_t now = time(nullptr);
  struct tm* p_tm = localtime(&now);
  dow = p_tm->tm_wday+1;
  day = p_tm->tm_mday;
  month = p_tm->tm_mon + 1;
  sprintf(psz, "%d %s %04d", day, mon2str(month, szBuf, sizeof(szBuf)-1), (p_tm->tm_year + 1900));
}

void getHumidit(char *psz)
{ 
  byte temperature = 0;
  byte humidity = 0;
  int err = SimpleDHTErrSuccess;
  if ((err = dht11.read(&temperature, &humidity, NULL)) != SimpleDHTErrSuccess) {
    delay(100);
    return;
  }
  float  hu = ((int)humidity);
  dtostrf(hu, 3, 0, szMesg);
  strcat(szMesg, " %RH");
}

void getTemperatur(char *psz)
{
  byte temperature = 0;
  byte humidity = 0;
  int err = SimpleDHTErrSuccess;
  if ((err = dht11.read(&temperature, &humidity, NULL)) != SimpleDHTErrSuccess) {
    delay(100);
    return;
  }
  float t = ((int)temperature);
  dtostrf(t, 3, 0, szMesg);
  strcat(szMesg, " $");
}

void getCommand()
{
  if (inputString.length() > 0)
  {
    commandString = inputString.substring(1, 2);
    text = inputString.substring(3, inputString.length() - 1);
  }
}

// String getTextToPrint()
// {
//   if (inputString.length() > 0)
//   {
//     return inputString.substring(1, 3);
//   }
//   return "";
// }

// void serialEvent()
// {
//   while (Serial.available())
//   {
//     char inChar = (char)Serial.read();
//     inputString += inChar;
//     if (inChar == '\n')
//     {
//       stringComplete = true;
//     }
//   }
// }

void setup(void)
{
  Serial.begin(9600);
  myDisplay.begin();

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  delay(2000);  
  getTimentp();

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  
  delay(3000);
  getTimentp();
  
  myDisplay.begin(3);
  myDisplay.setInvert(false);

  myDisplay.setZone(0, 0, 1); // thu ngay thang nam
  myDisplay.setZone(1, 2, 3);// gio phut
  myDisplay.setFont(1, numeric7Seg);
  myDisplay.setFont(2, numeric7Se);
  
  myDisplay.displayZoneText(1, szTime, PA_CENTER, SPEED_TIME, 0,PA_OPENING_CURSOR,PA_SCAN_HORIZX);
  myDisplay.displayZoneText(0, szMesg, PA_CENTER, SPEED_TIME, 0, PA_PRINT, PA_SCROLL_LEFT);


  myDisplay.addChar('$', degC);
  
  getDate(szMesg);
  getTime(szTime);

  mutex = xSemaphoreCreateMutex();
  
  xTaskCreatePinnedToCore(processCommandTask, "ProcessCommandTask", 8000, NULL, 0, &ProcessCommandTask, 0);
  xTaskCreatePinnedToCore(displayTask, "DisplayTask", 8000, NULL, 1, &DisplayTask, 0);
  xTaskCreatePinnedToCore(timeTask, "TimeTask", 8000, NULL, 0, &TimeTask, 0);
}

void getTimentp()
{
  configTime(timezone, dst, "pool.ntp.org","time.nist.gov");

  while(!time(nullptr)){
    delay(500);
    Serial.print(".");
  }
  
  Serial.print("Time Update");
}

void displayTask(void *pvParameters)
{
  while (1)
  {
    DisplayTask = xTaskGetCurrentTaskHandle();
    static uint8_t  display = 0;  
    
    myDisplay.displayAnimate();
    
    if (myDisplay.getZoneStatus(0))
    {
      xSemaphoreTake(mutex, portMAX_DELAY);
      
      switch (display)
      {
        case 0: // Thứ
          myDisplay.setTextEffect(0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
          display++;
          dow2str(dow, szMesg, MAX_MESG);
          break;

        case 1: // Ngày tháng năm
          myDisplay.setTextEffect(0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
          display++;
          getDate(szMesg);
          break;

        case 2: // Nhiệt độ
          myDisplay.setTextEffect(0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
          display++;
          getTemperatur(szMesg);
          break;

        default: // Độ ẩm
          myDisplay.setTextEffect(0, PA_SCROLL_LEFT,PA_BLINDS);
          display = 0;
          getHumidit(szMesg);
          break;
      }
      
      myDisplay.displayReset(0);
      xSemaphoreGive(mutex);
    }
    
    vTaskDelay(pdMS_TO_TICKS(100)); 
  }
}

void timeTask(void *pvParameters)
{
  while (1)
  {
    TimeTask = xTaskGetCurrentTaskHandle();
    static uint32_t lastTime = 0; 
    static bool flasher = false;  
    
    if (millis() - lastTime >= 2500)
    {
      lastTime = millis();
      xSemaphoreTake(mutex, portMAX_DELAY);
      
      getTime(szTime, flasher);
      flasher = !flasher;

      myDisplay.displayReset(1);
      myDisplay.displayReset(2);
      xSemaphoreGive(mutex);
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000)); 
  }
}

void processCommandTask(void *pvParameters)
{
  while (1)
  {
    if (stringComplete)
    {
      stringComplete = false;
      xSemaphoreTake(mutex, portMAX_DELAY);
      
      getCommand();
      
      if (commandString.equals("1"))
      {
        myDisplay.displayReset();
        myDisplay.displayText("Bat dau", PA_CENTER, 50, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
        inputString = "";
       
      }
      else if (commandString.equals("0"))
      {
        myDisplay.displayReset();
        myDisplay.displayText("Ket thuc", PA_CENTER, 50, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
        inputString = ""; 
      
      }
      else if (commandString.equals("2"))
      {
        myDisplay.displayReset();
        myDisplay.displayText(text.c_str(), PA_CENTER, 50, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
        inputString = "";
       
      }
      else if (commandString.equals("3"))
      {
        myDisplay.displayReset(0);
        inputString = "";
        getTimentp();
  
      myDisplay.begin(3);
      myDisplay.setInvert(false);

      myDisplay.setZone(0, 0, 1);
      myDisplay.setZone(1, 2, 3);
      myDisplay.setFont(1, numeric7Seg);
      myDisplay.setFont(2, numeric7Se);
      
      myDisplay.displayZoneText(1, szTime, PA_CENTER, SPEED_TIME, 0,PA_OPENING_CURSOR,PA_SCAN_HORIZX);
      myDisplay.displayZoneText(0, szMesg, PA_CENTER, SPEED_TIME, 0, PA_PRINT, PA_SCROLL_LEFT);


      myDisplay.addChar('$', degC);
      
      getDate(szMesg);
      getTime(szTime);

      mutex = xSemaphoreCreateMutex();
      
      xTaskCreatePinnedToCore(displayTask, "DisplayTask", 8000, NULL, 1, &DisplayTask, 0);
      xTaskCreatePinnedToCore(timeTask, "TimeTask", 8000, NULL, 0, &TimeTask, 0);
      }
      
      xSemaphoreGive(mutex);
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000)); 
  }
}

void loop(void)
{
 
}
