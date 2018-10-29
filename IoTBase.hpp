#ifndef IoTBase_h
#define IoTBase_h

#include "debug.hpp"

#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <SPIFFS.h>

// workaround for bug see https://github.com/platformio/platformio-core/issues/646
// must be undef before WifiManager
#undef min
#undef max

#include <WifiManager.h>

#include <rom/rtc.h>
#include <Preferences.h>
#include <ArduinoJson.h> //https://github.com/bblanchon/ArduinoJson

#include <vector>

#define PREF_WIFI_CONFIGURED "WifiConfigured"
#define PREF_RESTART_WITH_CONFIG_PORTAL "RestartPortal"

#define NO_NUMBER_F -99999

class IoTBaseParameter {
    public:

        /** 
            Create custom parameters that can be added to the WiFiManager setup web page
            @id is used for HTTP queries and must not contain spaces nor other special characters
        */
        IoTBaseParameter(String p_id, String p_placeholder, String p_defaultValue, int p_length);
        IoTBaseParameter(const IoTBaseParameter &param);
        ~IoTBaseParameter();

        WiFiManagerParameter* getWifiManagerParameter() {
            return wifiManagerParameter;
        }

        void setWifiManagerParameter(WiFiManagerParameter *wifiParam) {
            wifiManagerParameter = wifiParam;
        }

        struct cmp_str
        {
            bool operator()(const String &a, const String &b) const
            {
                return strcmp(a.c_str(), b.c_str()) < 0;
            }
        };

        const String& getId() const
        {
            return id;
        }

        String id;
        String placeholder;
        String defaultValue;
        int    length;

    private:
        WiFiManagerParameter *wifiManagerParameter;
};

class IoTBase {
    public: 

        Preferences preferences;

        explicit IoTBase();

        ~IoTBase() = default;

        void readConfiguration();
        bool begin();
        void loop();

        void restartWithConfigurationPortal();

        void setLoadConfigCallback(void (*func)(JsonObject&));
        void setSaveConfigCallback(void (*func)(JsonObject&));
        void setUpdateConfigCallback(void (*func)(JsonObject&));

        // FIXME: add customHtml parameter
        void addParameter(String id, String placeholder, String defaultValue, int length);
        
        // helper methods for json parsing:
        float parseJson(char* jsonString, char *jsonPath);

        boolean isSummerTime();

        uint8_t getWifiQuality();
        bool isWifiConnected();

    private:
        
        static void _saveWifiManagerConfigCallback();
        
        void checkResetReason();
        void writeConfiguration();
        void updateConfigurationFromWifiManager();

        void (*_loadConfigCallback)(JsonObject&) = NULL;
        void (*_saveConfigCallback)(JsonObject&) = NULL;

        String _getResetReason(RESET_REASON);

        std::vector<IoTBaseParameter> _parameters;

        uint8_t _wifiQualityMeasurements[10];
        uint8_t _wifiQualityMeasurementsIndex = 0;
        void _recordWifiQuality();

};

#endif // IoTBase_h