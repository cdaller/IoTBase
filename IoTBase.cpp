/* 
 * IoTBase library written by Christof Dallermassl
 * for easier development of IoT hardware based on ESP32 or ESP8266 hardware
 */

#define DEBUG 1
#define TIME_ZONE 1
#define TIME_ZONE_MINUTES 0

#include "IoTBase.hpp"

#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <TimeLib.h>
#include <NtpClientLib.h>


// global variable, cannot use class variable :-(
bool shouldSaveWifiManagerConfig = false;

IoTBase::IoTBase() {
}

/* if there was a configuration saved in SPIFFS, load it and call callback */
void IoTBase::readConfiguration() {
    //clean FS, for testing
    // SPIFFS.format();

    //read configuration from FS json
    DEBUG_PRINTLN("mounting FS...");

    // open SPIFFS, format if failure
    if (SPIFFS.begin(true)) {
        DEBUG_PRINTLN("mounted file system");
        if (SPIFFS.exists("/config.json")) {
            //file exists, reading and loading
            DEBUG_PRINTLN("reading config file");
            File configFile = SPIFFS.open("/config.json", "r");
            if (configFile) {
                DEBUG_PRINTLN("opened config file");
                size_t size = configFile.size();
                // Allocate a buffer to store contents of the file.
                std::unique_ptr<char[]> buf(new char[size]);

                configFile.readBytes(buf.get(), size);
                DynamicJsonBuffer jsonBuffer;
                JsonObject& json = jsonBuffer.parseObject(buf.get());
                json.printTo(Serial);
                if (json.success()) {
                    DEBUG_PRINTLN("\nparsed json");

                    if (_loadConfigCallback != NULL) {
                        _loadConfigCallback(json);
                    }
                } else {
                    Serial.println("failed to load json config");
                }
                configFile.close();
            }
        }
    } else {
        Serial.println("failed to mount FS");
    }
    // end read
};

/* call callback and save configuration to SPIFFS */
void IoTBase::writeConfiguration() {
    //save the custom parameters to FS
    if (shouldSaveWifiManagerConfig) {
        DEBUG_PRINTLN("saving config");
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.createObject();
        if (_saveConfigCallback != NULL) {
            _saveConfigCallback(json);
        }

        File configFile = SPIFFS.open("/config.json", "w");
        if (!configFile) {
            Serial.println("failed to open config file for writing");
        }

        #ifdef DEBUG
            json.printTo(Serial);
            Serial.println();
        #endif
        json.printTo(configFile);
        configFile.close();
        //end save
    }
};

/* update parameters from WifiManagerParameters */
void IoTBase::updateConfigurationFromWifiManager() {
    // read updated parameters and write them to the variables via callback:
    if (_loadConfigCallback != NULL) {
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.createObject();
        for(auto &param : _parameters) {
            json[param.id] = param.getWifiManagerParameter()->getValue();
            // memory is freed by wifimanager, so remove pointer to WifiManagerParameter
            param.setWifiManagerParameter(NULL); 
        }
        DEBUG_PRINTLN("creating json from GUI:");
        json.printTo(Serial);

        _loadConfigCallback(json);
    }
};

bool IoTBase::begin() {

    // not working reliable, so disable it at the moment: 
    //checkResetReason();


    // WiFiManager, Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    // forward parameters to WifiManager
    for(auto &param : _parameters) {
        DEBUG_PRINTF2("Adding parameter to wifimanger: %s=%s\n", param.id.c_str(), param.defaultValue.c_str());
        WiFiManagerParameter *customParam = new WiFiManagerParameter(param.id.c_str(), param.placeholder.c_str(), param.defaultValue.c_str(), param.length);
        param.setWifiManagerParameter(customParam); // save to retrieve value later
        wifiManager.addParameter(customParam);    
    }

    // set config save notify callback
    wifiManager.setSaveConfigCallback(_saveWifiManagerConfigCallback);

	// preferences.begin("iotbase", true);
    // DEBUG_PRINTF("WIFI Configured: %s", preferences.getBool(PREF_WIFI_CONFIGURED) ? "true" : "false");
    // if (!preferences.getBool(PREF_WIFI_CONFIGURED)) {
    //     Serial.println("wifi configuration will be reset");
    // reset settings - for testing
    //     wifiManager.resetSettings();
    // }

    // need to start with configuration portal or just try to connect to wifi?
    preferences.begin("iotbase", false);
    if (preferences.getBool(PREF_RESTART_WITH_CONFIG_PORTAL, false)) {
        DEBUG_PRINTLN(F("starting configuration portal mode"));

        preferences.putBool(PREF_RESTART_WITH_CONFIG_PORTAL, false);
        preferences.end();

        wifiManager.startConfigPortal();
    } else {
        preferences.end();
        DEBUG_PRINTLN(F("starting autoconnect mode"));
        wifiManager.autoConnect();
    }

    //if you get here you have connected to the WiFi
    DEBUG_PRINTLN("connected...yeey :)");
    
    updateConfigurationFromWifiManager();

    writeConfiguration();

    Serial.print("local ip: ");
    DEBUG_PRINTLN(WiFi.localIP());

    NTP.begin ("europe.pool.ntp.org", TIME_ZONE, true, TIME_ZONE_MINUTES);

    #ifdef DEBUG
        Serial.println("DEBUG is on in IoTBase");
    #else
        Serial.println("DEBUG is off in IoTBase");
    #endif

    DEBUG_PRINTLN("Debug is on with DEBUG_PRINTLN");

    return true;
}

/**
 * Invoke loop in the loop method of main application to allow IoTBase
 * to do some things on a regular basis. 
 */
void IoTBase::loop() {
    _recordWifiQuality();
}

/** 
 * take the last 10 Wifi quality values to get a stable average
 */
void IoTBase::_recordWifiQuality() {
    if (WiFi.status() == WL_CONNECTED) {
        long dBm = WiFi.RSSI(); // values between -50 (good) and -100 (bad)
        long quality = (uint8_t) 2 * (dBm + 100);
        //DEBUG_PRINTF2("Wifi rssi=%ld, quality=%ld\n", dBm, quality);

        _wifiQualityMeasurements[_wifiQualityMeasurementsIndex++] = quality;
        if (_wifiQualityMeasurementsIndex >= 10) {
            _wifiQualityMeasurementsIndex = 0;
        }
    }
}

uint8_t IoTBase::getWifiQuality() {
    // calculate average of last 10 measurements:
    uint16_t sum = 0;
    for (uint8_t index = 0; index < 10; index++) {
        sum += _wifiQualityMeasurements[index];
    }
    return (uint8_t) (sum / 10);
}

bool IoTBase::isWifiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void IoTBase::addParameter(String id, String placeholder, String defaultValue, int length) {
  	_parameters.emplace_back(id, std::move(placeholder), std::move(defaultValue), length);
}


// This function checks the reset reason returned by the ESP and resets the configuration if neccessary.
// It counts all system reboots that occured by power cycles or button resets.
// If the ESP32 receives an IP the boot counts as successful and the counter will be reset by Basecamps
// WiFi management.
void IoTBase::checkResetReason() {
    // Instead of the internal flash it uses the somewhat limited, but sufficient preferences storage
    preferences.begin("iotbase", false);
    // Get the reset reason for the current boot
    RESET_REASON reason = rtc_get_reset_reason(0);
    DEBUG_PRINT("Reset reason: ");
    DEBUG_PRINTLN(_getResetReason(reason));
    // If the reason is caused by a power cycle (1) or a RTC reset / button press(16) evaluate the current
    // bootcount and act accordingly.
    if (reason == 1 || reason == 16) {
        // Get the current number of unsuccessful boots stored
        unsigned int bootCounter = preferences.getUInt("bootcounter", 0);
        // increment it
        bootCounter++;
        DEBUG_PRINT("Unsuccessful boots: ");
        DEBUG_PRINTLN(bootCounter);

        // If the counter is bigger than 3 it will be the fifths consecutive unsucessful reboot.
        // This forces a reset of the WiFi configuration and the AP will be opened again
        if (bootCounter > 3){
          Serial.println("Configuration forcibly reset.");
          // Mark the WiFi configuration as invalid
          // configuration.set("WifiConfigured", "False");
          // Save the configuration immediately
          //configuration.save();
          preferences.putBool(PREF_WIFI_CONFIGURED, false);
          // Reset the boot counter
          preferences.putUInt("bootcounter", 0);
          // Call the destructor for preferences so that all data is safely stored befor rebooting
          preferences.end();
          Serial.println("Resetting the WiFi configuration.");
          // Reboot
          ESP.restart();

          // If the WiFi is not configured and the device is rebooted twice format the internal flash storage
      } else if (bootCounter > 2 && !preferences.getBool(PREF_WIFI_CONFIGURED)) {
          Serial.println("Factory reset was forced.");
          // Format the flash storage
          SPIFFS.format();
          // Reset the boot counter
          preferences.putUInt("bootcounter", 0);
          // Call the destructor for preferences so that all data is safely stored befor rebooting
          preferences.end();
          Serial.println("Rebooting.");
          // Reboot
          ESP.restart();

      // In every other case: store the current boot count
      } else {
          preferences.putUInt("bootcounter", bootCounter);
      };

    // if the reset has been for any other cause, reset the counter
    } else {
        preferences.putUInt("bootcounter", 0);
    };
    // Call the destructor for preferences so that all data is safely stored
    preferences.end();
};

// load configuration (file or GUI) into variables
void IoTBase::setLoadConfigCallback(void (*func)(JsonObject&)) {
    _loadConfigCallback = func;
};

// save variables into configuration
void IoTBase::setSaveConfigCallback(void (*func)(JsonObject&)) {
    _saveConfigCallback = func;
};

void IoTBase::restartWithConfigurationPortal() {
    preferences.begin("iotbase", false);
    preferences.putBool(PREF_RESTART_WITH_CONFIG_PORTAL, true);
    preferences.end();
    ESP.restart();
};

//callback notifying us of the need to save config
void IoTBase::_saveWifiManagerConfigCallback() {
    DEBUG_PRINTLN("Should save config");
    shouldSaveWifiManagerConfig = true;
};

boolean IoTBase::isSummerTime() {
    return NTP.isSummerTime();
}

String IoTBase::_getResetReason(RESET_REASON reason)
{
  switch ( reason)
  {
    case 1 : return "POWERON_RESET (1)";          /**<1, Vbat power on reset*/
    case 3 : return "SW_RESET (3)";               /**<3, Software reset digital core*/
    case 4 : return "OWDT_RESET (4)";             /**<4, Legacy watch dog reset digital core*/
    case 5 : return "DEEPSLEEP_RESET (5)";        /**<5, Deep Sleep reset digital core*/
    case 6 : return "SDIO_RESET (6)";             /**<6, Reset by SLC module, reset digital core*/
    case 7 : return "TG0WDT_SYS_RESET (7)";       /**<7, Timer Group0 Watch dog reset digital core*/
    case 8 : return "TG1WDT_SYS_RESET (8)";       /**<8, Timer Group1 Watch dog reset digital core*/
    case 9 : return "RTCWDT_SYS_RESET (9)";       /**<9, RTC Watch dog Reset digital core*/
    case 10 : return "INTRUSION_RESET (10)";       /**<10, Instrusion tested to reset CPU*/
    case 11 : return "TGWDT_CPU_RESET (11)";       /**<11, Time Group reset CPU*/
    case 12 : return "SW_CPU_RESET (12)";          /**<12, Software reset CPU*/
    case 13 : return "RTCWDT_CPU_RESET (13)";      /**<13, RTC Watch dog Reset CPU*/
    case 14 : return "EXT_CPU_RESET (14)";         /**<14, for APP CPU, reseted by PRO CPU*/
    case 15 : return "RTCWDT_BROWN_OUT_RESET (15)";/**<15, Reset when the vdd voltage is not stable*/
    case 16 : return "RTCWDT_RTC_RESET (16)";      /**<16, RTC Watch dog reset digital core and rtc module*/
    default : return "NO_MEAN";
  }
}

// parse jsonPaths like $.foo[1].bar.baz[2][3].value equals to foo[1].bar.baz[2][3].value
float IoTBase::parseJson(char* jsonString, char *jsonPath) {
    float jsonValue;
    DynamicJsonBuffer jsonBuffer;
    
    JsonVariant root = jsonBuffer.parse(jsonString);
    JsonVariant element = root;

    if (root.success()) {
        // parse jsonPath and navigate through json object:
        char pathElement[40];
        int pathIndex = 0;

        DEBUG_PRINTF("parsing '%s'\n", jsonPath);
        for (int i = 0; jsonPath[i] != '\0'; i++){
            if (jsonPath[i] == '$') {
                element = root;
            } else if (jsonPath[i] == '.') {
                if (pathIndex > 0) {
                    pathElement[pathIndex++] = '\0';
                    // printf("pathElement '%s'\n", pathElement);
                    pathIndex = 0;
                    element = element[pathElement];
                    if (!element.success()) {
                        DEBUG_PRINTF("failed to parse key %s\n", pathElement);
                    }
                }
            } else if ((jsonPath[i] >= 'a' && jsonPath[i] <= 'z') 
                    || (jsonPath[i] >= 'A' && jsonPath[i] <= 'Z') 
                    || (jsonPath[i] >= '0' && jsonPath[i] <= '9')
                    || jsonPath[i] == '-' || jsonPath[i] == '_'
                    ) {
                pathElement[pathIndex++] = jsonPath[i];
            } else if (jsonPath[i] == '[') {
                if (pathIndex > 0) {
                    pathElement[pathIndex++] = '\0';
                    // printf("pathElement '%s'\n", pathElement);
                    pathIndex = 0;
                    element = element[pathElement];
                    if (!element.success()) {
                        DEBUG_PRINTF("failed in parsing key %s\n", pathElement);
                    }
                }
            } else if (jsonPath[i] == ']') {
                pathElement[pathIndex++] = '\0';
                int arrayIndex = strtod(pathElement, NULL);
                // printf("index '%s' = %d\n", pathElement, arrayIndex);
                pathIndex = 0;
                element = element[arrayIndex];
                if (!element.success()) {
                    DEBUG_PRINTF("failed in parsing index %d\n", arrayIndex);
                }
            }
        }  
        // final token if any:
        if (pathIndex > 0) {
            pathElement[pathIndex++] = '\0';
            // printf("pathElement '%s'\n", pathElement);
            pathIndex = 0;
            element = element[pathElement];
            if (!element.success()) {
                DEBUG_PRINTF("failed in parsing key %s\n", pathElement);
            }
        }

        jsonValue = element.as<float>();

        //jsonValue = measurements[1]["sensordatavalues"][0]["value"];
        DEBUG_PRINTF("success reading value: %f\n", jsonValue);
    } else {
        jsonValue = NO_NUMBER_F;
        DEBUG_PRINTLN("could not parse json for value");
    }
    return jsonValue;
}

IoTBaseParameter::IoTBaseParameter(String p_id, String p_placeholder, String p_defaultValue, int p_length) {
    id = std::move(p_id);
    placeholder = std::move(p_placeholder);
    defaultValue = std::move(p_defaultValue);
    length = p_length;
}

// copy constructor
IoTBaseParameter::IoTBaseParameter(const IoTBaseParameter &param)
    : id(param.id),
      placeholder(param.placeholder),
      defaultValue(param.defaultValue),
      length(param.length) {
}

IoTBaseParameter::~IoTBaseParameter() {
}

