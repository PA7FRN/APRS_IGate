#include "EEPROM.h"
#include <SPI.h>
#include "WiFi.h"
#include "ESPAsyncWebServer.h"
#include "Wire.h"
#include "SSD1306.h"
#include "kissHost.hpp"
#include "ax25.hpp"

#define offsetEEPROM 0x0    //offset config
#define Modem_RX 22
#define Modem_TX 23
#define OLED_SCL	15			// GPIO15
#define OLED_SDA	4			// GPIO4
#define OLED_RST	16
#define TX_LED 27
#define TX_LED_DELAY 500 // milliseconds
#define OLED_ADR	0x3C		// Default 0x3C for 0.9", for 1.3" it is 0x78
#define wdtTimeout 30   //time in seconds to trigger the watchdog
#define VERSION "Arduino_RAZ_IGATE_TCP"
#define hasLCD
#define SQ_MIN 0
#define SQ_MAX 8
#define SETTINGS_COUNT 13

char receivedString[28];

uint32_t oledSleepTime = 0;
uint32_t txLedDelayTime = 0;
uint32_t lastClientUpdate = 0;

AsyncWebServer webServer(80);
WiFiClient client;
HardwareSerial Modem(1);
KissHost kissHost(1);
AX25 ax25(1);

hw_timer_t *timer = NULL;

void IRAM_ATTR resetModule() {
	ets_printf("WDT Reboot\n");
	esp_restart();
}

struct StoreStruct {
  byte chkDigit;
  char wifiSSID[25];
  char pass[64];
  char callSign[10];
  int modemChannel;
  int squelch;
  int oledTimeout;
  int updateInterval;
  char passCode[6];
  char symbol[3];
  char latitude[9];
  char longitude[10];
  char PHG[8];
  char beaconText[37];
  char AprsIsAddress[25];
  int APRSPort;
};

StoreStruct storage = {
  '$',
  "YourSSID",
  "WiFiPassword",
  "PI4RAZ-11",
  64, //12.5 KHz steps, 64 = 144.800
  0,
  20,
  600,
  "99999",
  "/I",
  "5204.44N",
  "00430.24E",
  "PHG3210",
  "ESP32 RAZ IGATE /A=000012",
  "rotate.aprs.net",
  14580
};

struct SettingStruct {
  const char* type;
  const char* name;
  const char* text;
  const char* comment;
  int min;
  int max;
  void* dataPointer;
};

SettingStruct setting[SETTINGS_COUNT] = {
  {"text"  , "Callsign"      , "Callsign (+SSID)", ""                            ,   0,     9, &(storage.callSign)      },
  {"number", "ModemChannel"  , "Modem channel"   , "12.5 KHz steps, 64 = 144.800",   0,   160, &(storage.modemChannel)  },
  {"number", "squelch"       , "squelch"         , "0 (open) is recommended"     ,   0,     8, &(storage.squelch)       },
  {"number", "OledTimeout"   , "Oled timeout"    , "seconds"                     ,   1,    60, &(storage.oledTimeout)   },
  {"number", "UpdateInterval", "Update interval" , "seconds"                     , 300,  7200, &(storage.updateInterval)},
  {"text"  , "Passcode"      , "Passcode"        , "<a href=https://apps.magicbug.co.uk/passcode/>get passcode</a>", 0, 5, &(storage.passCode)},
  {"text"  , "Symbol"        , "Symbol"          , "<a href=http://www.aprs.org/symbols.html>info</a>"             , 0, 2, &(storage.symbol)  },
  {"text"  , "Latitude"      , "Latitude"        , ""                            ,   0,     8, &(storage.latitude)      },
  {"text"  , "Longitude"     , "Longitude"       , ""                            ,   0,     9, &(storage.longitude)     },
  {"text"  , "PHG"           , "PHG"             , ""                            ,   0,     7, &(storage.PHG)           },
  {"text"  , "beaconText"    , "beacon text"     , ""                            ,   0,    36, &(storage.beaconText)    },
  {"text"  , "APRSISaddress" , "APRS-IS address" , ""                            ,   0,    24, &(storage.AprsIsAddress) },
  {"number", "APRSport"      , "APRS port"       , ""                            ,   0, 99999, &(storage.APRSPort)      }
};

#ifdef hasLCD
SSD1306  lcd(OLED_ADR, OLED_SDA, OLED_SCL);// i2c ADDR & SDA, SCL on wemos
#endif

//const char *ap_ssid = "RazIGate";
//const char *ap_password = "aprsigate";

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP Web Server</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
  <style>
    html {font-family: Arial; display: inline-block; text-align: left;}
    h2 {font-size: 3.0rem;}
    p {font-size: 3.0rem;}
    body {max-width: 600px; margin:0px auto; padding-bottom: 25px;}
  </style>
</head>
<body>
  <h2>ESP32 RAZ IGATE</h2>
  %SETTINGPLACEHOLDER%
</body>
</html>
)rawliteral";

String processor(const String& var){
  if (var == "SETTINGPLACEHOLDER") {
    String settings = "";
    settings += "<form action=\"/update\"><table>";
    for (int i=0; i<SETTINGS_COUNT; i++) {
      settings += "<tr><td style=\"text-align:right\">";
      settings.concat(setting[i].text);
      settings += "</td><td><input type=\"";
      settings.concat(setting[i].type);
      settings += "\" name=\"";
      settings.concat(setting[i].name);
      if (setting[i].type == "number") {
        settings += "\" min=\"";
        settings.concat(setting[i].min);
        settings += "\" max=\"";
        settings.concat(setting[i].max);
        settings += "\" value=\"";
        int* intPointer = (int *)(setting[i].dataPointer);
        settings += String(*intPointer);
      }
      else {
        settings += "\"  maxlength=\"";
        settings.concat(setting[i].max);
        settings += "\" value=\"";
        settings.concat((char *)(setting[i].dataPointer));
      }
      settings += "\"></td><td>";
      settings.concat(setting[i].comment);
      settings += "</td></tr>";
    }
    settings += "</table><input type=\"submit\" value=\"Set\"></form>";
    return settings;
  }
  return String();
}

void setSettingVal(int i, String strSetting) {
  if (setting[i].type == "number") {
    int* intPointer = (int *)(setting[i].dataPointer);
    *intPointer = strSetting.toInt();
  }
  else {
    char* chrPointer = (char *)(setting[i].dataPointer);
    strSetting.toCharArray(chrPointer, setting[i].max+1);
  }
}

void setup() {
	pinMode(OLED_RST,OUTPUT);
	digitalWrite(OLED_RST, LOW); // low to reset OLED
	pinMode(TX_LED,OUTPUT);
	digitalWrite(TX_LED, LOW); // low to reset OLED
	delay(50);
	digitalWrite(OLED_RST, HIGH); // must be high to turn on OLED
	delay(50);
	Wire.begin(4, 15);

	#ifdef hasLCD
	lcd.init();
	lcd.flipScreenVertically();
	lcd.setFont(ArialMT_Plain_10);
	lcd.setTextAlignment(TEXT_ALIGN_LEFT);
	lcd.drawString(0, 0, "APRS IGate");
	lcd.drawString(0, 8, "STARTING");
	lcd.display();
	lcd.setFont(ArialMT_Plain_10);
	#endif
	delay(1000);

	Serial.begin(9600);
	Serial.println("AIRGATE - PA2RDK");
	Modem.begin(9600, SERIAL_8N1, Modem_RX, Modem_TX);
	Modem.setTimeout(2);

	if (!EEPROM.begin(sizeof(storage) /*EEPROM_SIZE*/))
	{
		Serial.println("failed to initialise EEPROM"); while(1);
	}
  if (EEPROM.read(offsetEEPROM) != storage.chkDigit){
		Serial.println(F("Writing defaults"));
		saveConfig();
  }
	loadConfig();
	printConfig();

	#ifdef hasLCD
	lcd.drawString(0, 16, "Wait for setup");
	lcd.display();
	#endif

	Serial.println(F("Type GS to enter Wifi setup:"));
  uint32_t setupWaitTime=millis();
  char gs1, gs2;
  while (millis()-setupWaitTime < 10000) {
    if (Serial.available()) {
      gs1 = gs2;
      gs2 = Serial.read();
      if (((gs1 == 'G') || (gs1 == 'g')) &&
          ((gs2 == 'S') || (gs2 == 's'))) {
        Serial.println(F("Setup entered..."));
        #ifdef hasLCD
        lcd.clear();
        lcd.drawString(0, 0,"Setup entered");
        lcd.display();
        #endif
        setWifiSettings();
        delay(2000);
      }
    }
  }

/*WiFi.softAP(ap_ssid, ap_password);
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP()); */
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });

  webServer.on("/update", HTTP_GET, [] (AsyncWebServerRequest *request) {
    bool doSave = false;
    for (int i=0; i<SETTINGS_COUNT; i++) {
      if (request->hasParam(setting[i].name)) {
        doSave = true;
        setSettingVal(i, request->getParam(setting[i].name)->value());
      }
    }
    if (doSave) {
      saveConfig();
      showSettings();
    }
    request->send_P(200, "text/html", index_html, processor);
  });
  InitConnection();
  webServer.begin();

	delay(1000);
	setDra(storage.modemChannel, storage.squelch);
	Modem.println(F("AT+DMOSETVOLUME=8"));
	Modem.println(F("AT+DMOSETMIC=8,0"));
	Modem.println(F("AT+SETFILTER=1,1,1"));
	Serial.print(F("DRA Initialized on freq:"));
	Serial.println(float(144000+(storage.modemChannel*12.5))/1000);

  #ifdef hasLCD
  lcd.drawString(0, 24, "Modem set at "+String(float(144000+(storage.modemChannel*12.5))/1000)+ "MHz" );
  lcd.display();
  oledSleepTime=millis();
  #endif

	timer = timerBegin(0, 80, true);                  //timer 0, div 80
	timerAttachInterrupt(timer, &resetModule, true);  //attach callback
	timerAlarmWrite(timer, wdtTimeout * 1000 * 1000, false); //set time in us
	timerAlarmEnable(timer);                          //enable interrupt

	delay(3000);
}

void loop() {
  timerWrite(timer, 0);

  #ifdef hasLCD
  if (millis()-oledSleepTime>storage.oledTimeout*1000) {
    lcd.clear();
    lcd.display();
    oledSleepTime=millis();
  }
  #endif

  if (millis()-txLedDelayTime>TX_LED_DELAY) {
    digitalWrite(TX_LED, LOW);
  }
  
  int buflen = 0;
  if (Modem.available() > 0) {
    buflen = kissHost.processKissInByte(Modem.read());
  }

  if (check_connection()){
    if (buflen>0) {
      bool drop;
      if (ax25.parseForIS(kissHost.packet, buflen, &drop)) {
        if (drop) {
          drop_packet();
        }
        else {
          digitalWrite(TX_LED, HIGH);
          txLedDelayTime=millis();
          send_packet();
        }
      }
    }

    if (millis()-lastClientUpdate>storage.updateInterval*1000) {
      updateGatewayonAPRS();
    }
    
    //  If connected to APRS-IS, read any response from APRS-IS and display it.
    //  Buffer 80 characters at a time in case printing a character at a time is slow.
    receive_data();
    int b = 0;
    while (Serial.available() > 0) {
      b = Serial.read();
      Modem.write(b);
    }
  }
}

// See http://www.aprs-is.net/Connecting.aspx
boolean check_connection() {
	if (WiFi.status() !=WL_CONNECTED || !client.connected()) {
		InitConnection();
	}
	return client.connected();
}

void receive_data() {
	if (client.available()) {
		char rbuf[81];
		int i = 0;
		while (i < 80 && client.available()) {
			char c = client.read();
			rbuf[i++] = c;
			if (c == '\n') break;
		}
    rbuf[i] = 0;
 // Serial.println(rbuf);
  }
}

void drop_packet() {
  Serial.print("d ");
  Serial.println(ax25.isPacket);
  #ifdef hasLCD
  lcd.clear();
  lcd.drawString(0, 0, "Drop packet");
  lcd.drawStringMaxWidth(0,16, 200, ax25.isPacket);
  lcd.display();
  oledSleepTime=millis();
  #endif
}

void send_packet() {
  Serial.print("R ");
  Serial.println(ax25.isPacket);
  #ifdef hasLCD
  lcd.clear();
  lcd.drawString(0, 0, "Send packet");
  lcd.drawStringMaxWidth(0,16, 200, ax25.isPacket);
  lcd.display();
  oledSleepTime=millis();
  #endif
  client.println(ax25.isPacket);
  client.println();
}

void InitConnection() {
	Serial.println("++++++++++++++");
	Serial.println("Initialize connection");

  #ifdef hasLCD
  lcd.clear();
  lcd.drawString(0, 0, "Connecting to WiFi" );
  lcd.display();
  oledSleepTime=millis();
  #endif

	if (WiFi.status() != WL_CONNECTED){
		Serial.println("Connecting to WiFi");
		WlanReset();
		WiFi.begin(storage.wifiSSID, storage.pass);
		int agains=1;
		while ((WiFi.status() != WL_CONNECTED) && (agains < 20)){
			Serial.print(".");
			delay(1000);
			agains++;
		}
		//WlanStatus();
	}

	if (WlanStatus()==WL_CONNECTED){
    #ifdef hasLCD
    lcd.drawString(0, 8, "Connected to:");
    lcd.drawString(70,8, storage.wifiSSID);
    lcd.display();
    oledSleepTime=millis();
    #endif
    Serial.println("++++++++++++++");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

		if (!client.connected()) {
			Serial.println("Connecting...");
			if (client.connect(storage.AprsIsAddress, storage.APRSPort)) {
				// Log in

				client.print("user ");
				client.print(storage.callSign);
				client.print(" pass ");
				client.print(storage.passCode);
				client.println(" vers " VERSION);

				updateGatewayonAPRS();

				Serial.println("Connected");

        #ifdef hasLCD
        lcd.drawString(0, 16, "Con. APRS-IS:");
        lcd.drawString(70,16, storage.callSign);
        lcd.display();
        oledSleepTime=millis();
        #endif
      }
      else {
        Serial.println("Failed");

        #ifdef hasLCD
        lcd.drawString(0, 16, "Conn. to APRS-IS Failed" );
        lcd.display();
        oledSleepTime=millis();
        #endif
        // if still not connected, delay to prevent constant attempts.
        delay(1000);
      }
		}
	}
  oledSleepTime=millis();
}

void updateGatewayonAPRS(){
  #ifdef hasLCD
  lcd.clear();
  lcd.drawString(0, 0, "Update IGate info");
  lcd.display();
  oledSleepTime=millis();
  #endif
  if (client.connected()){
    Serial.println("Update IGate info on APRS");
    client.print(storage.callSign);
    client.print(">APRAZ1,TCPIP*:!");
    client.print(storage.latitude);
    client.print(storage.symbol[0]);
    client.print(storage.longitude);
    client.print(storage.symbol[1]);
    client.print(storage.PHG);
    client.println(storage.beaconText);
    lastClientUpdate = millis();
  };
}

void WlanReset() {
	WiFi.persistent(false);
	WiFi.disconnect();
	WiFi.mode(WIFI_OFF);
	WiFi.mode(WIFI_STA);
	delay(1000);
}

int WlanStatus() {
	switch (WiFi.status()) {
	case WL_CONNECTED:
		Serial.print(F("WlanCStatus:: CONNECTED to:"));				// 3
		Serial.println(WiFi.SSID());
		WiFi.setAutoReconnect(true);				// Reconenct to this AP if DISCONNECTED
		return(3);
		break;

		// In case we get disconnected from the AP we loose the IP address.
		// The ESP is configured to reconnect to the last router in memory.
	case WL_DISCONNECTED:
		Serial.print(F("WlanStatus:: DISCONNECTED, IP="));			// 6
		Serial.println(WiFi.localIP());
		return(6);
		break;

		// When still pocessing
	case WL_IDLE_STATUS:
		Serial.println(F("WlanStatus:: IDLE"));					// 0
		return(0);
		break;

		// This code is generated as soonas the AP is out of range
		// Whene detected, the program will search for a better AP in range
	case WL_NO_SSID_AVAIL:
		Serial.println(F("WlanStatus:: NO SSID"));					// 1
		return(1);
		break;

	case WL_CONNECT_FAILED:
		Serial.println(F("WlanStatus:: FAILED"));					// 4
		return(4);
		break;

		// Never seen this code
	case WL_SCAN_COMPLETED:
		Serial.println(F("WlanStatus:: SCAN COMPLETE"));			// 2
		return(2);
		break;

		// Never seen this code
	case WL_CONNECTION_LOST:
		Serial.println(F("WlanStatus:: LOST"));					// 5
		return(5);
		break;

		// This code is generated for example when WiFi.begin() has not been called
		// before accessing WiFi functions
	case WL_NO_SHIELD:
		Serial.println(F("WlanStatus:: WL_NO_SHIELD"));				//255
		return(255);
		break;

	default:
		break;
	}
	return(-1);
}

void setDra(byte freq, byte squelch) {
  char buff[50];

  int frMhz = int(freq/80)+4;
  int frkHzPart;
  if (freq>79) {
    frkHzPart = freq-80;
  }
  else {
    frkHzPart=freq;
  }
  frkHzPart *=125; 

  sprintf(
    buff, "AT+DMOSETGROUP=0,14%01d.%04d,14%01d.%04d,0000,%1d,0000", 
    frMhz, frkHzPart, frMhz, frkHzPart, squelch
  );
  Serial.println();
  Serial.println(buff);
  Modem.println(buff);
}

void showSettings() {
	Serial.print(F("Wifi SSID : "));
	Serial.println(storage.wifiSSID);

	Serial.print(F("WiFi password : "));
	Serial.println(storage.pass);

  for (int i=0; i<SETTINGS_COUNT; i++) {
    Serial.print(setting[i].text);
    Serial.print(" : ");
    if (setting[i].type == "number") {
      int* intPointer = (int *)(setting[i].dataPointer);
      Serial.println(String(*intPointer));
    }
    else {
      Serial.println((char *)(setting[i].dataPointer));
    }
  }
}

void setWifiSettings() {
  receivedString[0] = 'X';

  Serial.print(F("Wifi SSID ("));
  Serial.print(storage.wifiSSID);
  Serial.print(F("):"));
  getStringValue(24);
  if (receivedString[0] != 0) {
    storage.wifiSSID[0] = 0;
    strcat(storage.wifiSSID, receivedString);
  }
  Serial.println();

  Serial.print(F("WiFi password ("));
  Serial.print(storage.pass);
  Serial.print(F("):"));
  getStringValue(63);
  if (receivedString[0] != 0) {
    storage.pass[0] = 0;
    strcat(storage.pass, receivedString);
  }
  Serial.println();

  saveConfig();
//  loadConfig();
}

void getStringValue(int length) {
	SerialFlush();
	receivedString[0] = 0;
	int i = 0;
	while (receivedString[i] != 13 && i < length) {
		if (Serial.available() > 0) {
			receivedString[i] = Serial.read();
			if (receivedString[i] == 13 || receivedString[i] == 10) {
				i--;
			}
			else {
				Serial.write(receivedString[i]);
			}
			i++;
		}
	}
	receivedString[i] = 0;
	SerialFlush();
}

void saveConfig() {
  for (unsigned int t = 0; t < sizeof(storage); t++)
    EEPROM.write(offsetEEPROM + t, *((char*)&storage + t));
  EEPROM.commit();
}

void loadConfig() {
  if (EEPROM.read(offsetEEPROM + 0) == storage.chkDigit)
    for (unsigned int t = 0; t < sizeof(storage); t++)
      *((char*)&storage + t) = EEPROM.read(offsetEEPROM + t);
}

void printConfig() {
  if (EEPROM.read(offsetEEPROM + 0) == storage.chkDigit)
    for (unsigned int t = 0; t < sizeof(storage); t++)
      Serial.write(EEPROM.read(offsetEEPROM + t));

  Serial.println();
  showSettings();
}

void SerialFlush() {
  for (int i = 0; i < 10; i++)
  {
    while (Serial.available() > 0) {
      Serial.read();
    }
  }
}
