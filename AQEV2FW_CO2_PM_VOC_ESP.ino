#include <Wire.h>
#include <SPI.h>
#include <ESP8266_AT_Client.h>
#include <SdFat.h>
#include <RTClib.h>
#include <RTC_DS3231.h>
#include <Time.h>
#include <TimeLib.h>
#include <TinyWatchdog.h>
#include <SHT25.h>
#include <WildFire_SPIFlash.h>
#include <CapacitiveSensor.h>
#include <LiquidCrystal.h>
#include <PubSubClient.h>
#include <util/crc16.h>
#include <math.h>
#include <TinyGPS.h>
#include <SoftwareSerial.h>
#include <jsmn.h>
#include <SoftReset.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>

#define INCLUDE_FIRMWARE_INTEGRITY_SELF_CHECK

#include <PMSX003.h>
#include <K30.h>
#include <AMS_IAQ_CORE_C.h>

// semantic versioning - see http://semver.org/
#define AQEV2FW_MAJOR_VERSION 2
#define AQEV2FW_MINOR_VERSION 3
#define AQEV2FW_PATCH_VERSION 7

#define WLAN_SEC_AUTO (10) // made up to support auto-config of security

// the start address of the second to last 4k page, where config is backed up off MCU
// the last page is reserved for use by the bootloader
#define SECOND_TO_LAST_4K_PAGE_ADDRESS      0x7E000

int esp8266_enable_pin = 23; // Arduino digital the pin that is used to reset/enable the ESP8266 module

Stream * at_command_interface = &Serial1;  // Serial1 is the 'stream' the AT command interface is on
Stream * at_debug_interface = &Serial;
ESP8266_AT_Client esp(esp8266_enable_pin, at_command_interface); // instantiate the client object

TinyWatchdog tinywdt;
SHT25 sht25;
WildFire_SPIFlash flash;
CapacitiveSensor touch = CapacitiveSensor(A0, A1);
LiquidCrystal lcd(A3, A2, 4, 5, 6, 8);
char g_lcd_buffer[2][17] = {0}; // 2 rows of 16 characters each, with space for NULL terminator
char last_painted[2][17] = {"                ","                "};

byte mqtt_server_ip[4] = { 0 };
PubSubClient mqtt_client;
char mqtt_client_id[32] = {0};

boolean wifi_can_connect = false;
uint8_t wifi_connect_attempts = 0;
boolean user_location_override = false;
boolean gps_installed = false;
boolean display_offline_mode_banner = false;

RTC_DS3231 rtc;
SdFat SD;

#define MAX_SAMPLE_BUFFER_DEPTH  (60) // 5 minutes @ 5 second resolution
#define CO2_EQUIVALENT_SAMPLE_BUFFER (0)
#define TVOC_SAMPLE_BUFFER           (1)
#define RESISTANCE_SAMPLE_BUFFER     (2)
#define CO2_SAMPLE_BUFFER            (3)
#define A_PM1P0_SAMPLE_BUFFER        (4)
#define A_PM2P5_SAMPLE_BUFFER        (5)
#define A_PM10P0_SAMPLE_BUFFER       (6)
#define B_PM1P0_SAMPLE_BUFFER        (7)
#define B_PM2P5_SAMPLE_BUFFER        (8)
#define B_PM10P0_SAMPLE_BUFFER       (9)
#define NUM_SAMPLE_BUFFERS           (10)
SoftwareSerial pmsx003Serial_2(A7, A5);  // RX, TX
SoftwareSerial pmsx003Serial_1(A4, A5);  // RX, TX
PMSX003 pmsx003_1(&pmsx003Serial_1);
PMSX003 pmsx003_2(&pmsx003Serial_2);
float pm1p0_ugpm3 = 0.0f;
float pm2p5_ugpm3 = 0.0f;
float pm10p0_ugpm3 = 0.0f;
float instant_pm1p0_ugpm3_a = 0.0f;
float instant_pm2p5_ugpm3_a = 0.0f;
float instant_pm10p0_ugpm3_a = 0.0f;
float instant_pm1p0_ugpm3_b = 0.0f;
float instant_pm2p5_ugpm3_b = 0.0f;
float instant_pm10p0_ugpm3_b = 0.0f;
uint8_t pm_enable_sensor_a = true;
uint8_t pm_enable_sensor_b = true;
boolean particulate_ready = false;
void set_pm1p0_offset(char * arg);
void set_pm2p5_offset(char * arg);
void set_pm10p0_offset(char * arg);
void begin_pm(char * arg);
void test_pm(char * arg);
void pmsen(char * arg);
void cmd_pm_enable(char * arg);
void cmd_pm_disable(char * arg);
const char cmd_string_pm1p0_off[] PROGMEM   = "pm1p0_off  ";
const char cmd_string_pm2p5_off[] PROGMEM   = "pm2p5_off  ";
const char cmd_string_pm10p0_off[] PROGMEM  = "pm10p0_off ";
const char cmd_string_beginpm[] PROGMEM     = "beginpm    ";
const char cmd_string_testpm[] PROGMEM      = "testpm     ";
const char cmd_string_pmsen[] PROGMEM       = "pmsen      ";
const char cmd_string_pm_enable[] PROGMEM   = "pmenable   ";
const char cmd_string_pm_disable[] PROGMEM  = "pmdisable  ";
SoftwareSerial co2Serial(9, 10);  // RX, TX
K30 k30(&co2Serial);
float co2_ppm = 0.0f;
float instant_co2_ppm = 0.0f;
boolean co2_ready = false;
void set_co2_offset(char * arg);
void co2_baseline_voltage_characterization_command(char * arg);
const char cmd_string_co2_off[] PROGMEM     = "co2_off    ";
const char cmd_string_co2_blv[] PROGMEM     = "co2_blv    ";
AMS_IAQ_CORE_C iaqcore;
boolean iaqcore_ready = false;
boolean iaqcore_failed = false;
boolean iaqcore_ok = false;
float tvoc_ppb = 0.0f;
float co2_equivalent_ppm = 0.0f;
float resistance_ohms = 0.0f;
float instant_tvoc_ppb = 0.0f;
float instant_co2_equivalent_ppm = 0.0f;
float instant_resistance_ohms = 0.0f;
char compensated_instant_value_string[64] = {0};
char converted_value_string_2[64] = {0};
char compensated_value_string_2[64] = {0};
char compensated_instant_value_string_2[64] = {0};
char raw_instant_value_string_2[64] = {0};
char converted_value_string_3[64] = {0};
char compensated_value_string_3[64] = {0};
char compensated_instant_value_string_3[64] = {0};
char raw_instant_value_string_3[64] = {0};

void eco2_baseline_voltage_characterization_command(char * arg);
void tvoc_baseline_voltage_characterization_command(char * arg);
void res_baseline_voltage_characterization_command(char * arg);
void eco2_slope_command(char * arg);
void eco2_offset_command(char * arg);
void tvoc_slope_command(char * arg);
void tvoc_offset_command(char * arg);
void res_slope_command(char * arg);
void res_offset_command(char * arg);

const char cmd_string_eco2_blv[] PROGMEM     = "eco2_blv   ";
const char cmd_string_tvoc_blv[] PROGMEM     = "tvoc_blv   ";
const char cmd_string_res_blv[] PROGMEM      = "res_blv    ";
const char cmd_string_eco2_slope[] PROGMEM   = "eco2_slope ";
const char cmd_string_eco2_offset[] PROGMEM  = "eco2_off   ";
const char cmd_string_tvoc_slope[] PROGMEM   = "tvoc_slope ";
const char cmd_string_tvoc_offset[] PROGMEM  = "tvoc_off   ";
const char cmd_string_res_slope[] PROGMEM    = "res_slope  ";
const char cmd_string_res_offset[] PROGMEM   = "res_off    ";

void collectCO2Equivalent(void);
void collectTVOC(void);
void collectResistance(void);
boolean publishIAQCore(void);

TinyGPS gps;
SoftwareSerial gpsSerial(18, 18); // RX, TX
Adafruit_BMP280 bme;
int sensor_enable = 17;
boolean gps_disabled = false;
#define GPS_MQTT_STRING_LENGTH (128)
#define GPS_CSV_STRING_LENGTH (64)
char gps_mqtt_string[GPS_MQTT_STRING_LENGTH] = {0};
char gps_csv_string[GPS_CSV_STRING_LENGTH] = {0};

boolean mqtt_stay_connected = true;

uint32_t update_server_ip32 = 0;
char update_server_name[32] = {0};
unsigned long integrity_num_bytes_total = 0;
unsigned long integrity_crc16_checksum = 0;
uint32_t flash_file_size = 0;
uint16_t flash_signature = 0;
boolean downloaded_integrity_file = false;
boolean integrity_check_succeeded = false;
boolean allowed_to_write_config_eeprom = false;

unsigned long current_millis = 0;
char firmware_version[16] = {0};
uint8_t temperature_units = 'C';
float reported_temperature_offset_degC = 0.0f;
float reported_humidity_offset_percent = 0.0f;

float temperature_degc = 0.0f;
float relative_humidity_percent = 0.0f;
float pressure_pa = 0.0f;

float instant_temperature_degc = 0.0f;
float instant_humidity_percent = 0.0f;
float instant_pressure_pa = 0.0f;
float instant_altitude_m = 0.0f;

float gps_latitude = TinyGPS::GPS_INVALID_F_ANGLE;
float gps_longitude = TinyGPS::GPS_INVALID_F_ANGLE;
float gps_altitude = TinyGPS::GPS_INVALID_F_ALTITUDE;
unsigned long gps_age = TinyGPS::GPS_INVALID_AGE;

float user_latitude = TinyGPS::GPS_INVALID_F_ANGLE;
float user_longitude = TinyGPS::GPS_INVALID_F_ANGLE;
float user_altitude = TinyGPS::GPS_INVALID_F_ALTITUDE;

float sample_buffer[NUM_SAMPLE_BUFFERS][MAX_SAMPLE_BUFFER_DEPTH] = {0};
uint16_t sample_buffer_idx = 0;

uint32_t sampling_interval = 0;    // how frequently the sensorss are sampled
uint16_t sample_buffer_depth = 0;  // how many samples are kept in memory for averaging
uint32_t reporting_interval = 0;   // how frequently readings are reported (to wifi or console/sd)

#define TOUCH_SAMPLE_BUFFER_DEPTH (4)
float touch_sample_buffer[TOUCH_SAMPLE_BUFFER_DEPTH] = {0};

#define LCD_ERROR_MESSAGE_DELAY   (4000)
#define LCD_SUCCESS_MESSAGE_DELAY (2000)

jsmn_parser parser;
jsmntok_t json_tokens[21];

boolean temperature_ready = false;
boolean humidity_ready = false;
boolean pressure_ready = false;

boolean init_sht25_ok = false;
boolean init_spi_flash_ok = false;
boolean init_esp8266_ok = false;
boolean init_sdcard_ok = false;
boolean init_rtc_ok = false;
boolean init_bmp280_ok = false;

typedef struct {
    float temperature_degC;     // starting at this temperature
    float slope_volts_per_degC; // use a line with this slope
    float intercept_volts;      // and this intercept
    // to calculate the baseline voltage
} baseline_voltage_t;
baseline_voltage_t baseline_voltage_struct; // scratch space for a single baseline_voltage_t entry
boolean valid_temperature_characterization_struct(baseline_voltage_t * temperature_characterization_struct_p) __attribute__((weak));

#define BACKLIGHT_OFF_AT_STARTUP (0)
#define BACKLIGHT_ON_AT_STARTUP  (1)
#define BACKLIGHT_ALWAYS_ON      (2)
#define BACKLIGHT_ALWAYS_OFF     (3)

boolean g_backlight_turned_on = false;

// the software's operating mode
#define MODE_CONFIG      (1)
#define MODE_OPERATIONAL (2)
// submodes of normal behavior
#define SUBMODE_NORMAL   (3)
// #define SUBMODE_ZEROING  (4) // deprecated for SUBMODE_OFFLINE
#define SUBMODE_OFFLINE  (5)

uint8_t mode = MODE_OPERATIONAL;

// the config mode state machine's return values
#define CONFIG_MODE_NOTHING_SPECIAL  (0)
#define CONFIG_MODE_GOT_INIT         (1)
#define CONFIG_MODE_GOT_EXIT         (2)

#define EEPROM_CONFIG_MEMORY_SIZE (1024)

#define EEPROM_MAC_ADDRESS    (E2END + 1 - 6)    // MAC address, i.e. the last 6-bytes of EEPROM
// more parameters follow, address relative to each other so they don't overlap
#define EEPROM_CONNECT_METHOD     (EEPROM_MAC_ADDRESS - 1)        // connection method encoded as a single byte value
#define EEPROM_SSID               (EEPROM_CONNECT_METHOD - 32)    // ssid string, up to 32 characters (one of which is a null terminator)
#define EEPROM_NETWORK_PWD        (EEPROM_SSID - 32)              // network password, up to 32 characters (one of which is a null terminator)
#define EEPROM_SECURITY_MODE      (EEPROM_NETWORK_PWD - 1)        // security mode encoded as a single byte value
#define EEPROM_STATIC_IP_ADDRESS  (EEPROM_SECURITY_MODE - 4)      // static ipv4 address, 4 bytes - 0.0.0.0 indicates use DHCP
#define EEPROM_STATIC_NETMASK     (EEPROM_STATIC_IP_ADDRESS - 4)  // static netmask, 4 bytes
#define EEPROM_STATIC_GATEWAY     (EEPROM_STATIC_NETMASK - 4)     // static default gateway ip address, 4 bytes
#define EEPROM_STATIC_DNS         (EEPROM_STATIC_GATEWAY - 4)     // static dns server ip address, 4 bytes
#define EEPROM_MQTT_PASSWORD      (EEPROM_STATIC_DNS - 32)        // password for mqtt server, up to 32 characters (one of which is a null terminator)
#define EEPROM_ECO2_SENSITIVITY   (EEPROM_MQTT_PASSWORD - 4)      // float value, 4-bytes, the sensitivity from the sticker  [UNUSED]
#define EEPROM_ECO2_CAL_SLOPE     (EEPROM_ECO2_SENSITIVITY - 4)    // float value, 4-bytes, the slope applied to the sensor    
#define EEPROM_ECO2_CAL_OFFSET    (EEPROM_ECO2_CAL_SLOPE - 4)      // float value, 4-btyes, the offset applied to the sensor
#define EEPROM_TVOC_SENSITIVITY   (EEPROM_ECO2_CAL_OFFSET - 4)     // float value, 4-bytes, the sensitivity from the sticker  [UNUSED]
#define EEPROM_TVOC_CAL_SLOPE     (EEPROM_TVOC_SENSITIVITY - 4)     // float value, 4-bytes, the slope applied to the sensor   
#define EEPROM_TVOC_CAL_OFFSET    (EEPROM_TVOC_CAL_SLOPE - 4)       // float value, 4-bytes, the offset applied to the sensor   
#define EEPROM_PRIVATE_KEY        (EEPROM_TVOC_CAL_OFFSET - 32)     // 32-bytes of Random Data (256-bits)
#define EEPROM_MQTT_SERVER_NAME   (EEPROM_PRIVATE_KEY - 32)       // string, the DNS name of the MQTT server (default mqtt.wickeddevice.com), up to 32 characters (one of which is a null terminator)
#define EEPROM_MQTT_USERNAME      (EEPROM_MQTT_SERVER_NAME - 32)  // string, the user name for the MQTT server (default wickeddevice), up to 32 characters (one of which is a null terminator)
#define EEPROM_MQTT_CLIENT_ID     (EEPROM_MQTT_USERNAME - 32)     // string, the client identifier for the MQTT server (default SHT25 identifier), between 1 and 23 characters long
#define EEPROM_MQTT_AUTH          (EEPROM_MQTT_CLIENT_ID - 1)     // MQTT authentication enabled, single byte value 0 = disabled or 1 = enabled
#define EEPROM_MQTT_PORT          (EEPROM_MQTT_AUTH - 4)          // MQTT authentication enabled, reserve four bytes, even though you only need two for a port
#define EEPROM_UPDATE_SERVER_NAME (EEPROM_MQTT_PORT - 32)         // string, the DNS name of the Firmware Update server (default update.wickeddevice.com), up to 32 characters (one of which is a null terminator)
#define EEPROM_OPERATIONAL_MODE   (EEPROM_UPDATE_SERVER_NAME - 1) // operational mode encoded as a single byte value (e.g. NORMAL, OFFLINE, etc.)
#define EEPROM_TEMPERATURE_UNITS  (EEPROM_OPERATIONAL_MODE - 1)   // temperature units 'F' for Fahrenheit and 'C' for Celsius
#define EEPROM_UPDATE_FILENAME    (EEPROM_TEMPERATURE_UNITS - 32) // 32-bytes for the update server filename (excluding the implied extension)
#define EEPROM_TEMPERATURE_OFFSET (EEPROM_UPDATE_FILENAME - 4)    // float value, 4-bytes, the offset applied to the sensor for reporting
#define EEPROM_HUMIDITY_OFFSET    (EEPROM_TEMPERATURE_OFFSET - 4) // float value, 4-bytes, the offset applied to the sensor for reporting
#define EEPROM_BACKLIGHT_DURATION (EEPROM_HUMIDITY_OFFSET - 2)    // integer value, 2-bytes, how long, in seconds the backlight should stay on when it turns on
#define EEPROM_BACKLIGHT_STARTUP  (EEPROM_BACKLIGHT_DURATION - 1) // boolean value, whether or not the backlight should turn on at startup
#define EEPROM_SAMPLING_INTERVAL  (EEPROM_BACKLIGHT_STARTUP - 2)  // integer value, number of seconds between sensor samplings
#define EEPROM_REPORTING_INTERVAL (EEPROM_SAMPLING_INTERVAL - 2)  // integer value, number of seconds between sensor reports
#define EEPROM_AVERAGING_INTERVAL (EEPROM_REPORTING_INTERVAL - 2) // integer value, number of seconds of samples averaged
#define EEPROM_ALTITUDE_METERS    (EEPROM_AVERAGING_INTERVAL - 2) // signed integer value, 2-bytes, the altitude in meters above sea level, where the Egg is located
#define EEPROM_MQTT_TOPIC_PREFIX  (EEPROM_ALTITUDE_METERS - 64)   // up to 64-character string, prefix prepended to logical sensor topics
#define EEPROM_USE_NTP            (EEPROM_MQTT_TOPIC_PREFIX - 1)  // 1 means use NTP, anything else means don't use NTP
#define EEPROM_NTP_SERVER_NAME    (EEPROM_USE_NTP - 32)           // 32-bytes for the NTP server to use
#define EEPROM_NTP_TZ_OFFSET_HRS  (EEPROM_NTP_SERVER_NAME - 4)    // timezone offset as a floating point value
#define EEPROM_ECO2_BASELINE_VOLTAGE_TABLE (EEPROM_NTP_TZ_OFFSET_HRS - (5*sizeof(baseline_voltage_t))) // array of (up to) five structures for baseline offset characterization over temperature
#define EEPROM_MQTT_TOPIC_SUFFIX_ENABLED  (EEPROM_ECO2_BASELINE_VOLTAGE_TABLE - 1)
#define EEPROM_TVOC_BASELINE_VOLTAGE_TABLE (EEPROM_MQTT_TOPIC_SUFFIX_ENABLED - (5*sizeof(baseline_voltage_t))) // array of (up to) five structures for baseline offset characterization over temperature
#define EEPROM_RESISTANCE_BASELINE_VOLTAGE_TABLE (EEPROM_TVOC_BASELINE_VOLTAGE_TABLE - (5*sizeof(baseline_voltage_t))) // array of (up to) five structures for baseline offset characterization over temperature
#define EEPROM_RESISTANCE_SENSITIVITY   (EEPROM_RESISTANCE_BASELINE_VOLTAGE_TABLE - 4)     // float value, 4-bytes, the sensitivity from the sticker  [UNUSED]
#define EEPROM_RESISTANCE_CAL_SLOPE     (EEPROM_RESISTANCE_SENSITIVITY - 4)     // float value, 4-bytes, the slope applied to the sensor   
#define EEPROM_RESISTANCE_CAL_OFFSET    (EEPROM_RESISTANCE_CAL_SLOPE - 4)       // float value, 4-bytes, the offset applied to the sensor   
#define EEPROM_USER_LATITUDE_DEG  (EEPROM_RESISTANCE_CAL_OFFSET - 4)     // float value, 4-bytes, user specified latitude in degrees
#define EEPROM_USER_LONGITUDE_DEG (EEPROM_USER_LATITUDE_DEG - 4)         // float value, 4-bytes, user specified longitude in degrees
#define EEPROM_USER_LOCATION_EN   (EEPROM_USER_LONGITUDE_DEG - 1)        // 1 means user location supercedes GPS location, anything else means GPS or bust
#define EEPROM_2_2_0_SAMPLING_UPD (EEPROM_USER_LOCATION_EN - 1)          // 1 means to sampling parameter default changes have been applied
#define EEPROM_DISABLE_SOFTAP     (EEPROM_2_2_0_SAMPLING_UPD - 1)        // 1 means to disable softap behavior
#define EEPROM_PM1P0_CAL_OFFSET   (EEPROM_DISABLE_SOFTAP - 4)            // offset value for PM1P0 in ug/m^3
#define EEPROM_PM2P5_CAL_OFFSET   (EEPROM_PM1P0_CAL_OFFSET - 4)          // offset value for PM2P5 in ug/m^3
#define EEPROM_PM10P0_CAL_OFFSET  (EEPROM_PM2P5_CAL_OFFSET - 4)          // offset value for PM10P0 in ug/m^3
#define EEPROM_CO2_SENSITIVITY    (EEPROM_PM10P0_CAL_OFFSET - 4)      // float value, 4-bytes, the sensitivity from the sticker  [UNUSED]
#define EEPROM_CO2_CAL_SLOPE      (EEPROM_CO2_SENSITIVITY - 4)     // float value, 4-bytes, the slope applied to the sensor   [UNUSED]
#define EEPROM_CO2_CAL_OFFSET     (EEPROM_CO2_CAL_SLOPE - 4)       // float value, 4-btyes, the offset applied to the sensor
#define EEPROM_CO2_BASELINE_VOLTAGE_TABLE (EEPROM_CO2_CAL_OFFSET - (5*sizeof(baseline_voltage_t))) // array of (up to) five structures for baseline offset characterization over temperature
#define EEPROM_TEMPERATURE_OFFLINE_OFFSET (EEPROM_CO2_BASELINE_VOLTAGE_TABLE - 4)
#define EEPROM_HUMIDITY_OFFLINE_OFFSET (EEPROM_TEMPERATURE_OFFLINE_OFFSET - 4)
#define EEPROM_MQTT_STAY_CONNECTED (EEPROM_HUMIDITY_OFFLINE_OFFSET - 1)
#define EEPROM_PM_A_ENABLE (EEPROM_MQTT_STAY_CONNECTED - 1)
#define EEPROM_PM_B_ENABLE (EEPROM_PM_A_ENABLE - 1)
//  /\
//   L Add values up here by subtracting offsets to previously added values
//   * ... and make sure the addresses don't collide and start overlapping!
//   T Add values down here by adding offsets to previously added values
//  \/
#define EEPROM_BACKUP_HUMIDITY_OFFLINE_OFFSET    (EEPROM_BACKUP_TEMPERATURE_OFFLINE_OFFSET + 4)
#define EEPROM_BACKUP_TEMPERATURE_OFFLINE_OFFSET (EEPROM_BACKUP_TVOC_CAL_OFFSET + 4)
#define EEPROM_BACKUP_TVOC_CAL_OFFSET    (EEPROM_BACKUP_TVOC_CAL_SLOPE + 4)
#define EEPROM_BACKUP_TVOC_CAL_SLOPE     (EEPROM_BACKUP_TVOC_SENSITIVITY + 4)
#define EEPROM_BACKUP_TVOC_SENSITIVITY   (EEPROM_BACKUP_ECO2_CAL_OFFSET + 4)
#define EEPROM_BACKUP_ECO2_CAL_OFFSET     (EEPROM_BACKUP_ECO2_CAL_SLOPE + 4)
#define EEPROM_BACKUP_ECO2_CAL_SLOPE      (EEPROM_BACKUP_ECO2_SENSITIVITY + 4)
#define EEPROM_BACKUP_ECO2_SENSITIVITY    (EEPROM_BACKUP_RESISTANCE_CAL_OFFSET + 4)
#define EEPROM_BACKUP_RESISTANCE_CAL_OFFSET    (EEPROM_BACKUP_RESISTANCE_CAL_SLOPE + 4)
#define EEPROM_BACKUP_RESISTANCE_CAL_SLOPE     (EEPROM_BACKUP_RESISTANCE_SENSITIVITY + 4)
#define EEPROM_BACKUP_RESISTANCE_SENSITIVITY   (EEPROM_BACKUP_NTP_TZ_OFFSET_HRS + 4)
#define EEPROM_BACKUP_NTP_TZ_OFFSET_HRS  (EEPROM_BACKUP_HUMIDITY_OFFSET + 4)
#define EEPROM_BACKUP_HUMIDITY_OFFSET    (EEPROM_BACKUP_TEMPERATURE_OFFSET + 4)
#define EEPROM_BACKUP_TEMPERATURE_OFFSET (EEPROM_BACKUP_PRIVATE_KEY + 32)
#define EEPROM_BACKUP_PRIVATE_KEY        (EEPROM_BACKUP_PM1P0_CAL_OFFSET + 4)
#define EEPROM_BACKUP_PM1P0_CAL_OFFSET   (EEPROM_BACKUP_PM2P5_CAL_OFFSET + 4)
#define EEPROM_BACKUP_PM2P5_CAL_OFFSET   (EEPROM_BACKUP_PM10P0_CAL_OFFSET + 4)
#define EEPROM_BACKUP_PM10P0_CAL_OFFSET  (EEPROM_BACKUP_CO2_CAL_OFFSET + 4)
#define EEPROM_BACKUP_CO2_CAL_OFFSET     (EEPROM_BACKUP_CO2_CAL_SLOPE + 4)
#define EEPROM_BACKUP_CO2_CAL_SLOPE      (EEPROM_BACKUP_CO2_SENSITIVITY + 4)
#define EEPROM_BACKUP_CO2_SENSITIVITY    (EEPROM_BACKUP_MQTT_PASSWORD + 32)
#define EEPROM_BACKUP_MQTT_PASSWORD      (EEPROM_BACKUP_MAC_ADDRESS + 6)
#define EEPROM_BACKUP_MAC_ADDRESS        (EEPROM_BACKUP_CHECK + 2) // backup parameters are added here offset from the EEPROM_CRC_CHECKSUM
#define EEPROM_BACKUP_CHECK              (EEPROM_CRC_CHECKSUM + 2) // 2-byte value with various bits set if backup has ever happened
#define EEPROM_CRC_CHECKSUM              (E2END + 1 - EEPROM_CONFIG_MEMORY_SIZE) // reserve the last 1kB for config

// backup status bits
#define BACKUP_STATUS_PARTICULATE_CALIBRATION_BIT (9)
#define BACKUP_STATUS_MAC_ADDRESS_BIT             (8)
#define BACKUP_STATUS_MQTT_PASSSWORD_BIT          (7)
#define BACKUP_STATUS_ECO2_CALIBRATION_BIT        (6)
#define BACKUP_STATUS_TVOC_CALIBRATION_BIT        (5)
#define BACKUP_STATUS_CO2_CALIBRATION_BIT         (4)
#define BACKUP_STATUS_PRIVATE_KEY_BIT             (3)
#define BACKUP_STATUS_TEMPERATURE_CALIBRATION_BIT (2)
#define BACKUP_STATUS_HUMIDITY_CALIBRATION_BIT    (1)
#define BACKUP_STATUS_TIMEZONE_CALIBRATION_BIT    (0)

// valid connection methods
// only DIRECT is supported initially
#define CONNECT_METHOD_DIRECT        (0)

#define BIT_IS_CLEARED(val, b) (!(val & (1UL << b)))
#define CLEAR_BIT(val, b) \
  do { \
    val &= ~(1UL << b); \
  } while(0)

boolean mirrored_config_restore_and_validate(void);
boolean mirrored_config_matches_eeprom_config(void);
void setLCD_P(const char * str PROGMEM);
void suspendGpsProcessing(void);
void collectTouch(void);
boolean processTouchQuietly(void);
void petWatchdog(void);
void updateCornerDot(void);
void backlightOn(void);


boolean mode_requires_wifi(uint8_t opmode);
void resumeGpsProcessing(void);
void delayForWatchdog(void);
void displayRSSI(void);
boolean restartWifi(void);
void watchdogForceReset(void);
void commitConfigToMirroredConfig(void);
void checkForFirmwareUpdates(void);
void checkForESPFirmwareUpdates(void);
void getNetworkTime(void);
boolean mqttReconnect(void);
boolean mqttDisconnect(void);
void lcdFrownie(uint8_t pos_x, uint8_t pos_y);
void backlightOff(void);
void clearLCD(void);
void clearLCD(boolean repaint);
void repaintLCD(void);
void updateLCD(const char * str, uint8_t pos_x, uint8_t pos_y, uint8_t num_chars);
void updateLCD(const char * str, uint8_t pos_x, uint8_t pos_y, uint8_t num_chars, boolean repaint);
void updateLCD(float value, uint8_t pos_x, uint8_t pos_y, uint8_t field_width);
void updateLCD(float value, uint8_t pos_x, uint8_t pos_y, uint8_t field_width, boolean repaint);
void updateLCD(uint32_t ip, uint8_t line_number);
void updateLCD(const char * str, uint8_t line_number);
void updateLCD(int32_t value, uint8_t pos_x, uint8_t pos_y, uint8_t num_chars);
void updateLCD(char value, uint8_t pos_x, uint8_t pos_y, uint8_t num_chars);
float toFahrenheit(float degC);
void processTouchBetweenGpsMessages(char c);
void collectTemperature(void);
void collectHumidity(void);
void collectPressure(void);
void updateGpsStrings(void);
void advanceSampleBufferIndex(void);
void loop_wifi_mqtt_mode(void);
void loop_offline_mode(void);
void watchdogInitialize(void);
void selectNoSlot(void);
void getCurrentFirmwareSignature(void);
void selectSlot1(void);
void selectSlot2(void);
void selectSlot3(void);
time_t AQE_now(void);
void clearTempBuffers(void);
void recomputeAndStoreConfigChecksum(void);
uint16_t computeEepromChecksum(void);
uint16_t getStoredEepromChecksum(void);
void recomputeAndStoreConfigChecksum(void);
void help_menu(char * arg);
void print_eeprom_value(char * arg);
void initialize_eeprom_value(char * arg);
void restore(char * arg);
void set_mac_address(char * arg);
void set_connection_method(char * arg);
void set_ssid(char * arg);
void set_network_password(char * arg);
void set_network_security_mode(char * arg);
void set_static_ip_address(char * arg);
void use_command(char * arg);
void set_mqtt_password(char * arg);
void set_mqtt_server(char * arg);
void set_mqtt_port(char * arg);
void set_mqtt_username(char * arg);
void set_mqtt_client_id(char * arg);
void set_mqtt_authentication(char * arg);
void set_mqtt_topic_prefix(char * arg);
void backup(char * arg);
void set_reported_temperature_offset(char * arg);
void set_reported_humidity_offset(char * arg);
void set_reported_temperature_offline_offset(char * arg);
void set_reported_humidity_offline_offset(char * arg);
void set_private_key(char * arg);
void set_operational_mode(char * arg);
void set_temperature_units(char * arg);
void set_update_filename(char * arg);
void force_command(char * arg);
void set_backlight_behavior(char * arg);
void AQE_set_datetime(char * arg);
void list_command(char * arg);
void download_command(char * arg);
void delete_command(char * arg);
void sampling_command(char * arg);
void altitude_command(char * arg);
void set_ntp_server(char * arg);
void set_ntp_timezone_offset(char * arg);
void set_update_server_name(char * arg);
void topic_suffix_config(char * arg);
void set_user_latitude(char * arg);
void set_user_longitude(char * arg);
void set_user_location_enable(char * arg);


void verifyProgmemWithSpiFlash(void);

// Note to self:
//   When implementing a new parameter, ask yourself:
//     should there be a command for the user to set its value directly
//     should 'get' support it (almost certainly the answer is yes)
//     should 'init' support it (is there a way to set it without user intervention)
//     should 'restore' support it directly
//     should 'restore defaults' support it
//   ... and remember, anything that changes the config EEPROM
//       needs to call recomputeAndStoreConfigChecksum after doing so

// the order of the command keywords in this array
// must be kept in index-correspondence with the associated
// function pointers in the command_functions array
//
// these keywords are padded with spaces
// in order to ease printing as a table
// string comparisons should use strncmp rather than strcmp
const char cmd_string_get[] PROGMEM         = "get        ";
const char cmd_string_init[] PROGMEM        = "init       ";
const char cmd_string_restore[] PROGMEM     = "restore    ";
const char cmd_string_mac[] PROGMEM         = "mac        ";
const char cmd_string_method[] PROGMEM      = "method     ";
const char cmd_string_ssid[] PROGMEM        = "ssid       ";
const char cmd_string_pwd[] PROGMEM         = "pwd        ";
const char cmd_string_security[] PROGMEM    = "security   ";
const char cmd_string_staticip[] PROGMEM    = "staticip   ";
const char cmd_string_use[] PROGMEM         = "use        ";
const char cmd_string_mqttsrv[] PROGMEM     = "mqttsrv    ";
const char cmd_string_mqttport[] PROGMEM    = "mqttport   ";
const char cmd_string_mqttuser[] PROGMEM    = "mqttuser   ";
const char cmd_string_mqttpwd[] PROGMEM     = "mqttpwd    ";
const char cmd_string_mqttid[] PROGMEM      = "mqttid     ";
const char cmd_string_mqttauth[] PROGMEM    = "mqttauth   ";
const char cmd_string_mqttprefix[] PROGMEM  = "mqttprefix ";
const char cmd_string_mqttsuffix[] PROGMEM  = "mqttsuffix ";
const char cmd_string_updatesrv[] PROGMEM   = "updatesrv  ";
const char cmd_string_backup[] PROGMEM      = "backup     ";
const char cmd_string_temp_off[] PROGMEM    = "temp_off   ";
const char cmd_string_temp_sdoff[] PROGMEM  = "temp_sdoff ";
const char cmd_string_hum_off[] PROGMEM     = "hum_off    ";
const char cmd_string_hum_sdoff[] PROGMEM   = "hum_sdoff  ";
const char cmd_string_key[] PROGMEM         = "key        ";
const char cmd_string_opmode[] PROGMEM      = "opmode     ";
const char cmd_string_tempunit[] PROGMEM    = "tempunit   ";
const char cmd_string_updatefile[] PROGMEM  = "updatefile ";
const char cmd_string_force[] PROGMEM       = "force      ";
const char cmd_string_backlight[] PROGMEM   = "backlight  ";
const char cmd_string_datetime[] PROGMEM    = "datetime   ";
const char cmd_string_list[] PROGMEM        = "list       ";
const char cmd_string_download[] PROGMEM    = "download   ";
const char cmd_string_delete[] PROGMEM      = "delete     ";
const char cmd_string_sampling[] PROGMEM    = "sampling   ";
const char cmd_string_altitude[] PROGMEM    = "altitude   ";
const char cmd_string_ntpsrv[] PROGMEM      = "ntpsrv     ";
const char cmd_string_tz_off[] PROGMEM      = "tz_off     ";
const char cmd_string_usr_lat[] PROGMEM     = "latitude   ";
const char cmd_string_usr_lng[] PROGMEM     = "longitude  ";
const char cmd_string_usr_loc_en[] PROGMEM  = "location   ";


const char cmd_string_null[] PROGMEM        = "";

PGM_P const commands[] PROGMEM = {
    cmd_string_get,
    cmd_string_init,
    cmd_string_restore,
    cmd_string_mac,
    cmd_string_method,
    cmd_string_ssid,
    cmd_string_pwd,
    cmd_string_security,
    cmd_string_staticip,
    cmd_string_use,
    cmd_string_mqttsrv,
    cmd_string_mqttport,
    cmd_string_mqttuser,
    cmd_string_mqttpwd,
    cmd_string_mqttid,
    cmd_string_mqttauth,
    cmd_string_mqttprefix,
    cmd_string_mqttsuffix,
    cmd_string_updatesrv,
    cmd_string_backup,
    cmd_string_temp_off,
    cmd_string_hum_off,
    cmd_string_temp_sdoff,
    cmd_string_hum_sdoff,
    cmd_string_key,
    cmd_string_opmode,
    cmd_string_tempunit,
    cmd_string_updatefile,
    cmd_string_force,
    cmd_string_backlight,
    cmd_string_datetime,
    cmd_string_list,
    cmd_string_download,
    cmd_string_delete,
    cmd_string_sampling,
    cmd_string_altitude,
    cmd_string_ntpsrv,
    cmd_string_tz_off,
    cmd_string_usr_lat,
    cmd_string_usr_lng,
    cmd_string_usr_loc_en,


    cmd_string_pm1p0_off,
    cmd_string_pm2p5_off,
    cmd_string_pm10p0_off,
    cmd_string_beginpm,
    cmd_string_testpm,
    cmd_string_pmsen,
    cmd_string_pm_enable,
    cmd_string_pm_disable,
    cmd_string_co2_off,
    cmd_string_co2_blv,
    cmd_string_eco2_blv,
    cmd_string_tvoc_blv,
    cmd_string_res_blv,
    cmd_string_eco2_slope,
    cmd_string_eco2_offset,
    cmd_string_tvoc_slope,
    cmd_string_tvoc_offset,
    cmd_string_res_slope,
    cmd_string_res_offset,

    cmd_string_null
};

void (*command_functions[])(char * arg) = {
    print_eeprom_value,
    initialize_eeprom_value,
    restore,
    set_mac_address,
    set_connection_method,
    set_ssid,
    set_network_password,
    set_network_security_mode,
    set_static_ip_address,
    use_command,
    set_mqtt_server,
    set_mqtt_port,
    set_mqtt_username,
    set_mqtt_password,
    set_mqtt_client_id,
    set_mqtt_authentication,
    set_mqtt_topic_prefix,
    topic_suffix_config,
    set_update_server_name,
    backup,
    set_reported_temperature_offset,
    set_reported_humidity_offset,
    set_reported_temperature_offline_offset,
    set_reported_humidity_offline_offset,
    set_private_key,
    set_operational_mode,
    set_temperature_units,
    set_update_filename,
    force_command,
    set_backlight_behavior,
    AQE_set_datetime,
    list_command,
    download_command,
    delete_command,
    sampling_command,
    altitude_command,
    set_ntp_server,
    set_ntp_timezone_offset,
    set_user_latitude,
    set_user_longitude,
    set_user_location_enable,


    set_pm1p0_offset,
    set_pm2p5_offset,
    set_pm10p0_offset,
    begin_pm,
    test_pm,
    pmsen,
    cmd_pm_enable,
    cmd_pm_disable,
    set_co2_offset,
    co2_baseline_voltage_characterization_command,
    eco2_baseline_voltage_characterization_command,
    tvoc_baseline_voltage_characterization_command,
    res_baseline_voltage_characterization_command,
    eco2_slope_command,
    eco2_offset_command,
    tvoc_slope_command,
    tvoc_offset_command,
    res_slope_command,
    res_offset_command,

    0
};

// tiny watchdog timer intervals
unsigned long previous_tinywdt_millis = 0;
const long tinywdt_interval = 1000;

// sensor sampling timer intervals
unsigned long previous_sensor_sampling_millis = 0;

// touch sampling timer intervals
unsigned long previous_touch_sampling_millis = 0;
const long touch_sampling_interval = 200;

// progress dots timer intervals
unsigned long previous_progress_dots_millis = 0;
const long progress_dots_interval = 1000;

#define NUM_HEARTBEAT_WAVEFORM_SAMPLES (84)
const uint8_t heartbeat_waveform[NUM_HEARTBEAT_WAVEFORM_SAMPLES] PROGMEM = {
    95, 94, 95, 96, 95, 94, 95, 96, 95, 94,
    95, 96, 95, 94, 95, 96, 95, 97, 105, 112,
    117, 119, 120, 117, 111, 103, 95, 94, 95, 96,
    95, 94, 100, 131, 162, 193, 224, 255, 244, 214,
    183, 152, 121, 95, 88, 80, 71, 74, 82, 90,
    96, 95, 94, 95, 96, 97, 106, 113, 120, 125,
    129, 132, 133, 131, 128, 124, 118, 111, 103, 96,
    95, 96, 95, 94, 95, 96, 95, 94, 95, 99,
    105, 106, 101, 96
};
uint8_t heartbeat_waveform_index = 0;

#define SCRATCH_BUFFER_SIZE (1024)
char scratch[SCRATCH_BUFFER_SIZE] = { 0 };  // scratch buffer, for general use
uint16_t scratch_idx = 0;
#define ESP8266_INPUT_BUFFER_SIZE (1000)
uint8_t esp8266_input_buffer[ESP8266_INPUT_BUFFER_SIZE] = {0};     // sketch must instantiate a buffer to hold incoming data
// 1500 bytes is way overkill for MQTT, but if you have it, may as well
// make space for a whole TCP packet
char converted_value_string[64] = {0};
char compensated_value_string[64] = {0};
char raw_value_string[64] = {0};




char raw_instant_value_string[64] = {0};
char response_body[256] = {0};

char MQTT_TOPIC_STRING[128] = {0};
char MQTT_TOPIC_PREFIX[64] = "/orgs/wd/aqe/";
uint8_t mqtt_suffix_enabled = 0;

const char * header_row = "Timestamp,"
                          "Temperature[degC],"
                          "Humidity[percent],"

                          "CO2[ppm],"
                          "PM1.0[ug/m^3],"
                          "PM2.5[ug/m^3],"
                          "PM10.0[ug/m^3],"
                          "CO2 Equivalent[ppm],"
                          "TVOC [ppb],"
                          "Resistance [ohms],"

                          "Pressure[Pa],"
                          "Latitude[deg],"
                          "Longitude[deg],"
                          "Altitude[m]";

static boolean wdt_reset_pending = false;

void setup() {
    boolean integrity_check_passed = false;
    boolean mirrored_config_mismatch = false;
    boolean valid_ssid_passed = false;

    // turn off backlight
    pinMode(A6, OUTPUT);
    digitalWrite(A6, LOW);

    // allow for power stabilization
    delay(500);

    // initialize hardware
    initializeHardware();

    //  uint8_t tmp[EEPROM_CONFIG_MEMORY_SIZE] = {0};
    //  get_eeprom_config(tmp);
    //  Serial.println(F("EEPROM Config:"));
    //  dump_config(tmp);
    //  Serial.println();
    //  Serial.println(F("Mirrored Config:"));
    //  get_mirrored_config(tmp);
    //  dump_config(tmp);
    //  Serial.println();

    integrity_check_passed = checkConfigIntegrity();
    // if the integrity check failed, try and undo the damage using the mirror config, if it's valid
    if(!integrity_check_passed) {
        Serial.println(F("Info: Startup config integrity check failed, attempting to restore from mirrored configuration."));
        allowed_to_write_config_eeprom = true;
        mirrored_config_restore_and_validate();
        integrity_check_passed = checkConfigIntegrity();
        mirrored_config_mismatch = !mirrored_config_matches_eeprom_config();
        allowed_to_write_config_eeprom = false;
    }
    else if(!mirrored_config_matches_eeprom_config()) {
        // probably the reason you got into this case is because you changed settings
        // in CLI mode but failed to connect to a network afterwards
        // so this will revert the Egg to use its last settings that *did* connect to a network
        Serial.println(F("Info: Startup config integrity check passed, but mirrored config differs, attempting to restore from mirrored configuration."));
        allowed_to_write_config_eeprom = true;
        mirrored_config_restore_and_validate();
        integrity_check_passed = checkConfigIntegrity();
        mirrored_config_mismatch = !mirrored_config_matches_eeprom_config();
        allowed_to_write_config_eeprom = false;
    }

    // possible outcomes of the above code
    // integrity_check_passed && mirrored_config_mismatch
    //   means... mirror configuration is not yet valid
    // integrity_check_passed && !mirrored_config_mismatch
    //   means... everything is great, normal behavior
    // !integrity_check_passed && mirrored_config_mismatch
    //   means... all attempts to attain a valid configuration faile
    //            and the mirrored config is *different* from the eeprom config
    // !integrity_check_passed && !mirrored_config_mismatch
    //   means... all attempts to attain a valid configuration faile
    //            and the mirrored config is *identical* to the eeprom config
    valid_ssid_passed = valid_ssid_config();
    boolean ok_to_exit_config_mode = true;

    // if a software update introduced new settings
    // they should be populated with defaults as necessary
    initializeNewConfigSettings();
    user_location_override = (eeprom_read_byte((const uint8_t *) EEPROM_USER_LOCATION_EN) == 1) ? true : false;
    uint8_t target_mode = eeprom_read_byte((const uint8_t *) EEPROM_OPERATIONAL_MODE);

    // get the temperature units
    temperature_units = eeprom_read_byte((const uint8_t *) EEPROM_TEMPERATURE_UNITS);
    if((temperature_units != 'C') && (temperature_units != 'F')) {
        temperature_units = 'C';
    }

    // get the sampling, reporting, and averaging parameters
    sampling_interval = eeprom_read_word((uint16_t * ) EEPROM_SAMPLING_INTERVAL) * 1000L;
    reporting_interval = eeprom_read_word((uint16_t * ) EEPROM_REPORTING_INTERVAL) * 1000L;
    sample_buffer_depth = (uint16_t) ((((uint32_t) eeprom_read_word((uint16_t * ) EEPROM_AVERAGING_INTERVAL)) * 1000L) / sampling_interval);


    // config mode processing loop
    do {
        // if the appropriate escape sequence is received within 8 seconds
        // go into config mode
        const long startup_time_period = 12000;
        long start = millis();
        long min_over = 100;
        boolean got_serial_input = false;
        Serial.println(F("Enter 'aqe' for CONFIG mode."));
        Serial.print(F("OPERATIONAL mode automatically begins after "));
        Serial.print(startup_time_period / 1000);
        Serial.println(F(" secs of no input."));
        setLCD_P(PSTR(" TOUCH TO SETUP "
                      " OR CONNECT USB "));



        boolean touch_detected = false;
        current_millis = millis();
        suspendGpsProcessing();
        while (current_millis < start + startup_time_period) { // can get away with this sort of thing at start up
            current_millis = millis();



            if(current_millis - previous_touch_sampling_millis >= touch_sampling_interval) {
                static uint8_t num_touch_intervals = 0;
                previous_touch_sampling_millis = current_millis;
                collectTouch();
                touch_detected = processTouchQuietly();

                num_touch_intervals++;
                if(num_touch_intervals == 5) {
                    petWatchdog();
                    num_touch_intervals = 0;
                }

            }

            if (Serial.available()) {
                if (got_serial_input == false) {
                    Serial.println();
                }
                got_serial_input = true;

                start = millis(); // reset the timeout
                if (CONFIG_MODE_GOT_INIT == configModeStateMachine(Serial.read(), false)) {
                    mode = MODE_CONFIG;
                    allowed_to_write_config_eeprom = true;
                    break;
                }
            }

            // output a countdown to the Serial Monitor
            if (millis() - start >= min_over) {
                uint8_t countdown_value_display = (startup_time_period - 500 - min_over) / 1000;
                if (got_serial_input == false) {
                    Serial.print(countdown_value_display);
                    Serial.print(F("..."));
                }

                if(countdown_value_display == 10) {
                    // do reset on error, don't display text on LCD
                    collectParticulate(true, false);
                }


                updateCornerDot();

                min_over += 1000;
            }
        }
        Serial.println();
        backlightOn();


        if(true) {

            valid_ssid_passed = valid_ssid_config();

            // check for initial integrity of configuration in eeprom
            if((mode != MODE_CONFIG) && mode_requires_wifi(target_mode) && !valid_ssid_passed) {

                Serial.println(F("Info: No valid SSID configured, automatically falling back to CONFIG mode."));
                configInject(F("aqe\r"));
                Serial.println();

                if(true) {

                    // if you're not already in config mode, and if softap is NOT allowed,
                    // and if your operational mode requires wifi
                    // and if you don't have a viable ssid configured
                    // then coerce terminal-based config mode
                    setLCD_P(PSTR("PLEASE CONFIGURE"
                                  "NETWORK SETTINGS"));
                    delay(LCD_ERROR_MESSAGE_DELAY);

                    configInject(F("aqe\r"));
                    Serial.println();
                    mode = MODE_CONFIG;
                }
            }
            else if(!integrity_check_passed) {
                // we have no choice but to offer config mode to the user
                Serial.println(F("Info: Config memory integrity check failed, automatically falling back to CONFIG mode."));
                configInject(F("aqe\r"));
                Serial.println();
                setLCD_P(PSTR("CONFIG INTEGRITY"
                              "  CHECK FAILED  "));
                mode = MODE_CONFIG;
            }
        }

        resumeGpsProcessing();
        Serial.println();
        delayForWatchdog();

        // check to determine if we have a GPS
        uint32_t gps_wait = millis() + 1500;
        while(!gps_installed && (gpsSerial.available() || (millis() < gps_wait))) {
            char c = gpsSerial.read();
            if(c == '$') {
                gps_installed = true;
            }
        }
        suspendGpsProcessing();

        while((mode == MODE_CONFIG) || ((mode_requires_wifi(target_mode) && !valid_ssid_passed))) {
            mode = MODE_CONFIG; // fix for invalid ssid in normal mode and typing exist causing spin state

            allowed_to_write_config_eeprom = true;
            const uint32_t idle_timeout_period_ms = 1000UL * 60UL * 5UL; // 5 minutes
            uint32_t idle_time_ms = 0;
            Serial.println(F("-~=* In CONFIG Mode *=~-"));
            if(integrity_check_passed) {
                setLCD_P(PSTR("  CONFIG MODE"));
            }

            Serial.print(F("OPERATIONAL mode begins automatically after "));
            Serial.print((idle_timeout_period_ms / 1000UL) / 60UL);
            Serial.println(F(" mins without input."));
            Serial.println(F("Enter 'help' for a list of available commands, "));

            configInject(F("get settings\r"));
            Serial.println();
            Serial.println(F(" @=============================================================@"));
            Serial.println(F(" # GETTING STARTED                                             #"));
            Serial.println(F(" #-------------------------------------------------------------#"));
            Serial.println(F(" #   First type 'ssid your_ssid_here' and & press <enter>      #"));
            Serial.println(F(" #   Then type 'pwd your_network_password' & press <enter>     #"));
            Serial.println(F(" #   Then type 'get settings' & press <enter> to review config #"));
            Serial.println(F(" #   Finally, type 'exit' to go into OPERATIONAL mode,         #"));
            Serial.println(F(" #     and verify that the Egg connects to your network!       #"));
            Serial.println(F(" @=============================================================@"));

            prompt();
            for (;;) {
                current_millis = millis();

                // check to determine if we have a GPS
                while(!gps_installed && gpsSerial.available()) {
                    char c = gpsSerial.read();
                    if(c == '$') {
                        gps_installed = true;
                    }
                }

                // stuck in this loop until the command line receives an exit command
                if(mode != MODE_CONFIG) {
                    break; // if a command changes mode, we're done with config
                }

                if (Serial.available()) {
                    idle_time_ms = 0;
                    // if you get serial traffic, pass it along to the configModeStateMachine for consumption
                    if (CONFIG_MODE_GOT_EXIT == configModeStateMachine(Serial.read(), false)) {
                        mode = MODE_OPERATIONAL;
                        break;
                    }
                }

                // pet the watchdog once a second
                if (current_millis - previous_tinywdt_millis >= tinywdt_interval) {
                    idle_time_ms += tinywdt_interval;
                    petWatchdog();
                    previous_tinywdt_millis = current_millis;
                }

                if (idle_time_ms >= idle_timeout_period_ms) {
                    Serial.println(F("Info: Idle time expired, exiting CONFIG mode."));
                    break;
                }
            }

            valid_ssid_passed = valid_ssid_config();
            if(valid_ssid_passed) {
                break;
            }
        }

        integrity_check_passed = checkConfigIntegrity();
        valid_ssid_passed = valid_ssid_config();
        ok_to_exit_config_mode = true;

        target_mode = eeprom_read_byte((const uint8_t *) EEPROM_OPERATIONAL_MODE);
        if(!integrity_check_passed) {
            ok_to_exit_config_mode = false;
            mode = MODE_CONFIG;
        }
        else if(mode_requires_wifi(target_mode) && !valid_ssid_passed) {
            ok_to_exit_config_mode = false;
            mode = MODE_CONFIG;
        }

    }
    while(!ok_to_exit_config_mode);

    allowed_to_write_config_eeprom = false;

    Serial.println(F("-~=* In OPERATIONAL Mode *=~-"));
    setLCD_P(PSTR("OPERATIONAL MODE"));
    SUCCESS_MESSAGE_DELAY();

    // ... but *which* operational mode are we in?
    mode = target_mode;

    // ... and what is the temperature and humdidity offset we should use
    if(mode_requires_wifi(mode)) {
        reported_temperature_offset_degC = eeprom_read_float((float *) EEPROM_TEMPERATURE_OFFSET);
        reported_humidity_offset_percent = eeprom_read_float((float *) EEPROM_HUMIDITY_OFFSET);
    } else {
        reported_temperature_offset_degC = eeprom_read_float((float *) EEPROM_TEMPERATURE_OFFLINE_OFFSET);
        reported_humidity_offset_percent = eeprom_read_float((float *) EEPROM_HUMIDITY_OFFLINE_OFFSET);
        if(isnan(reported_temperature_offset_degC)) {
            reported_temperature_offset_degC = eeprom_read_float((float *) EEPROM_TEMPERATURE_OFFSET);
        }
        if(isnan(reported_humidity_offset_percent)) {
            reported_humidity_offset_percent = eeprom_read_float((float *) EEPROM_HUMIDITY_OFFSET);
        }
    }

    if(isnan(reported_temperature_offset_degC)) {
        reported_temperature_offset_degC = 0;
    }
    if(isnan(reported_humidity_offset_percent)) {
        reported_humidity_offset_percent = 0;
    }

    boolean use_ntp = eeprom_read_byte((uint8_t *) EEPROM_USE_NTP);
    boolean shutdown_wifi = !mode_requires_wifi(mode);
    mqtt_stay_connected = (eeprom_read_byte((const uint8_t *) EEPROM_MQTT_STAY_CONNECTED) == 1) ? true : false;
    if(mode_requires_wifi(mode) || use_ntp) {
        shutdown_wifi = false;

        // Scan Networks to show RSSI
        uint8_t connect_method = eeprom_read_byte((const uint8_t *) EEPROM_CONNECT_METHOD);
        displayRSSI();
        delayForWatchdog();
        petWatchdog();

        // Try and Connect to the Configured Network
        if(!restartWifi()) {
            // technically this code should be unreachable
            // because error conditions internal to the restartWifi function
            // should restart the unit at a finer granularity
            // but this additional report should be harmless at any rate
            Serial.println(F("Error: Failed to connect to configured network. Rebooting."));
            Serial.flush();
            // watchdogForceReset();
            wdt_reset_pending = true;
            return;
        }
        delayForWatchdog();
        petWatchdog();

        // at this point we have connected to the network successfully
        // it's an opportunity to mirror the eeprom configuration
        // if it's different from what's already there
        // importantly this check only happens at startup
        commitConfigToMirroredConfig();

        // Check for Firmware Updates
        checkForFirmwareUpdates();
        checkForESPFirmwareUpdates();
#if defined(INCLUDE_FIRMWARE_INTEGRITY_SELF_CHECK)
        verifyProgmemWithSpiFlash();
#endif
        integrity_check_passed = checkConfigIntegrity();
        if(!integrity_check_passed) {
            Serial.println(F("Error: Config Integrity Check Failed after checkForFirmwareUpdates"));
            setLCD_P(PSTR("CONFIG INTEGRITY"
                          "  CHECK FAILED  "));
            for(;;) {
                // prevent automatic reset
                delay(1000);
                petWatchdog();
            }
        }

        if(use_ntp) {
            getNetworkTime();
        }

        if(mode_requires_wifi(mode)) {
            // Connect to MQTT server
            if(mqtt_stay_connected && !mqttReconnect()) {
                setLCD_P(PSTR("  MQTT CONNECT  "
                              "     FAILED     "));
                lcdFrownie(15, 1);
                ERROR_MESSAGE_DELAY();
                Serial.println(F("Error: Unable to connect to MQTT server"));
                Serial.flush();
                // watchdogForceReset();
                wdt_reset_pending = true;
                return;
            }
            delayForWatchdog();
            petWatchdog();
        }
        else {
            shutdown_wifi = true;
        }
    }
#if defined(INCLUDE_FIRMWARE_INTEGRITY_SELF_CHECK)
    else {
        verifyProgmemWithSpiFlash();
    }
#endif

    if(shutdown_wifi) {
        // it's a mode that doesn't require Wi-Fi
        // save settings as necessary
        commitConfigToMirroredConfig();
        esp.sleep(2); // deep sleep
    }

    // get the temperature units
    temperature_units = eeprom_read_byte((const uint8_t *) EEPROM_TEMPERATURE_UNITS);
    if((temperature_units != 'C') && (temperature_units != 'F')) {
        temperature_units = 'C';
    }

    // get the sampling, reporting, and averaging parameters
    sampling_interval = eeprom_read_word((uint16_t * ) EEPROM_SAMPLING_INTERVAL) * 1000L;
    reporting_interval = eeprom_read_word((uint16_t * ) EEPROM_REPORTING_INTERVAL) * 1000L;
    sample_buffer_depth = (uint16_t) ((((uint32_t) eeprom_read_word((uint16_t * ) EEPROM_AVERAGING_INTERVAL)) * 1000L) / sampling_interval);

    if(mode == SUBMODE_NORMAL) {
        setLCD_P(PSTR("TEMP ---  RH ---"
                      "CO2  --- VOC ---"));
        SUCCESS_MESSAGE_DELAY();
    }

    resumeGpsProcessing();
    backlightOff();
}

void updateDisplayedSensors() {
    static uint8_t current_displayed_page = 0;
    static boolean first = true;
    static unsigned long previous_display_update = 0;
    const long display_update_interval = 15000;
    const uint8_t NUM_DISPLAY_PAGES = 2;

    if(first || ((current_millis - previous_display_update) >= display_update_interval)) {
        previous_display_update = current_millis;
        clearLCD(false);

        if(display_offline_mode_banner) {
            if(init_sdcard_ok) {
                setLCD_P(PSTR("  LOGGING DATA  "
                              "   TO SD CARD   "));
            }
            else { // if(!init_sdcard_ok)
                setLCD_P(PSTR("  LOGGING DATA  "
                              "  TO USB-SERIAL "));
            }
            repaintLCD();
            first = false;
            return;
        }

        switch(current_displayed_page) {
        case 2:

            break;
        case 1:
            updateLCD("P1 ", 0, 0, 3, false);
            updateLCD("P25 ", 8, 0, 4, false);
            updateLCD("P10 ", 0, 1, 4, false);
            if(init_bmp280_ok) {
                updateLCD("BP ", 9, 1, 3, false);
                if(pressure_ready || (sample_buffer_idx > 0)) {
                    updateLCD(pressure_pa / 1000.0f, 12, 1, 4, false);
                }
                else {
                    updateLCD("---", 12, 1, 4, false);
                }
            }

            if(particulate_ready || (sample_buffer_idx > 0)) {
                updateLCD(pm1p0_ugpm3, 3, 0, 4, false);
                updateLCD(pm2p5_ugpm3, 12, 0, 4, false);
                updateLCD(pm10p0_ugpm3, 4, 1, 3, false);
            }
            else {
                updateLCD("---", 3, 0, 4, false);
                updateLCD("---", 12, 0, 4, false);
                updateLCD("---", 4, 1, 3, false);
            }
            break;
        case 0:
        default:
            updateLCD("TEMP ", 0, 0, 5, false);
            updateLCD("RH ", 10, 0, 3, false);
            if(init_sht25_ok) {
                if(temperature_ready|| (sample_buffer_idx > 0)) {
                    float reported_temperature = temperature_degc - reported_temperature_offset_degC;
                    if(temperature_units == 'F') {
                        reported_temperature = toFahrenheit(reported_temperature);
                    }
                    updateLCD(reported_temperature, 5, 0, 3, false);
                }
                else {
                    updateLCD("---", 5, 0, 3, false);
                }
            }
            else {
                // sht25 is not ok
                updateLCD("XXX", 5, 0, 3, false);
            }

            if(init_sht25_ok) {
                if(humidity_ready || (sample_buffer_idx > 0)) {
                    float reported_relative_humidity_percent = constrain(relative_humidity_percent - reported_humidity_offset_percent, 0.0f, 100.0f);
                    updateLCD(reported_relative_humidity_percent, 13, 0, 3, false);
                }
                else {
                    updateLCD("---", 13, 0, 3, false);
                }
            }
            else {
                updateLCD("XXX", 13, 0, 3, false);
            }

            updateLCD("TEMP ", 0, 0, 5, false);
            updateLCD("RH ", 10, 0, 3, false);
            updateLCD("CO2 ", 0, 1, 4, false);
            updateLCD("VOC ", 9, 1, 4, false);
            if(init_sht25_ok) {
                if(temperature_ready|| (sample_buffer_idx > 0)) {
                    float reported_temperature = temperature_degc - reported_temperature_offset_degC;
                    if(temperature_units == 'F') {
                        reported_temperature = toFahrenheit(reported_temperature);
                    }
                    updateLCD(reported_temperature, 5, 0, 3, false);
                }
                else {
                    updateLCD("---", 5, 0, 3, false);
                }
            }
            else {
                // sht25 is not ok
                updateLCD("XXX", 5, 0, 3, false);
            }

            if(init_sht25_ok) {
                if(humidity_ready || (sample_buffer_idx > 0)) {
                    float reported_relative_humidity_percent = relative_humidity_percent - reported_humidity_offset_percent;
                    updateLCD(reported_relative_humidity_percent, 13, 0, 3, false);
                }
                else {
                    updateLCD("---", 13, 0, 3, false);
                }
            }
            else {
                updateLCD("XXX", 13, 0, 3, false);
            }

            if(iaqcore_ready || (sample_buffer_idx > 0)) {
                updateLCD(tvoc_ppb, 13, 1, 3, false);
            }
            else {
                updateLCD("---", 13, 1, 3, false);
            }

            if(co2_ready || (sample_buffer_idx > 0)) {
                updateLCD(co2_ppm, 4, 1, 4, false);
            }
            else {
                updateLCD("----", 4, 1, 4, false);
            }
            break;
        }

        repaintLCD();

        current_displayed_page++;
        current_displayed_page = current_displayed_page % NUM_DISPLAY_PAGES;
    }
    first = false;
}

void loop() {
    current_millis = millis();
    static boolean first = true;

    // whenever you come through loop, process a GPS byte if there is one
    // will need to test if this keeps up, but I think it will
    if(user_location_override) {
        // hardware doesn't matter in this case
        updateGpsStrings();
    }
    else if(!gps_disabled) {
        while(gpsSerial.available()) {
            char c = gpsSerial.read();
            // Serial.print(c);
            if(c == '$') {
                gps_installed = true;
            }

            if(gps.encode(c)) {
                gps.f_get_position(&gps_latitude, &gps_longitude, &gps_age);
                gps_altitude = gps.f_altitude();
                updateGpsStrings();
                break;
            }

            processTouchBetweenGpsMessages(c);
        }
    }

    if(first) {
        updateGpsStrings();
    }

    if(first || (current_millis - previous_sensor_sampling_millis >= sampling_interval)) {
        suspendGpsProcessing();
        previous_sensor_sampling_millis = current_millis;
        //Serial.print(F("Info: Sampling Sensors @ "));
        //Serial.println(millis());
        collectTemperature();
        collectHumidity();
        collectPressure();
        collectParticulate();
        collectCO2();
        if(iaqcore.update() && (iaqcore.getStatus() == AMS_IAQ_CORE_C_STATUS_OK)) {
            iaqcore_ok = true;
        }
        collectCO2Equivalent();
        collectTVOC();
        collectResistance();
        advanceSampleBufferIndex();
    }

    if(first || (current_millis - previous_touch_sampling_millis >= touch_sampling_interval)) {
        previous_touch_sampling_millis = current_millis;
        if(!gps_installed || user_location_override) {
            collectTouch();
        }
        processTouchQuietly();
    }


    // the following loop routines *must* return reasonably frequently
    // so that the watchdog timer is serviced
    if (first) {
        printCsvDataLine();
    }

    if (!wdt_reset_pending) {
        switch(mode) {
        case SUBMODE_NORMAL:
            loop_wifi_mqtt_mode();
            break;
        case SUBMODE_OFFLINE:
            loop_offline_mode();
            break;
        default: // unkown operating mode, nothing to be done
            break;
        }
    }

    // pet the watchdog
    if (current_millis - previous_tinywdt_millis >= tinywdt_interval) {
        previous_tinywdt_millis = current_millis;
        //Serial.println(F("Info: Watchdog Pet."));
        delayForWatchdog();
        petWatchdog();
    }

    if(gps_disabled) {
        resumeGpsProcessing();
    }

    updateDisplayedSensors();
    first = false;

    if (wdt_reset_pending) {
        watchdogForceReset();
    }
}

/****** INITIALIZATION SUPPORT FUNCTIONS ******/
void ERROR_MESSAGE_DELAY(void) {
    delay(LCD_ERROR_MESSAGE_DELAY);
}

void SUCCESS_MESSAGE_DELAY(void) {
    delay(LCD_SUCCESS_MESSAGE_DELAY);
}

void init_firmware_version(void) {
    snprintf(firmware_version, 15, "%d.%d.%d",
             AQEV2FW_MAJOR_VERSION,
             AQEV2FW_MINOR_VERSION,
             AQEV2FW_PATCH_VERSION);
}

void initEsp8266(void) {
    uint8_t connect_method = eeprom_read_byte((const uint8_t *) EEPROM_CONNECT_METHOD);
    Serial.print(F("Info: ESP8266 Initialization..."));

    esp.setTcpKeepAliveInterval(10); // 10 seconds
    esp.setInputBuffer(esp8266_input_buffer, ESP8266_INPUT_BUFFER_SIZE); // connect the input buffer up
    if (esp.reset()) {
        esp.setNetworkMode(1);
        Serial.println(F("OK."));
        init_esp8266_ok = true;
    }
    else {
        Serial.println(F("Failed."));
        init_esp8266_ok = false;
    }
}

void initializeHardware(void) {
    pinMode(sensor_enable, OUTPUT);
    digitalWrite(sensor_enable, HIGH);

    Serial.begin(115200);
    Serial1.begin(115200);

    init_firmware_version();

    // without this line, if the touch hardware is absent
    // serial input processing grinds to a snails pace
    touch.set_CS_Timeout_Millis(100);
    // touch.set_CS_AutocaL_Millis(5000);

    Serial.println(F(" +------------------------------------+"));
    Serial.println(F(" |   Welcome to Air Quality Egg 2.0   |"));
    Serial.println(F(" |    VOC / CO2 / PM Sensor Suite     |"));
    Serial.print(  F(" |       Firmware Version "));
    Serial.print(firmware_version);
    Serial.println(F("       |"));
    Serial.println(F(" +------------------------------------+"));
    Serial.print(F(" Compiled on: "));
    Serial.println(__DATE__ " " __TIME__);
    Serial.print(F(" Egg Serial Number: "));
    print_eeprom_mqtt_client_id();
    Serial.println();

    pinMode(A6, OUTPUT);
    uint8_t backlight_behavior = eeprom_read_byte((uint8_t *) EEPROM_BACKLIGHT_STARTUP);
    if((BACKLIGHT_ON_AT_STARTUP == backlight_behavior) || (backlight_behavior == BACKLIGHT_ALWAYS_ON)) {
        backlightOn();
    }
    else {
        backlightOff();
    }

    // smiley face
    byte smiley[8] = {
        B00000,
        B00000,
        B01010,
        B00000,
        B10001,
        B01110,
        B00000,
        B00000
    };

    byte frownie[8] = {
        B00000,
        B00000,
        B01010,
        B00000,
        B01110,
        B10001,
        B00000,
        B00000
    };

    byte emptybar[8] = {
        B11111,
        B10001,
        B10001,
        B10001,
        B10001,
        B10001,
        B10001,
        B11111
    };

    byte fullbar[8] = {
        B11111,
        B11111,
        B11111,
        B11111,
        B11111,
        B11111,
        B11111,
        B11111
    };

    lcd.begin(16, 2);

    lcd.createChar(0, smiley);
    lcd.createChar(1, frownie);
    lcd.createChar(2, emptybar);
    lcd.createChar(3, fullbar);

    setLCD_P(PSTR("AIR QUALITY EGG "));
    char tmp[17] = {0};
    snprintf(tmp, 16, "VERSION %d.%d.%d",
             AQEV2FW_MAJOR_VERSION,
             AQEV2FW_MINOR_VERSION,
             AQEV2FW_PATCH_VERSION);

    updateLCD(tmp, 1);

    delay(1000);
    // Initialize Tiny Watchdog
    Serial.print(F("Info: Tiny Watchdog Initialization..."));
    watchdogInitialize();
    Serial.println(F("OK."));

    Wire.begin();

    // Initialize slot select pins
    Serial.print(F("Info: Slot Select Pins Initialization..."));
    pinMode(7, OUTPUT);
    pinMode(9, OUTPUT);
    pinMode(10, OUTPUT);
    selectNoSlot();
    Serial.println(F("OK."));

    // Initialize SPI Flash
    Serial.print(F("Info: SPI Flash Initialization..."));
    if (flash.initialize()) {
        Serial.println(F("OK."));
        init_spi_flash_ok = true;
    }
    else {
        Serial.println(F("Fail."));
        init_spi_flash_ok = false;
    }

    getCurrentFirmwareSignature();

    // Initialize SHT25
    Serial.print(F("Info: SHT25 Initialization..."));
    if (sht25.begin()) {
        Serial.println(F("OK."));
        init_sht25_ok = true;
    }
    else {
        Serial.println(F("Failed."));
        init_sht25_ok = false;
    }


    pm_enable_sensor_a = eeprom_read_byte((uint8_t *) EEPROM_PM_A_ENABLE) == 0 ? 0 : 1;
    pm_enable_sensor_b = eeprom_read_byte((uint8_t *) EEPROM_PM_B_ENABLE) == 0 ? 0 : 1;

    // put the same thing in for the particulate sensors
    pmsx003Serial_1.clearInterruptsDuringTx(false);
    pmsx003Serial_2.clearInterruptsDuringTx(false);
    pinMode(A4, INPUT_PULLUP);
    pinMode(A7, INPUT_PULLUP);
    pinMode(A5, OUTPUT);
    delay(20);
    uint8_t ii = 0;
    if(pm_enable_sensor_a) {
        while(!pmsx003_1.begin()) {
            if(ii == 3) {
                break;
            }
            ii++;
        }
    }

    ii = 0;
    if(pm_enable_sensor_b) {
        while(!pmsx003_2.begin()) {
            if(ii == 3) {
                break;
            }
            ii++;
        }
    }

    co2Serial.clearInterruptsDuringTx(false);
    // this seems to be necessary
    // for CO2 software serial to work
    // must be some source of constructor
    pinMode(9, INPUT_PULLUP);
    pinMode(10, OUTPUT);
// Initialize iAQ-core
    Serial.print(F("Info: iAQ-core Initialization..."));
    selectSlot1();
    if (iaqcore.update() &&
            ((iaqcore.getStatus() == AMS_IAQ_CORE_C_STATUS_OK)
             || (iaqcore.getStatus() == AMS_IAQ_CORE_C_STATUS_BUSY)
             || (iaqcore.getStatus() == AMS_IAQ_CORE_C_STATUS_WARMING_UP))) {
        Serial.println(F("OK."));
        iaqcore_failed = false;
    }
    else {
        Serial.println(F("Failed."));
        iaqcore_failed = true;
    }

    Serial.print(F("Info: BMP280 Initialization..."));
    if (!bme.begin()) {
        Serial.println(F("Fail."));
        init_bmp280_ok = false;
    }
    else {
        Serial.println(F("OK."));
        init_bmp280_ok = true;
    }

    // Initialize SD card
    Serial.print(F("Info: RTC Initialization..."));
    selectSlot3();
    rtc.begin();
    if (rtc.isrunning()) {
        Serial.println(F("OK."));
        setSyncProvider(AQE_now);
        init_rtc_ok = true;
    }
    else {
        Serial.println(F("Fail."));
        init_rtc_ok = false;
    }

    // Initialize SD card
    Serial.print(F("Info: SD Card Initialization..."));
    if (SD.begin(16)) {
        Serial.println(F("OK."));
        init_sdcard_ok = true;
    }
    else {
        Serial.println(F("Fail."));
        init_sdcard_ok = false;
    }

    selectNoSlot();

    petWatchdog();
    SUCCESS_MESSAGE_DELAY(); // don't race past the splash screen, and give watchdog some breathing room
    initEsp8266();

    updateLCD("VOC / CO2 / PM", 0);
    updateLCD("MODEL", 1);
    SUCCESS_MESSAGE_DELAY();

    // put the same thing in for the GPS pins
    // just to be on the safe side
    pinMode(18, INPUT_PULLUP);
}

/****** CONFIGURATION SUPPORT FUNCTIONS ******/
void initializeNewConfigSettings(void) {
    char * command_buf = &(scratch[0]);
    clearTempBuffers();

    boolean in_config_mode = false;
    allowed_to_write_config_eeprom = true;


    // if necessary, initialize the default mqtt prefix
    // if it's never been set, the first byte in memory will be 0xFF
    uint8_t val = eeprom_read_byte((const uint8_t *) EEPROM_MQTT_TOPIC_PREFIX);
    if(val == 0xFF) {
        if(!in_config_mode) {
            configInject(F("aqe\r"));
            in_config_mode = true;
        }
        memset(command_buf, 0, 128);
        strcat(command_buf, "mqttprefix ");
        strcat(command_buf, MQTT_TOPIC_PREFIX);
        strcat(command_buf, "\r");
        configInject(command_buf);
    }

    // if the mqtt server is set to opensensors.io, change it to mqtt.opensensors.io
    memset(command_buf, 0, 128);
    eeprom_read_block(command_buf, (const void *) EEPROM_MQTT_SERVER_NAME, 31);
    if(strcmp_P(command_buf, PSTR("opensensors.io")) == 0) {
        if(!in_config_mode) {
            configInject(F("aqe\r"));
            in_config_mode = true;
        }
        configInject(F("mqttsrv mqtt.opensensors.io\r"));
    }

    // if the mqtt suffix enable is neither zero nor one, set it to one (enabled)
    val = eeprom_read_byte((const uint8_t *) EEPROM_MQTT_TOPIC_SUFFIX_ENABLED);
    if(val == 0xFF) {
        if(!in_config_mode) {
            configInject(F("aqe\r"));
            in_config_mode = true;
        }
        memset(command_buf, 0, 128);
        strcat(command_buf, "mqttsuffix enable\r");
        configInject(command_buf);
    }

    val = eeprom_read_byte((const uint8_t *) EEPROM_2_2_0_SAMPLING_UPD);
    if(val != 1) {
        if(!in_config_mode) {
            configInject(F("aqe\r"));
            in_config_mode = true;
        }



        eeprom_write_byte((uint8_t *) EEPROM_2_2_0_SAMPLING_UPD, 1);

        // check if ntpsrv is pool.ntp.org, and if so, switch it to 0.airqualityegg.pool.ntp.org
        memset(command_buf, 0, 128);
        eeprom_write_block(command_buf, (void *) EEPROM_NTP_SERVER_NAME, 32);
        if(strcmp(command_buf, "pool.ntp.org") == 0) {
            configInject(F("ntpsrv 0.airqualityegg.pool.ntp.org\r"));
        }

        recomputeAndStoreConfigChecksum();
    }

    // re-configure from mqtt.opensensors.io to mqtt.wickeddevice.com
    clearTempBuffers();
    eeprom_read_block(command_buf, (const void *) EEPROM_MQTT_SERVER_NAME, 32);
    if(strcmp_P(command_buf, PSTR("mqtt.opensensors.io")) == 0) {
        memset(command_buf, 0, 128);
        eeprom_read_block(command_buf, (const void *) EEPROM_MQTT_USERNAME, 32);
        if(strcmp_P(command_buf, PSTR("wickeddevice")) == 0) {
            eeprom_read_block(converted_value_string, (const void *) EEPROM_MQTT_CLIENT_ID, 32);
            if(strncmp_P(converted_value_string, PSTR("egg"), 3) == 0) {

                if(!in_config_mode) {
                    configInject(F("aqe\r"));
                    in_config_mode = true;
                }

                // change the mqtt server to mqtt.wickeddevice.com
                // and change the mqtt username to the egg serial number
                // effectively:
                //   configInject(F("mqttsrv mqtt.wickeddevice.com\r"));
                //   configInject(F("mqttuser egg-serial-number \r"));
                configInject(F("mqttsrv mqtt.wickeddevice.com\r"));
                strcpy_P(raw_instant_value_string, PSTR("mqttuser "));
                strcat(raw_instant_value_string, converted_value_string);
                strcat_P(raw_instant_value_string, PSTR("\r"));
                configInject(raw_instant_value_string);
            }
        }
    }

    // backlight settings
    uint8_t backlight_startup = eeprom_read_byte((uint8_t *) EEPROM_BACKLIGHT_STARTUP);
    uint16_t backlight_duration = eeprom_read_word((uint16_t *) EEPROM_BACKLIGHT_DURATION);
    if((backlight_startup == 0xFF) || (backlight_duration == 0xFFFF)) {
        configInject(F("aqe\r"));
        configInject(F("backlight initon\r"));
        configInject(F("backlight 60\r"));
        in_config_mode = true;
    }

// sampling settings
    uint16_t l_sampling_interval = eeprom_read_word((uint16_t * ) EEPROM_SAMPLING_INTERVAL);
    uint16_t l_reporting_interval = eeprom_read_word((uint16_t * ) EEPROM_REPORTING_INTERVAL);
    uint16_t l_averaging_interval = eeprom_read_word((uint16_t * ) EEPROM_AVERAGING_INTERVAL);
    if((l_sampling_interval == 0xFFFF) || (l_reporting_interval == 0xFFFF) || (l_averaging_interval == 0xFFFF)) {
        if(!in_config_mode) {
            configInject(F("aqe\r"));
            in_config_mode = true;
        }
        configInject(F("sampling 5, 60, 60\r"));
    }

    if(in_config_mode) {
        configInject(F("exit\r"));
    }

    allowed_to_write_config_eeprom = false;
}

boolean checkConfigIntegrity(void) {
    uint16_t computed_crc = computeEepromChecksum();
    uint16_t stored_crc = getStoredEepromChecksum();
    if (computed_crc == stored_crc) {
        return true;
    }
    else {
        //Serial.print(F("Computed CRC = "));
        //Serial.print(computed_crc, HEX);
        //Serial.print(F(", Stored CRC = "));
        //Serial.println(stored_crc, HEX);
        return false;
    }
}

// this state machine receives bytes and
// returns true if the function is in config mode
uint8_t configModeStateMachine(char b, boolean reset_buffers) {
    static boolean received_init_code = false;
    const uint8_t buf_max_write_idx = 126; // [127] must always have a null-terminator
    static char buf[128] = {0}; // buffer to hold commands / data
    static uint8_t buf_idx = 0;  // current number of bytes in buf
    boolean line_terminated = false;
    char * first_arg = 0;
    uint8_t ret = CONFIG_MODE_NOTHING_SPECIAL;

    if (reset_buffers) {
        buf_idx = 0;
    }

    //  Serial.print('[');
    //  if(isprint(b)) Serial.print((char) b);
    //  Serial.print(']');
    //  Serial.print('\t');
    //  Serial.print("0x");
    //  if(b < 0x10) Serial.print('0');
    //  Serial.println(b, HEX);

    // if you are at the last write-able location in the buffer
    // the only legal characters to accept are a backspace, a newline, or a carriage return
    // reject anything else implicitly
    if((buf_idx == buf_max_write_idx) && (b != 0x7F) && (b != 0x0D) && (b != 0x0A)) {
        Serial.println(F("Warn: Input buffer full and cannot accept new characters. Press enter to clear buffers."));
    }
    // the following logic rejects all non-printable characters besides 0D, 0A, and 7F
    else if (b == 0x7F) { // backspace key is special
        if (buf_idx > 0) {
            buf_idx--;
            buf[buf_idx] = '\0';
            Serial.print(b); // echo the character
        }
    }
    else if (b == 0x0D || b == 0x0A) { // carriage return or new line is also special
        buf[buf_idx] = '\0'; // force terminator do not advance write pointer
        line_terminated = true;
        Serial.println(); // echo the character
    }
    else if ((buf_idx <= buf_max_write_idx) && isprint(b)) {
        // otherwise if there's space and the character is 'printable' add it to the buffer
        // silently drop all other non-printable characters
        buf[buf_idx++] = b;
        buf[buf_idx] = '\0';
        Serial.print(b); // echo the character
    }

    char lower_buf[128] = {0};
    if (line_terminated) {
        strncpy(lower_buf, buf, 127);
        lowercase(lower_buf);
    }

    // process the data currently stored in the buffer
    if (received_init_code && line_terminated) {
        // with the exeption of the command "exit"
        // commands are always of the form <command> <argument>
        // they are minimally parsed here and delegated to
        // callback functions that take the argument as a string

        // Serial.print("buf = ");
        // Serial.println(buf);

        if (strncmp("aqe", lower_buf, 3) == 0) {
            ret = CONFIG_MODE_GOT_INIT;
        }
        if (strncmp("exit", lower_buf, 4) == 0) {
            Serial.println(F("Exiting CONFIG mode..."));
            ret = CONFIG_MODE_GOT_EXIT;
        }
        else {
            // the string must have one, and only one, space in it
            uint8_t num_spaces = 0;
            char * p;
            for (p = buf; *p != '\0'; p++) { // all lines are terminated by '\r' above
                if (*p == ' ') {
                    num_spaces++;
                }

                if ((num_spaces == 1) && (*p == ' ')) {
                    // if this is the first space encountered, null the original string here
                    // in order to mark the first argument string
                    *p = '\0';
                }
                else if ((num_spaces > 0) && (first_arg == 0) && (*p != ' ')) {
                    // if we are beyond the first space,
                    // and have not encountered the beginning of the first argument
                    // and this character is not a space, it is by definition
                    // the beginning of the first argument, so mark it as such
                    first_arg = p;
                }
            }

            // deal with commands that can legitimately have no arguments first
            if (strncmp("help", lower_buf, 4) == 0) {
                help_menu(first_arg);
            }
            else if(strncmp("pwd", lower_buf, 3) == 0) {
                set_network_password(first_arg);
            }
            else if (first_arg != 0) {
                //Serial.print(F("Received Command: \""));
                //Serial.print(buf);
                //Serial.print(F("\" with Argument: \""));
                //Serial.print(first_arg);
                //Serial.print(F("\""));
                //Serial.println();

                // command with argument was received, determine if it's valid
                // and if so, call the appropriate command processing function
                boolean command_found = false;
                char _temp_command[16] = {0};
                uint8_t ii = 0;
                do {
                    strcpy_P(_temp_command, (PGM_P) pgm_read_word(&(commands[ii])));
                    trim_string(_temp_command);
                    if (strncmp(_temp_command, lower_buf, max(strlen(buf), strlen(_temp_command))) == 0) {
                        command_functions[ii](first_arg);
                        command_found = true;
                        break;
                    }
                    ii++;
                } while(_temp_command[0] != 0);

                if (!command_found) {
                    Serial.print(F("Error: Unknown command \""));
                    Serial.print(buf);
                    Serial.println(F("\""));
                }
            }
            else if (strlen(buf) > 0) {
                Serial.print(F("Error: Argument expected for command \""));
                Serial.print(buf);
                Serial.println(F("\", but none was received"));
            }
        }
    }
    else if (line_terminated) {
        // before we receive the init code, the only things
        // we are looking for are an exact match to the strings
        // "AQE\r" or "aqe\r"

        if (strncmp("aqe", lower_buf, 3) == 0) {
            received_init_code = true;
            ret = CONFIG_MODE_GOT_INIT;
        }
        else if (strlen(buf) > 0) {
            Serial.print(F("Error: Expecting Config Mode Unlock Code (\"aqe\"), but received \""));
            Serial.print(buf);
            Serial.println(F("\""));
        }
    }

    // clean up the buffer if you got a line termination
    if (line_terminated) {
        if (ret == CONFIG_MODE_NOTHING_SPECIAL) {
            prompt();
        }
        buf[0] = '\0';
        buf_idx = 0;
    }

    return ret;
}

void prompt(void) {
    Serial.print(F("AQE>: "));
}

// command processing function implementations
void configInject(char * str) {
    boolean reset_buffers = true;
    while (*str != '\0') {
        boolean got_exit = false;
        got_exit = configModeStateMachine(*str++, reset_buffers);
        if (reset_buffers) {
            reset_buffers = false;
        }
    }
}

void configInject(const __FlashStringHelper *ifsh) {
    memset(converted_value_string, 0, 64);
    PGM_P p = reinterpret_cast<PGM_P>(ifsh);
    strcpy_P(converted_value_string, p);
    configInject((char *) converted_value_string);
}

void lowercase(char * str) {
    uint16_t len = strlen(str);
    if (len < 0xFFFF) {
        for (uint16_t ii = 0; ii < len; ii++) {
            str[ii] = tolower(str[ii]);
        }
    }
}

void note_know_what_youre_doing() {
    Serial.println(F("   note:    Unless you *really* know what you're doing, you should"));
    Serial.println(F("            probably not be using this command."));
}

void warn_could_break_upload() {
    Serial.println(F("   warning: Using this command incorrectly can prevent your device"));
    Serial.println(F("            from publishing data to the internet."));
}

void warn_could_break_connect() {
    Serial.println(F("   warning: Using this command incorrectly can prevent your device"));
    Serial.println(F("            from connecting to your network."));
}

void defaults_help_indent(void) {
    Serial.print(F("                     "));
}

void get_help_indent(void) {
    Serial.print(F("      "));
}

void help_menu(char * arg) {
    const uint8_t commands_per_line = 3;
    const uint8_t first_dynamic_command_index = 2;

    lowercase(arg);

    if (arg == 0) {
        // list the commands that are legal
        Serial.print(F("help    \texit    \t"));
        char _temp_command[16] = {0};
        uint8_t ii = 0;
        uint8_t jj = first_dynamic_command_index;
        do {
            strcpy_P(_temp_command, (PGM_P) pgm_read_word(&(commands[ii])));

            if ((jj % commands_per_line) == 0) {
                Serial.println();
            }
            //Serial.print(jj + 1);
            //Serial.print(". ");
            Serial.print(_temp_command);
            Serial.print('\t');

            ii++;
            jj++;
        } while(_temp_command[0] != 0);
        Serial.println();
    }
    else {
        Serial.println(F("Please visit http://airqualityegg.wickeddevice.com/help for command line usage details"));
    }
}

void print_eeprom_mac(void) {
    uint8_t _mac_address[6] = {0};
    // retrieve the value from EEPROM
    eeprom_read_block(_mac_address, (const void *) EEPROM_MAC_ADDRESS, 6);

    // print the stored value, formatted
    for (uint8_t ii = 0; ii < 6; ii++) {
        if (_mac_address[ii] < 0x10) {
            Serial.print(F("0"));
        }
        Serial.print(_mac_address[ii], HEX);

        // only print colons after the first 5 values
        if (ii < 5) {
            Serial.print(F(":"));
        }
    }
    Serial.println();
}

void print_eeprom_connect_method(void) {
    uint8_t method = eeprom_read_byte((const uint8_t *) EEPROM_CONNECT_METHOD);
    switch (method) {
    case CONNECT_METHOD_DIRECT:
        Serial.println(F("Direct Connect"));
        break;
    default:
        Serial.print(F("Error: Unknown connection method code [0x"));
        if (method < 0x10) {
            Serial.print(F("0"));
        }
        Serial.print(method, HEX);
        Serial.println(F("]"));
        break;
    }
}

boolean valid_ssid_config(void) {
    char ssid[33] = {0};
    boolean ssid_contains_only_printables = true;

    uint8_t connect_method = eeprom_read_byte((const uint8_t *) EEPROM_CONNECT_METHOD);
    eeprom_read_block(ssid, (const void *) EEPROM_SSID, 32);
    for (uint8_t ii = 0; ii <= 32; ii++) {
        if (ssid[ii] == '\0') {
            break;
        }
        else if (!isprint(ssid[ii])) {
            ssid_contains_only_printables = false;
            break;
        }
    }

    if (!ssid_contains_only_printables || (strlen(ssid) == 0)) {
        return false;
    }

    return true;
}

void print_eeprom_ssid(void) {
    char ssid[33] = {0};
    eeprom_read_block(ssid, (const void *) EEPROM_SSID, 32);

    if (!valid_ssid_config()) {
        Serial.println(F("No SSID currently configured."));
    }
    else {
        Serial.println(ssid);
    }
}

void print_eeprom_security_type(void) {
    uint8_t security = eeprom_read_byte((const uint8_t *) EEPROM_SECURITY_MODE);
    switch (security) {
    case 0:
        Serial.println(F("Open"));
        break;
    case 1:
        Serial.println(F("WEP"));
        break;
    case 2:
        Serial.println(F("WPA2"));
        break;
    case 3:
        Serial.println(F("WPA"));
        break;
    case WLAN_SEC_AUTO:
        Serial.println(F("Automatic - Not Yet Determined"));
        break;
    default:
        Serial.print(F("Error: Unknown security mode code [0x"));
        if (security < 0x10) {
            Serial.print(F("0"));
        }
        Serial.print(security, HEX);
        Serial.println(F("]"));
        break;
    }
}

void print_eeprom_ipmode(void) {
    uint8_t ip[4] = {0};
    uint8_t netmask[4] = {0};
    uint8_t gateway[4] = {0};
    uint8_t dns[4] = {0};
    uint8_t noip[4] = {0};
    eeprom_read_block(ip, (const void *) EEPROM_STATIC_IP_ADDRESS, 4);
    eeprom_read_block(netmask, (const void *) EEPROM_STATIC_NETMASK, 4);
    eeprom_read_block(gateway, (const void *) EEPROM_STATIC_GATEWAY, 4);
    eeprom_read_block(dns, (const void *) EEPROM_STATIC_DNS, 4);

    if (memcmp(ip, noip, 4) == 0) {
        Serial.println(F("Configured for DHCP"));
    }
    else {
        Serial.println(F("Configured for Static IP: "));
        for(uint8_t param_idx = 0; param_idx < 4; param_idx++) {
            for (uint8_t ii = 0; ii < 4; ii++) {
                switch(param_idx) {
                case 0:
                    if(ii == 0) {
                        Serial.print(F("   IP Address:      "));
                    }
                    Serial.print(ip[ii], DEC);
                    break;
                case 1:
                    if(ii == 0) {
                        Serial.print(F("   Netmask:         "));
                    }
                    Serial.print(netmask[ii], DEC);
                    break;
                case 2:
                    if(ii == 0) {
                        Serial.print(F("   Default Gateway: "));
                    }
                    Serial.print(gateway[ii], DEC);
                    break;
                case 3:
                    if(ii == 0) {
                        Serial.print(F("   DNS Server:      "));
                    }
                    Serial.print(dns[ii], DEC);
                    break;
                }

                if( ii != 3 ) {
                    Serial.print(F("."));
                }
                else {
                    Serial.println();
                }
            }
        }
    }
}

void print_eeprom_float(const float * address) {
    float val = eeprom_read_float(address);
    Serial.println(val, 9);
}

void print_label_with_star_if_not_backed_up(char * label, uint8_t bit_number) {
    uint16_t backup_check = eeprom_read_word((const uint16_t *) EEPROM_BACKUP_CHECK);
    Serial.print(F("  "));
    if (!BIT_IS_CLEARED(backup_check, bit_number)) {
        Serial.print(F("*"));
    }
    else {
        Serial.print(F(" "));
    }
    Serial.print(F(" "));
    Serial.print(label);
}

void print_eeprom_string(const char * address) {
    char tmp[32] = {0};
    eeprom_read_block(tmp, (const void *) address, 31);
    Serial.println(tmp);
}

void print_eeprom_string(const char * address, const char * unless_it_matches_this, const char * in_which_case_print_this_instead) {
    char tmp[32] = {0};
    eeprom_read_block(tmp, (const void *) address, 31);

    if(strcmp(tmp, unless_it_matches_this) == 0) {
        Serial.println(in_which_case_print_this_instead);
    }
    else {
        Serial.println(tmp);
    }
}

void print_eeprom_update_server() {
    print_eeprom_string((const char *) EEPROM_UPDATE_SERVER_NAME, "", "Disabled");
}

void print_eeprom_ntp_server() {
    print_eeprom_string((const char *) EEPROM_NTP_SERVER_NAME, "", "Disabled");
}

void print_eeprom_update_filename() {
    print_eeprom_string((const char *) EEPROM_UPDATE_FILENAME);
}

void print_eeprom_mqtt_server() {
    print_eeprom_string((const char *) EEPROM_MQTT_SERVER_NAME);
}


void print_eeprom_mqtt_client_id() {
    print_eeprom_string((const char *) EEPROM_MQTT_CLIENT_ID);
}

void print_eeprom_mqtt_topic_prefix() {
    print_eeprom_string((const char *) EEPROM_MQTT_TOPIC_PREFIX);
}

void print_eeprom_mqtt_topic_suffix() {
    uint8_t val = eeprom_read_byte((uint8_t * ) EEPROM_MQTT_TOPIC_SUFFIX_ENABLED);
    if(val == 1) {
        Serial.print("Enabled");
    }
    else if(val == 0) {
        Serial.print("Disabled");
    }
    else {
        Serial.print("Uninitialized");
    }
    Serial.println();
}

void print_eeprom_mqtt_username() {
    print_eeprom_string((const char *) EEPROM_MQTT_USERNAME);
}

void print_eeprom_mqtt_authentication() {
    uint8_t auth = eeprom_read_byte((uint8_t *) EEPROM_MQTT_AUTH);
    if(auth) {
        Serial.println(F("    MQTT Authentication: Enabled"));
        Serial.print(F("    MQTT Username: "));
        print_eeprom_mqtt_username();
    }
    else {
        Serial.println(F("    MQTT Authentication: Disabled"));
    }
}

void print_eeprom_operational_mode(uint8_t opmode) {
    switch (opmode) {
    case SUBMODE_NORMAL:
        Serial.println(F("Normal"));
        break;
    case SUBMODE_OFFLINE:
        Serial.println(F("Offline"));
        break;
    default:
        Serial.print(F("Error: Unknown operational mode [0x"));
        if (opmode < 0x10) {
            Serial.print(F("0"));
        }
        Serial.print(opmode, HEX);
        Serial.println(F("]"));
        break;
    }
}

void print_eeprom_temperature_units() {
    uint8_t tempunit = eeprom_read_byte((uint8_t *) EEPROM_TEMPERATURE_UNITS);
    switch (tempunit) {
    case 'C':
        Serial.println(F("Celsius"));
        break;
    case 'F':
        Serial.println(F("Fahrenheit"));
        break;
    default:
        Serial.print(F("Error: Unknown temperature units [0x"));
        if (tempunit < 0x10) {
            Serial.print(F("0"));
        }
        Serial.print(tempunit, HEX);
        Serial.println(F("]"));
        break;
    }
}

void print_altitude_settings(void) {
    int16_t l_altitude = (int16_t) eeprom_read_word((uint16_t *) EEPROM_ALTITUDE_METERS);
    if(l_altitude != -1) {
        Serial.print(l_altitude);
        Serial.println(F(" meters"));
    }
    else {
        Serial.println("Not set");
    }
}

void print_latitude_settings(void) {
    int16_t l_latitude = (int16_t) eeprom_read_word((uint16_t *) EEPROM_USER_LATITUDE_DEG);
    float f_latitude = eeprom_read_float((float *) EEPROM_USER_LATITUDE_DEG);
    if(l_latitude != -1) {
        Serial.print(f_latitude, 6);
        Serial.println(F(" degrees"));
    }
    else {
        Serial.println("Not set");
    }
}

void print_longitude_settings(void) {
    int16_t l_longitude = (int16_t) eeprom_read_word((uint16_t *) EEPROM_USER_LONGITUDE_DEG);
    float f_longitude = eeprom_read_float((float *) EEPROM_USER_LONGITUDE_DEG);
    if(l_longitude != -1) {
        Serial.print(f_longitude, 6);
        Serial.println(F(" degrees"));
    }
    else {
        Serial.println("Not set");
    }
}

void print_eeprom_backlight() {
    uint16_t backlight_duration = eeprom_read_word((uint16_t *) EEPROM_BACKLIGHT_DURATION);
    uint8_t backlight_startup = eeprom_read_byte((uint8_t *) EEPROM_BACKLIGHT_STARTUP);
    Serial.print(backlight_duration);
    Serial.print(F(" seconds, "));
    switch(backlight_startup) {
    case BACKLIGHT_ON_AT_STARTUP:
        Serial.println(F("ON at startup"));
        break;
    case BACKLIGHT_OFF_AT_STARTUP:
        Serial.println(F("OFF at startup"));
        break;
    case BACKLIGHT_ALWAYS_ON:
        Serial.println(F("always ON"));
        break;
    case BACKLIGHT_ALWAYS_OFF:
        Serial.println(F("always OFF"));
        break;
    }
}

void print_eeprom_value(char * arg) {
    if (strncmp(arg, "mac", 3) == 0) {
        print_eeprom_mac();
    }
    else if (strncmp(arg, "method", 6) == 0) {
        print_eeprom_connect_method();
    }
    else if (strncmp(arg, "ssid", 4) == 0) {
        print_eeprom_ssid();
    }
    else if (strncmp(arg, "security", 8) == 0) {
        print_eeprom_security_type();
    }
    else if (strncmp(arg, "ipmode", 6) == 0) {
        print_eeprom_ipmode();
    }

    else if(strncmp(arg, "pm1p0_off", 9) == 0) {
        print_eeprom_float((const float *) EEPROM_PM1P0_CAL_OFFSET);
    }
    else if(strncmp(arg, "pm2p5_off", 9) == 0) {
        print_eeprom_float((const float *) EEPROM_PM2P5_CAL_OFFSET);
    }
    else if(strncmp(arg, "pm10p0_off", 9) == 0) {
        print_eeprom_float((const float *) EEPROM_PM10P0_CAL_OFFSET);
    }
    else if (strncmp(arg, "co2_off", 7) == 0) {
        print_eeprom_float((const float *) EEPROM_CO2_CAL_OFFSET);
    }

    else if (strncmp(arg, "temp_off", 8) == 0) {
        print_eeprom_float((const float *) EEPROM_TEMPERATURE_OFFSET);
    }
    else if (strncmp(arg, "hum_off", 7) == 0) {
        print_eeprom_float((const float *) EEPROM_HUMIDITY_OFFSET);
    }
    else if (strncmp(arg, "temp_sdoff", 10) == 0) {
        print_eeprom_float((const float *) EEPROM_TEMPERATURE_OFFLINE_OFFSET);
    }
    else if (strncmp(arg, "hum_sdoff", 9) == 0) {
        print_eeprom_float((const float *) EEPROM_HUMIDITY_OFFLINE_OFFSET);
    }
    else if(strncmp(arg, "mqttsrv", 7) == 0) {
        print_eeprom_string((const char *) EEPROM_MQTT_SERVER_NAME);
    }
    else if(strncmp(arg, "mqttport", 8) == 0) {
        Serial.println(eeprom_read_dword((const uint32_t *) EEPROM_MQTT_PORT));
    }
    else if(strncmp(arg, "mqttuser", 8) == 0) {
        print_eeprom_string((const char *) EEPROM_MQTT_USERNAME);
    }
    else if(strncmp(arg, "mqttid", 6) == 0) {
        print_eeprom_string((const char *) EEPROM_MQTT_CLIENT_ID);
    }
    else if(strncmp(arg, "mqttauth", 8) == 0) {
        Serial.println(eeprom_read_byte((const uint8_t *) EEPROM_MQTT_AUTH));
    }
    else if(strncmp(arg, "opmode", 6) == 0) {
        print_eeprom_operational_mode(eeprom_read_byte((const uint8_t *) EEPROM_OPERATIONAL_MODE));
    }
    else if(strncmp(arg, "tempunit", 8) == 0) {
        print_eeprom_temperature_units();
    }
    else if(strncmp(arg, "backlight", 9) == 0) {
        print_eeprom_backlight();
    }
    else if(strncmp(arg, "timestamp", 9) == 0) {
        printCurrentTimestamp(NULL, NULL);
        Serial.println();
    }
    else if(strncmp(arg, "updatesrv", 9) == 0) {
        print_eeprom_update_server();
    }
    else if(strncmp(arg, "ntpsrv", 6) == 0) {
        print_eeprom_ntp_server();
    }
    else if(strncmp(arg, "updatefile", 10) == 0) {
        print_eeprom_string((const char *) EEPROM_UPDATE_FILENAME);
    }
    else if(strncmp(arg, "sampleint", 9) == 0) {
        Serial.println(eeprom_read_word((uint16_t *) EEPROM_SAMPLING_INTERVAL));
    }
    else if(strncmp(arg, "reportint", 9) == 0) {
        Serial.println(eeprom_read_word((uint16_t *) EEPROM_REPORTING_INTERVAL));
    }
    else if(strncmp(arg, "avgint", 6) == 0) {
        Serial.println(eeprom_read_word((uint16_t *) EEPROM_AVERAGING_INTERVAL));
    }
    else if(strncmp(arg, "altitude", 8) == 0) {
        Serial.println((int16_t) eeprom_read_word((uint16_t *) EEPROM_ALTITUDE_METERS));
    }
    else if(strncmp(arg, "latitude", 8) == 0) {
        Serial.println((int16_t) eeprom_read_float((float *) EEPROM_USER_LATITUDE_DEG));
    }
    else if(strncmp(arg, "longitude", 8) == 0) {
        Serial.println((int16_t) eeprom_read_float((float *) EEPROM_USER_LONGITUDE_DEG));
    }
    else if(strncmp(arg, "settings", 8) == 0) {
        // print all the settings to the screen in an orderly fashion
        Serial.println(F(" +-------------------------------------------------------------+"));
        Serial.println(F(" | Preferences/Options:                                        |"));
        Serial.println(F(" +-------------------------------------------------------------+"));
        Serial.print(F("    Operational Mode: "));
        print_eeprom_operational_mode(eeprom_read_byte((const uint8_t *) EEPROM_OPERATIONAL_MODE));
        Serial.print(F("    Temperature Units: "));
        print_eeprom_temperature_units();
        Serial.print(F("    Backlight Settings: "));
        print_eeprom_backlight();
        Serial.print(F("    Sensor Sampling Interval: "));
        Serial.print(eeprom_read_word((uint16_t *) EEPROM_SAMPLING_INTERVAL));
        Serial.println(F(" seconds"));
        Serial.print(F("    Sensor Averaging Interval: "));
        Serial.print(eeprom_read_word((uint16_t *) EEPROM_AVERAGING_INTERVAL));
        Serial.println(F(" seconds"));
        Serial.print(F("    Sensor Reporting Interval: "));
        Serial.print(eeprom_read_word((uint16_t *) EEPROM_REPORTING_INTERVAL));
        Serial.println(F(" seconds"));



        Serial.println(F(" +-------------------------------------------------------------+"));
        Serial.println(F(" | Location Settings:                                          |"));
        Serial.println(F(" +-------------------------------------------------------------+"));

        Serial.print(F("    User Location: "));
        if(eeprom_read_byte((uint8_t *) EEPROM_USER_LOCATION_EN) == 1) {
            Serial.println(F("Enabled"));
        }
        else {
            Serial.println(F("Disabled"));
        }

        Serial.print(F("    GPS: "));
        if(gps_installed) {
            Serial.println(F("Installed"));
        }
        else {
            Serial.println(F("Not installed"));
        }

        Serial.print(F("    User Latitude: "));
        print_latitude_settings();
        Serial.print(F("    User Longitude: "));
        print_longitude_settings();
        Serial.print(F("    User Altitude: "));
        print_altitude_settings();

        Serial.println(F(" +-------------------------------------------------------------+"));
        Serial.println(F(" | Network Settings:                                           |"));
        Serial.println(F(" +-------------------------------------------------------------+"));
        print_label_with_star_if_not_backed_up("MAC Address: ", BACKUP_STATUS_MAC_ADDRESS_BIT);
        print_eeprom_mac();
        uint8_t connect_method = eeprom_read_byte((const uint8_t *) EEPROM_CONNECT_METHOD);
        Serial.print(F("    Method: "));
        print_eeprom_connect_method();
        Serial.print(F("    SSID: "));
        print_eeprom_ssid();
        Serial.print(F("    Security Mode: "));
        print_eeprom_security_type();
        Serial.print(F("    IP Mode: "));
        print_eeprom_ipmode();
        Serial.print(F("    Update Server: "));
        print_eeprom_update_server();
        Serial.print(F("    Update Filename: "));
        print_eeprom_update_filename();
        Serial.print(F("    NTP Server: "));
        if(eeprom_read_byte((uint8_t *) EEPROM_USE_NTP) == 1) {
            print_eeprom_ntp_server();
        }
        else {
            Serial.println(F("Disabled"));
        }
        print_label_with_star_if_not_backed_up("NTP TZ Offset: ", BACKUP_STATUS_TIMEZONE_CALIBRATION_BIT);
        print_eeprom_float((const float *) EEPROM_NTP_TZ_OFFSET_HRS);

        Serial.println(F(" +-------------------------------------------------------------+"));
        Serial.println(F(" | MQTT Settings:                                              |"));
        Serial.println(F(" +-------------------------------------------------------------+"));
        Serial.print(F("    MQTT Server: "));
        print_eeprom_mqtt_server();
        Serial.print(F("    MQTT Port: "));
        Serial.println(eeprom_read_dword((const uint32_t *) EEPROM_MQTT_PORT));
        Serial.print(F("    MQTT Client ID: "));
        print_eeprom_mqtt_client_id();
        print_eeprom_mqtt_authentication();
        Serial.print(F("    MQTT Topic Prefix: "));
        print_eeprom_mqtt_topic_prefix();
        Serial.print(F("    MQTT Topic Suffix: "));
        print_eeprom_mqtt_topic_suffix();
        Serial.print(F("    MQTT Stay Connected: "));
        printYesOrNo(EEPROM_MQTT_STAY_CONNECTED, 1); // the value 1 is 'Yes', anything else is 'No'
        Serial.println(F(" +-------------------------------------------------------------+"));
        Serial.println(F(" | Credentials:                                                |"));
        Serial.println(F(" +-------------------------------------------------------------+"));
        print_label_with_star_if_not_backed_up("MQTT Password backed up? [* means no]", BACKUP_STATUS_MQTT_PASSSWORD_BIT);
        Serial.println();
        print_label_with_star_if_not_backed_up("Private key backed up? [* means no]", BACKUP_STATUS_PRIVATE_KEY_BIT);
        Serial.println();
        Serial.println(F(" +-------------------------------------------------------------+"));
        Serial.println(F(" | Sensor Calibrations:                                        |"));
        Serial.println(F(" +-------------------------------------------------------------+"));

        Serial.print(F("PM Sensor A: "));
        if(pm_enable_sensor_a) {
            Serial.print(F("Enabled"));
        }
        else {
            Serial.print(F("Disabled"));
        }
        Serial.println();

        Serial.print(F("PM Sensor B: "));
        if(pm_enable_sensor_b) {
            Serial.print(F("Enabled"));
        }
        else {
            Serial.print(F("Disabled"));
        }
        Serial.println();

        print_label_with_star_if_not_backed_up("PM1.0 Offset [ug/m^3]: ", BACKUP_STATUS_PARTICULATE_CALIBRATION_BIT);
        print_eeprom_float((const float *) EEPROM_PM1P0_CAL_OFFSET);
        print_label_with_star_if_not_backed_up("PM2.5 Offset [ug/m^3]: ", BACKUP_STATUS_PARTICULATE_CALIBRATION_BIT);
        print_eeprom_float((const float *) EEPROM_PM2P5_CAL_OFFSET);
        print_label_with_star_if_not_backed_up("PM10.0 Offset [ug/m^3]: ", BACKUP_STATUS_PARTICULATE_CALIBRATION_BIT);
        print_eeprom_float((const float *) EEPROM_PM10P0_CAL_OFFSET);
        print_label_with_star_if_not_backed_up("CO2 Offset [V]: ", BACKUP_STATUS_CO2_CALIBRATION_BIT);
        print_eeprom_float((const float *) EEPROM_CO2_CAL_OFFSET);
        Serial.print(F("    "));
        Serial.println(F("CO2 Baseline Voltage Characterization:"));
        print_baseline_voltage_characterization(EEPROM_CO2_BASELINE_VOLTAGE_TABLE);
        print_label_with_star_if_not_backed_up("CO2 Offset [ppm]: ", BACKUP_STATUS_ECO2_CALIBRATION_BIT);
        print_eeprom_float((const float *) EEPROM_ECO2_CAL_OFFSET);

        print_label_with_star_if_not_backed_up("CO2 Slope [ppm/ppm]: ", BACKUP_STATUS_ECO2_CALIBRATION_BIT);
        print_eeprom_float((const float *) EEPROM_ECO2_CAL_SLOPE);

        Serial.print(F("    "));
        Serial.println(F("CO2 Baseline Voltage Characterization:"));
        print_baseline_voltage_characterization(EEPROM_ECO2_BASELINE_VOLTAGE_TABLE);

        print_label_with_star_if_not_backed_up("TVOC Offset [ppb]: ", BACKUP_STATUS_TVOC_CALIBRATION_BIT);
        print_eeprom_float((const float *) EEPROM_TVOC_CAL_OFFSET);

        print_label_with_star_if_not_backed_up("TVOC Slope [ppb/ppb]: ", BACKUP_STATUS_TVOC_CALIBRATION_BIT);
        print_eeprom_float((const float *) EEPROM_TVOC_CAL_SLOPE);

        Serial.print(F("    "));
        Serial.println(F("TVOC Baseline Voltage Characterization:"));
        print_baseline_voltage_characterization(EEPROM_TVOC_BASELINE_VOLTAGE_TABLE);

        print_label_with_star_if_not_backed_up("Resistance Offset [ohms]: ", BACKUP_STATUS_TVOC_CALIBRATION_BIT);
        print_eeprom_float((const float *) EEPROM_RESISTANCE_CAL_OFFSET);

        print_label_with_star_if_not_backed_up("Resistance Slope [ohms/ohm]: ", BACKUP_STATUS_TVOC_CALIBRATION_BIT);
        print_eeprom_float((const float *) EEPROM_RESISTANCE_CAL_SLOPE);

        Serial.print(F("    "));
        Serial.println(F("Resistance Baseline Voltage Characterization:"));
        print_baseline_voltage_characterization(EEPROM_RESISTANCE_BASELINE_VOLTAGE_TABLE);

        char temp_reporting_offset_label[64] = {0};
        char temperature_units = (char) eeprom_read_byte((uint8_t *) EEPROM_TEMPERATURE_UNITS);
        snprintf(temp_reporting_offset_label, 63, "Temperature Reporting Offset [deg%c]: ", temperature_units);
        float temp_reporting_offset_degc = eeprom_read_float((float *) EEPROM_TEMPERATURE_OFFSET);
        float temperature_offset_display = temp_reporting_offset_degc;
        if(temperature_units == 'F') {
            temperature_offset_display = toFahrenheit(temp_reporting_offset_degc) - 32.0f;
        }
        print_label_with_star_if_not_backed_up((char * )temp_reporting_offset_label, BACKUP_STATUS_TEMPERATURE_CALIBRATION_BIT);
        Serial.println(temperature_offset_display, 2);

        print_label_with_star_if_not_backed_up("Humidity Reporting Offset [%]: ", BACKUP_STATUS_HUMIDITY_CALIBRATION_BIT);
        Serial.println(eeprom_read_float((float *) EEPROM_HUMIDITY_OFFSET), 2);

        memset(temp_reporting_offset_label, 0, 64);
        snprintf(temp_reporting_offset_label, 63, "Temperature Reporting Offline Offset [deg%c]: ", temperature_units);
        temp_reporting_offset_degc = eeprom_read_float((float *) EEPROM_TEMPERATURE_OFFLINE_OFFSET);
        temperature_offset_display = temp_reporting_offset_degc;
        if(temperature_units == 'F') {
            temperature_offset_display = toFahrenheit(temp_reporting_offset_degc) - 32.0f;
        }
        print_label_with_star_if_not_backed_up((char * )temp_reporting_offset_label, BACKUP_STATUS_TEMPERATURE_CALIBRATION_BIT);
        Serial.println(temperature_offset_display, 2);

        print_label_with_star_if_not_backed_up("Humidity Reporting Offline Offset [%]: ", BACKUP_STATUS_HUMIDITY_CALIBRATION_BIT);
        Serial.println(eeprom_read_float((float *) EEPROM_HUMIDITY_OFFLINE_OFFSET), 2);

        Serial.println(F(" +-------------------------------------------------------------+"));
        Serial.println(F(" | note: '*' next to label means the setting is not backed up. |"));
        Serial.println(F(" |     run 'backup all' when you are satisfied                 |"));
        Serial.println(F(" +-------------------------------------------------------------+"));
    }
    else {
        Serial.print(F("Error: Unexpected Variable Name \""));
        Serial.print(arg);
        Serial.println(F("\""));
    }
}

void initialize_eeprom_value(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    if (strncmp(arg, "mac", 3) == 0) {
        uint8_t _mac_address[6];
        if (!esp.getMacAddress((uint8_t *) _mac_address)) {
            Serial.println(F("Error: Could not retrieve MAC address from ESP8266"));
        }
        else {
            eeprom_write_block(_mac_address, (void *) EEPROM_MAC_ADDRESS, 6);
            recomputeAndStoreConfigChecksum();
        }
    }
    else {
        Serial.print(F("Error: Unexpected Variable Name \""));
        Serial.print(arg);
        Serial.println(F("\""));
    }
}

void restore(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    char blank[32] = {0};
    uint8_t tmp[32] = {0};
    boolean valid = true;

    // things that must have been backed up before restoring.
    // 1. MAC address              0x80
    // 2. MQTT Password            0x40
    // 3. Private Key              0x20

    uint16_t backup_check = eeprom_read_word((const uint16_t *) EEPROM_BACKUP_CHECK);

    if (strncmp(arg, "defaults", 8) == 0) {
        prompt();


        configInject(F("method direct\r"));
        configInject(F("security auto\r"));
        configInject(F("use dhcp\r"));
        configInject(F("opmode normal\r"));
        configInject(F("tempunit C\r"));
        configInject(F("altitude -1\r"));
        configInject(F("backlight 60\r"));
        configInject(F("backlight initon\r"));
        configInject(F("mqttsrv mqtt.wickeddevice.com\r"));
        configInject(F("mqttport 1883\r"));
        configInject(F("mqttauth enable\r"));
        // configInject(F("mqttuser wickeddevice\r"));
        configInject(F("mqttprefix /orgs/wd/aqe/\r"));
        configInject(F("mqttsuffix enable\r"));

        configInject(F("sampling 5, 60, 60\r")); // sample every 5 seconds, average over 1 minutes, report every minute
        configInject(F("restore particulate\r"));
        configInject(F("restore co2\r"));
        configInject(F("restore eco2\r"));
        configInject(F("restore tvoc\r"));
        configInject(F("restore res\r"));

        configInject(F("ntpsrv disable\r"));
        configInject(F("ntpsrv 0.airqualityegg.pool.ntp.org\r"));
        configInject(F("restore tz_off\r"));
        configInject(F("restore temp_off\r"));
        configInject(F("restore hum_off\r"));
        configInject(F("restore mqttpwd\r"));
        configInject(F("restore mqttid\r"));
        configInject(F("restore updatesrv\r"));
        configInject(F("restore updatefile\r"));
        configInject(F("restore key\r"));
        configInject(F("restore mac\r"));

        // copy the MQTT ID to the MQTT Username
        eeprom_read_block((void *) tmp, (const void *) EEPROM_MQTT_CLIENT_ID, 32);
        eeprom_write_block((void *) tmp, (void *) EEPROM_MQTT_USERNAME, 32);

        eeprom_write_block(blank, (void *) EEPROM_SSID, 32); // clear the SSID
        eeprom_write_block(blank, (void *) EEPROM_NETWORK_PWD, 32); // clear the Network Password
        mirrored_config_erase(); // erase the mirrored configuration, which will be restored next successful network connect

        Serial.println();
    }
    else if (strncmp(arg, "mac", 3) == 0) {
        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_MAC_ADDRESS_BIT)) {
            Serial.println(F("Error: MAC address must be backed up  "));
            Serial.println(F("       prior to executing a 'restore'."));
            return;
        }

        uint8_t _mac_address[6] = {0};
        char setmac_string[32] = {0};
        eeprom_read_block(_mac_address, (const void *) EEPROM_BACKUP_MAC_ADDRESS, 6);
        snprintf(setmac_string, 31,
                 "mac %02x:%02x:%02x:%02x:%02x:%02x\r",
                 _mac_address[0],
                 _mac_address[1],
                 _mac_address[2],
                 _mac_address[3],
                 _mac_address[4],
                 _mac_address[5]);

        configInject(setmac_string);
        Serial.println();
    }
    else if (strncmp("mqttpwd", arg, 7) == 0) {
        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_MQTT_PASSSWORD_BIT)) {
            Serial.println(F("Error: MQTT Password must be backed up  "));
            Serial.println(F("       prior to executing a 'restore'."));
            return;
        }

        eeprom_read_block(tmp, (const void *) EEPROM_BACKUP_MQTT_PASSWORD, 32);
        eeprom_write_block(tmp, (void *) EEPROM_MQTT_PASSWORD, 32);
    }
    else if (strncmp("mqttid", arg, 6) == 0) {
        // get the 8-byte unique electronic ID from the SHT25
        // convert it to a string, and store it to EEPROM
        uint8_t serial_number[8];
        sht25.getSerialNumber(serial_number);
        snprintf((char *) tmp, 31, "egg%02X%02X%02X%02X%02X%02X%02X%02X",
                 serial_number[0],
                 serial_number[1],
                 serial_number[2],
                 serial_number[3],
                 serial_number[4],
                 serial_number[5],
                 serial_number[6],
                 serial_number[7]);

        lowercase((char *) tmp); // for consistency with aqe v1 and airqualityegg.com assumptions

        eeprom_write_block(tmp, (void *) EEPROM_MQTT_CLIENT_ID, 32);
    }
    else if (strncmp("updatesrv", arg, 9) == 0) {
        eeprom_write_block("update.wickeddevice.com", (void *) EEPROM_UPDATE_SERVER_NAME, 32);
    }
    else if (strncmp("updatefile", arg, 10) == 0) {
        eeprom_write_block("aqev2_co2_pm_voc_esp", (void *) EEPROM_UPDATE_FILENAME, 32);
    }
    else if (strncmp("key", arg, 3) == 0) {
        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_PRIVATE_KEY_BIT)) {
            Serial.println(F("Error: Private key must be backed up  "));
            Serial.println(F("       prior to executing a 'restore'."));
            return;
        }

        eeprom_read_block(tmp, (const void *) EEPROM_BACKUP_PRIVATE_KEY, 32);
        eeprom_write_block(tmp, (void *) EEPROM_PRIVATE_KEY, 32);
    }

    else if (strncmp("particulate", arg, 11) == 0) {
        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_PARTICULATE_CALIBRATION_BIT)) {
            Serial.println(F("Error: Particulate calibration must be backed up  "));
            Serial.println(F("       prior to executing a 'restore'."));
            return;
        }

        eeprom_read_block(tmp, (const void *) EEPROM_BACKUP_PM1P0_CAL_OFFSET, 4);
        eeprom_write_block(tmp, (void *) EEPROM_PM1P0_CAL_OFFSET, 4);
        eeprom_read_block(tmp, (const void *) EEPROM_BACKUP_PM2P5_CAL_OFFSET, 4);
        eeprom_write_block(tmp, (void *) EEPROM_PM2P5_CAL_OFFSET, 4);
        eeprom_read_block(tmp, (const void *) EEPROM_BACKUP_PM10P0_CAL_OFFSET, 4);
        eeprom_write_block(tmp, (void *) EEPROM_PM10P0_CAL_OFFSET, 4);
    }
    else if (strncmp("co2", arg, 3) == 0) {
        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_CO2_CALIBRATION_BIT)) {
            Serial.println(F("Error: CO2 calibration must be backed up  "));
            Serial.println(F("       prior to executing a 'restore'."));
            return;
        }

        eeprom_read_block(tmp, (const void *) EEPROM_BACKUP_CO2_CAL_OFFSET, 4);
        eeprom_write_block(tmp, (void *) EEPROM_CO2_CAL_OFFSET, 4);
    }
    else if (strncmp("eco2", arg, 4) == 0) {
        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_ECO2_CALIBRATION_BIT)) {
            Serial.println(F("Error: ECO2 calibration must be backed up  "));
            Serial.println(F("       prior to executing a 'restore'."));
            return;
        }

        eeprom_read_block(tmp, (const void *) EEPROM_BACKUP_ECO2_CAL_OFFSET, 4);
        eeprom_write_block(tmp, (void *) EEPROM_ECO2_CAL_OFFSET, 4);
        eeprom_read_block(tmp, (const void *) EEPROM_BACKUP_ECO2_CAL_SLOPE, 4);
        eeprom_write_block(tmp, (void *) EEPROM_ECO2_CAL_SLOPE, 4);
    }
    else if (strncmp("tvoc", arg, 4) == 0) {

        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_TVOC_CALIBRATION_BIT)) {
            Serial.println(F("Error: TVOC calibration must be backed up  "));
            Serial.println(F("       prior to executing a 'restore'."));
            return;
        }

        eeprom_read_block(tmp, (const void *) EEPROM_BACKUP_TVOC_CAL_OFFSET, 4);
        eeprom_write_block(tmp, (void *) EEPROM_TVOC_CAL_OFFSET, 4);
        eeprom_read_block(tmp, (const void *) EEPROM_BACKUP_TVOC_CAL_SLOPE, 4);
        eeprom_write_block(tmp, (void *) EEPROM_TVOC_CAL_SLOPE, 4);
    }
    else if (strncmp("res", arg, 3) == 0) {

        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_TVOC_CALIBRATION_BIT)) {
            Serial.println(F("Error: TVOC calibration must be backed up  "));
            Serial.println(F("       prior to executing a 'restore'."));
            return;
        }

        eeprom_read_block(tmp, (const void *) EEPROM_BACKUP_RESISTANCE_CAL_OFFSET, 4);
        eeprom_write_block(tmp, (void *) EEPROM_RESISTANCE_CAL_OFFSET, 4);
        eeprom_read_block(tmp, (const void *) EEPROM_BACKUP_RESISTANCE_CAL_SLOPE, 4);
        eeprom_write_block(tmp, (void *) EEPROM_RESISTANCE_CAL_SLOPE, 4);
    }

    else if (strncmp("temp_off", arg, 8) == 0) {
        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_TEMPERATURE_CALIBRATION_BIT)) {
            Serial.println(F("Error: Temperature reporting offset should be backed up  "));
            Serial.println(F("       prior to executing a 'restore'. Setting to 0.0"));
            eeprom_write_float((float *) EEPROM_TEMPERATURE_OFFSET, 0.0f);
            eeprom_write_float((float *) EEPROM_TEMPERATURE_OFFLINE_OFFSET, 0.0f);
        }
        else {
            eeprom_read_block(tmp, (const void *) EEPROM_BACKUP_TEMPERATURE_OFFSET, 4);
            eeprom_write_block(tmp, (void *) EEPROM_TEMPERATURE_OFFSET, 4);
            eeprom_read_block(tmp, (const void *) EEPROM_BACKUP_TEMPERATURE_OFFLINE_OFFSET, 4);
            eeprom_write_block(tmp, (void *) EEPROM_TEMPERATURE_OFFLINE_OFFSET, 4);
        }
    }
    else if (strncmp("hum_off", arg, 7) == 0) {
        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_HUMIDITY_CALIBRATION_BIT)) {
            Serial.println(F("Warning: Humidity reporting offset should be backed up  "));
            Serial.println(F("         prior to executing a 'restore'. Setting to 0.0."));
            eeprom_write_float((float *) EEPROM_HUMIDITY_OFFSET, 0.0f);
            eeprom_write_float((float *) EEPROM_HUMIDITY_OFFLINE_OFFSET, 0.0f);
        }
        else {
            eeprom_read_block(tmp, (const void *) EEPROM_BACKUP_HUMIDITY_OFFSET, 4);
            eeprom_write_block(tmp, (void *) EEPROM_HUMIDITY_OFFSET, 4);
            eeprom_read_block(tmp, (const void *) EEPROM_BACKUP_HUMIDITY_OFFLINE_OFFSET, 4);
            eeprom_write_block(tmp, (void *) EEPROM_HUMIDITY_OFFLINE_OFFSET, 4);
        }
    }
    else if(strncmp("tz_off", arg, 6) == 0) {
        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_TIMEZONE_CALIBRATION_BIT)) {
            Serial.println(F("Warning: Timezone offset should be backed up  "));
            Serial.println(F("         prior to executing a 'restore'. Setting to 0.0."));
            eeprom_write_float((float *) EEPROM_NTP_TZ_OFFSET_HRS, 0.0f);
        }
        else {
            eeprom_read_block(tmp, (const void *) EEPROM_BACKUP_NTP_TZ_OFFSET_HRS, 4);
            eeprom_write_block(tmp, (void *) EEPROM_NTP_TZ_OFFSET_HRS, 4);
        }
    }
    else {
        valid = false;
        Serial.print(F("Error: Unexpected paramater name \""));
        Serial.print(arg);
        Serial.println(F("\""));
    }

    if (valid) {
        recomputeAndStoreConfigChecksum();
    }

}

void set_backlight_behavior(char * arg) {
    boolean valid = true;

    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    lowercase(arg);

    if(strncmp(arg, "initon", 6) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_BACKLIGHT_STARTUP, BACKLIGHT_ON_AT_STARTUP);
    }
    else if(strncmp(arg, "initoff", 7) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_BACKLIGHT_STARTUP, BACKLIGHT_OFF_AT_STARTUP);
    }
    else if(strncmp(arg, "alwayson", 8) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_BACKLIGHT_STARTUP, BACKLIGHT_ALWAYS_ON);
    }
    else if(strncmp(arg, "alwaysoff", 9) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_BACKLIGHT_STARTUP, BACKLIGHT_ALWAYS_OFF);
    }
    else {
        boolean arg_contains_only_digits = true;
        char * ptr = arg;
        uint16_t arglen = strlen(arg);
        for(uint16_t ii = 0; ii < arglen; ii++) {
            if(!isdigit(arg[ii])) {
                arg_contains_only_digits = true;
                break;
            }
        }

        if(arg_contains_only_digits) {
            uint32_t duration = (uint32_t) strtoul(arg, NULL, 10);
            if(duration < 0xFFFF) {
                eeprom_write_word((uint16_t *) EEPROM_BACKLIGHT_DURATION, (uint16_t) duration);
            }
        }
        else {
            valid = false;
            Serial.print(F("Error: Unexpected paramater name \""));
            Serial.print(arg);
            Serial.println(F("\""));
        }
    }

    if (valid) {
        recomputeAndStoreConfigChecksum();
    }
}

void altitude_command(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    char * endptr = NULL;
    trim_string(arg);
    int16_t l_altitude = (int16_t) strtol(arg, &endptr, 10);
    if(*endptr == NULL) {
        eeprom_write_word((uint16_t *) EEPROM_ALTITUDE_METERS, l_altitude);
        recomputeAndStoreConfigChecksum();
    }
    else {
        Serial.println(F("Error: altitude must be a numeric"));
    }
}

void sampling_command(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    uint16_t len = strlen(arg);
    uint8_t num_commas = 0;

    for(uint16_t ii = 0; ii < len; ii++) {
        // if any character is not a space, comma, or digit it's unparseable
        if((!isdigit(arg[ii])) && (arg[ii] != ' ') && (arg[ii] != ',')) {
            Serial.print(F("Error: Found invalid character '"));
            Serial.print((char) arg[ii]);
            Serial.print(F("'"));
            Serial.println();
            return;
        }

        if(arg[ii] == ',') {
            num_commas++;
        }
    }

    if(num_commas != 2) {
        Serial.print(F("Error: sampling expects exactly 3 values separated by commas, but received "));
        Serial.print(num_commas - 1);
        Serial.println();
        return;
    }

    // ok we have 3 numeric arguments separated by commas, parse them
    // ok we have six numeric arguments separated by commas, parse them
    char tmp[32] = {0};
    strncpy(tmp, arg, 31); // copy the string so you don't mutilate the argument
    char * token = strtok(tmp, ",");
    uint8_t token_number = 0;
    uint16_t l_sample_interval = 0;
    uint16_t l_averaging_interval = 0;
    uint16_t l_reporting_interval = 0;

    while (token != NULL) {
        switch(token_number++) {
        case 0:
            l_sample_interval = (uint16_t) strtoul(token, NULL, 10);
            if(l_sample_interval < 3) {
                Serial.print(F("Error: Sampling interval must be at least 3 [was "));
                Serial.print(l_sample_interval);
                Serial.print(F("]"));
                Serial.println();
                return;
            }
            break;
        case 1:
            l_averaging_interval = (uint16_t) strtoul(token, NULL, 10);
            if(l_averaging_interval < 1) {
                Serial.print(F("Error: Averaging interval must be greater than 0 [was "));
                Serial.print(l_averaging_interval);
                Serial.print(F("]"));
                Serial.println();
                return;
            }
            else if((l_averaging_interval % l_sample_interval) != 0) {
                Serial.print(F("Error: Averaging interval must be an integer multiple of the Sampling interval"));
                Serial.println();
                return;
            }
            else if((l_averaging_interval / l_sample_interval) > MAX_SAMPLE_BUFFER_DEPTH) {
                Serial.print(F("Error: Insufficient memory available for averaging interval @ sampling interval."));
                Serial.print(F("       Must require no more than "));
                Serial.print(MAX_SAMPLE_BUFFER_DEPTH);
                Serial.print(F(" samples, but requires"));
                Serial.print(l_averaging_interval / l_sample_interval);
                Serial.println(F(" samples"));
                Serial.println();
                return;
            }
            break;
        case 2:
            l_reporting_interval = (uint16_t) strtoul(token, NULL, 10);
            if(l_reporting_interval < 1) {
                Serial.print(F("Error: Reporting interval must be greater than 0 [was "));
                Serial.print(l_reporting_interval);
                Serial.print(F("]"));
                Serial.println();
                return;
            }
            else if((l_reporting_interval % l_sample_interval) != 0) {
                Serial.print(F("Error: Reporting interval must be an integer multiple of the Sampling interval"));
                Serial.println();
                return;
            }
            break;
        }

        token = strtok(NULL, ",");
    }

    // we got through all the checks! save these parameters to config memory
    eeprom_write_word((uint16_t *) EEPROM_SAMPLING_INTERVAL, l_sample_interval);
    eeprom_write_word((uint16_t *) EEPROM_REPORTING_INTERVAL, l_reporting_interval);
    eeprom_write_word((uint16_t *) EEPROM_AVERAGING_INTERVAL, l_averaging_interval);
    recomputeAndStoreConfigChecksum();
}

void AQE_set_datetime(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    uint16_t len = strlen(arg);
    uint8_t num_commas = 0;

    for(uint16_t ii = 0; ii < len; ii++) {
        // if any character is not a space, comma, or digit it's unparseable
        if((!isdigit(arg[ii])) && (arg[ii] != ' ') && (arg[ii] != ',')) {
            Serial.print(F("Error: Found invalid character '"));
            Serial.print((char) arg[ii]);
            Serial.print(F("'"));
            Serial.println();
            return;
        }

        if(arg[ii] == ',') {
            num_commas++;
        }
    }

    if(num_commas != 5) {
        Serial.print(F("Error: datetime expects exactly 6 values separated by commas, but received "));
        Serial.print(num_commas - 1);
        Serial.println();
        return;
    }

    // ok we have six numeric arguments separated by commas, parse them
    char tmp[32] = {0};
    strncpy(tmp, arg, 31); // copy the string so you don't mutilate the argument
    char * token = strtok(tmp, ",");
    uint8_t token_number = 0;
    uint8_t mo = 0, dy = 0, hr = 0, mn = 0, sc = 0;
    uint16_t yr = 0;

    while (token != NULL) {
        switch(token_number++) {
        case 0:
            yr = (uint16_t) strtoul(token, NULL, 10);
            if(yr < 2015) {
                Serial.print(F("Error: Year must be no earlier than 2015 [was "));
                Serial.print(yr);
                Serial.print(F("]"));
                Serial.println();
                return;
            }
            break;
        case 1:
            mo = (uint8_t) strtoul(token, NULL, 10);
            if(mo < 1 || mo > 12) {
                Serial.print(F("Error: Month must be between 1 and 12 [was "));
                Serial.print(mo);
                Serial.print(F("]"));
                Serial.println();
                return;
            }
            break;
        case 2:
            dy = (uint8_t) strtoul(token, NULL, 10);
            if(dy < 1 || dy > 31) {
                Serial.print(F("Error: Day must be between 1 and 31 [was "));
                Serial.print(dy);
                Serial.print(F("]"));
                Serial.println();
                return;
            }
            break;
        case 3:
            hr = (uint8_t) strtoul(token, NULL, 10);
            if(hr > 23) {
                Serial.print(F("Error: Hour must be between 0 and 23 [was "));
                Serial.print(hr);
                Serial.print(F("]"));
                Serial.println();
                return;
            }
            break;
        case 4:
            mn = (uint8_t) strtoul(token, NULL, 10);
            if(mn > 59) {
                Serial.print(F("Error: Minute must be between 0 and 59 [was "));
                Serial.print(mn);
                Serial.print(F("]"));
                Serial.println();
                return;
            }
            break;
        case 5:
            sc = (uint8_t) strtoul(token, NULL, 10);
            if(mn > 59) {
                Serial.print(F("Error: Second must be between 0 and 59 [was "));
                Serial.print(sc);
                Serial.print(F("]"));
                Serial.println();
                return;
            }
            break;
        }
        token = strtok(NULL, ",");
    }

    // if we have an RTC set the time in the RTC
    DateTime datetime(yr,mo,dy,hr,mn,sc);

    // it's not harmful to do this
    // even if the RTC is not present
    selectSlot3();
    rtc.adjust(datetime);

    // also clear the Oscillator Stop Flag
    // this should really be folded into the RTCLib code
    rtcClearOscillatorStopFlag();


    // at any rate sync the time to this
    setTime(datetime.unixtime());

}

void set_mac_address(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    uint8_t _mac_address[6] = {0};
    char tmp[32] = {0};

    strncpy(tmp, arg, 31); // copy the string so you don't mutilate the argument
    char * token = strtok(tmp, ":");
    uint8_t num_tokens = 0;

    // parse the argument string, expected to be of the form ab:01:33:51:c8:77
    while (token != NULL) {
        if(num_tokens > 5) {
            Serial.println(F("Error: Too many octets passed to setmac: "));
            Serial.print(F("       "));
            Serial.println(arg);
            Serial.println();
            return;
        }

        if ((strlen(token) == 2) && isxdigit(token[0]) && isxdigit(token[1]) && (num_tokens < 6)) {
            _mac_address[num_tokens++] = (uint8_t) strtoul(token, NULL, 16);
        }
        else {
            Serial.print(F("Error: MAC address parse error on input \""));
            Serial.print(arg);
            Serial.println(F("\""));
            return; // return early
        }


        token = strtok(NULL, ":");
    }

    if (num_tokens == 6) {
        eeprom_write_block(_mac_address, (void *) EEPROM_MAC_ADDRESS, 6);
        recomputeAndStoreConfigChecksum();
    }
    else {
        Serial.println(F("Error: MAC address must contain 6 bytes, with each separated by ':'"));
    }
}

void set_connection_method(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    lowercase(arg);
    boolean valid = true;
    if (strncmp(arg, "direct", 6) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_CONNECT_METHOD, CONNECT_METHOD_DIRECT);
    }
    else {
        Serial.print(F("Error: Invalid connection method entered - \""));
        Serial.print(arg);
        Serial.println(F("\""));
        Serial.println(F("       valid options are: 'direct'"));
        valid = false;
    }

    if (valid) {
        recomputeAndStoreConfigChecksum();
    }
}

void set_ssid(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    // we've reserved 32-bytes of EEPROM for an SSID
    // so the argument's length must be <= 31
    char ssid[33] = {0};
    uint16_t len = strlen(arg);
    if (len <= 32) {
        strncpy(ssid, arg, len);
        eeprom_write_block(ssid, (void *) EEPROM_SSID, 32);
        recomputeAndStoreConfigChecksum();
    }
    else {
        Serial.println(F("Error: SSID must be less than 33 characters in length"));
    }
}

void set_network_password(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    // we've reserved 32-bytes of EEPROM for a network password
    // so the argument's length must be <= 31
    char password[32] = {0};
    uint16_t len = strlen(arg);
    if (len < 32) {
        strncpy(password, arg, len);
        eeprom_write_block(password, (void *) EEPROM_NETWORK_PWD, 32);
        recomputeAndStoreConfigChecksum();
    }
    else {
        Serial.println(F("Error: Network password must be less than 32 characters in length"));
    }
}

void set_network_security_mode(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    boolean valid = true;
    if (strncmp("open", arg, 4) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_SECURITY_MODE, 0);
        set_network_password("");
    }
    else if (strncmp("wep", arg, 3) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_SECURITY_MODE, 1);
    }
    else if (strncmp("wpa2", arg, 4) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_SECURITY_MODE, 2);
    }
    else if (strncmp("wpa", arg, 3) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_SECURITY_MODE, 3);
    }
    else if(strncmp("auto", arg, 4) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_SECURITY_MODE, WLAN_SEC_AUTO);
    }
    else {
        Serial.print(F("Error: Invalid security mode entered - \""));
        Serial.print(arg);
        Serial.println(F("\""));
        Serial.println(F("       valid options are: 'open', 'wep', 'wpa', and 'wpa2'"));
        valid = false;
    }

    if (valid) {
        recomputeAndStoreConfigChecksum();
    }
}

void set_operational_mode(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    boolean valid = true;
    if (strncmp("normal", arg, 6) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_OPERATIONAL_MODE, SUBMODE_NORMAL);
    }
    else if (strncmp("offline", arg, 7) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_OPERATIONAL_MODE, SUBMODE_OFFLINE);
    }
    else {
        Serial.print(F("Error: Invalid operational mode entered - \""));
        Serial.print(arg);
        Serial.println(F("\""));
        Serial.println(F("       valid options are: 'normal', 'offline'"));
        valid = false;
    }

    if(valid) {
        recomputeAndStoreConfigChecksum();
    }
}

void set_temperature_units(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    if((strlen(arg) != 1) || ((arg[0] != 'C') && (arg[0] != 'F'))) {
        Serial.print(F("Error: temperature unit must be 'C' or 'F', but received '"));
        Serial.print(arg);
        Serial.println(F("'"));
        return;
    }

    eeprom_write_byte((uint8_t *) EEPROM_TEMPERATURE_UNITS, arg[0]);
    recomputeAndStoreConfigChecksum();
}

void set_static_ip_address(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    uint8_t _ip_address[4] = {0};
    uint8_t _gateway_ip[4] = {0};
    uint8_t _dns_ip[4] = {0};
    uint8_t _netmask[4] = {0};

    char tmp[128] = {0};
    strncpy(tmp, arg, 127); // copy the string so you don't mutilate the argument
    char * params[4] = {0};
    uint8_t param_idx = 0;

    // first tokenize on spaces, you should end up with four strings
    char * token = strtok(tmp, " ");
    uint8_t num_tokens = 0;

    while (token != NULL) {
        if(param_idx > 3) {
            Serial.println(F("Error: Too many parameters passed to staticip"));
            Serial.print(F("       "));
            Serial.println(arg);
            configInject(F("help staticip\r"));
            Serial.println();
            return;
        }
        params[param_idx++] = token;
        token = strtok(NULL, " ");
    }

    if(param_idx != 4) {
        Serial.println(F("Error: Too few parameters passed to staticip"));
        Serial.print(F("       "));
        Serial.println(arg);
        configInject(F("help staticip\r"));
        Serial.println();
        return;
    }

    for(param_idx = 0; param_idx < 4; param_idx++) {
        token = strtok(params[param_idx], ".");
        num_tokens = 0;

        // parse the parameter string, expected to be of the form 192.168.1.52
        while (token != NULL) {
            uint8_t tokenlen = strlen(token);
            if ((tokenlen < 4) && (num_tokens < 4)) {
                for (uint8_t ii = 0; ii < tokenlen; ii++) {
                    if (!isdigit(token[ii])) {
                        Serial.print(F("Error: IP address octets must be integer values [@param "));
                        Serial.print(param_idx + 1);
                        Serial.println(F("]"));
                        return;
                    }
                }
                uint32_t octet = (uint8_t) strtoul(token, NULL, 10);
                if (octet < 256) {
                    switch(param_idx) {
                    case 0:
                        _ip_address[num_tokens++] = octet;
                        break;
                    case 1:
                        _netmask[num_tokens++] = octet;
                        break;
                    case 2:
                        _gateway_ip[num_tokens++] = octet;
                        break;
                    case 3:
                        _dns_ip[num_tokens++] = octet;
                        break;
                    default:
                        break;
                    }
                }
                else {
                    Serial.print(F("Error: IP address octets must be between 0 and 255 inclusive [@param "));
                    Serial.print(param_idx + 1);
                    Serial.println(F("]"));
                    return;
                }
            }
            else {
                Serial.print(F("Error: IP address parse error on input \""));
                Serial.print(token);
                Serial.println(F("\""));
                return;
            }

            token = strtok(NULL, ".");
        }

        if (num_tokens != 4) {
            Serial.print(F("Error: IP Address must contain 4 valid octets separated by '.' [@param "));
            Serial.print(param_idx + 1);
            Serial.println(F("]"));
            return;
        }

    }

    // if we got this far, it means we got 4 valid IP addresses, and they
    // are stored in their respective local variables
    uint32_t ipAddress = esp.IpArrayToIpUint32((uint8_t *) _ip_address);
    uint32_t netMask = esp.IpArrayToIpUint32((uint8_t *) _netmask);
    uint32_t defaultGateway = esp.IpArrayToIpUint32((uint8_t *) _gateway_ip);

    eeprom_write_block(_ip_address, (void *) EEPROM_STATIC_IP_ADDRESS, 4);
    eeprom_write_block(_netmask, (void *) EEPROM_STATIC_NETMASK, 4);
    eeprom_write_block(_gateway_ip, (void *) EEPROM_STATIC_GATEWAY, 4);
    //eeprom_write_block(_dns_ip, (void *) EEPROM_STATIC_DNS, 4);
    recomputeAndStoreConfigChecksum();
}

void use_command(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    const uint8_t noip[4] = {0};
    if (strncmp("dhcp", arg, 4) == 0) {
        eeprom_write_block(noip, (void *) EEPROM_STATIC_IP_ADDRESS, 4);
        recomputeAndStoreConfigChecksum();
    }
    else if (strncmp("ntp", arg, 3) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_USE_NTP, 1);
        recomputeAndStoreConfigChecksum();
    }
    else {
        Serial.print(F("Error: Invalid parameter provided to 'use' command - \""));
        Serial.print(arg);
        Serial.println("\"");
        return;
    }
}

void force_command(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    if (strncmp("update", arg, 6) == 0) {
        Serial.println(F("Info: Erasing last flash page"));
        SUCCESS_MESSAGE_DELAY();
        invalidateSignature();
        configInject(F("opmode normal\r"));
        mode = SUBMODE_NORMAL;
        configInject(F("exit\r"));
    }
    else {
        Serial.print(F("Error: Invalid parameter provided to 'force' command - \""));
        Serial.print(arg);
        Serial.println(F("\""));
        return;
    }
}

boolean isSameOrAfter(char *ref, char *b) {
    // should return true if ref is <= ref lexically
    return strncmp(ref, b, 8) <= 0;
}

boolean isSameOrBefore(char *ref, char *b) {
    // should return true if ref is >= ref lexically
    return strncmp(ref, b, 8) >= 0;
}

void printDirectory(File dir, int numTabs, char * start = NULL, char * end = NULL) {
    for(;;) {
        File entry =  dir.openNextFile();
        if (! entry) {
            // no more files
            break;
        }

        char tmp[16] = {0};
        entry.getName(tmp, 16);
        if(tmp[0] != '.') {
            if (!entry.isDirectory() && start && end) {
                if(!isSameOrAfter(start, tmp) || !isSameOrBefore(end, tmp)) {
                    continue;
                }
            }

            Serial.print(tmp);

            for (uint8_t i=0; i<numTabs; i++) {
                Serial.print(F("\t"));
            }
            if (entry.isDirectory()) {
                Serial.println(F("/"));
                printDirectory(entry, numTabs+1, start, end);
            } else {
                // files have sizes, directories do not
                Serial.print(F("\t"));
                Serial.print(F("\t"));
                Serial.println(entry.size(), DEC);
            }
        }
        entry.close();
    }
}

void list_one_file(char * filename) {
    if (SD.exists(filename)) {
        Serial.println(filename);
    }
}

void list_command(char * arg) {
    if (strncmp("files", arg, 5) == 0) {
        if(init_sdcard_ok) {
            File root = SD.open("/", FILE_READ);
            printDirectory(root, 0);
            root.close();
        }
        else {
            Serial.println(F("Error: SD Card is not initialized, can't list files."));
        }
    }
    else { // otherwise treat it as a date range request
        // Serial.print(F("Error: Invalid parameter provided to 'list' command - \""));
        // Serial.print(arg);
        // Serial.println(F("\""));
        // fileop_command_delegate(arg, list_one_file);
        char *first_arg = NULL;
        char *second_arg = NULL;

        trim_string(arg);

        first_arg = strtok(arg, " ");
        second_arg = strtok(NULL, " ");
        if(init_sdcard_ok) {
            File root = SD.open("/", FILE_READ);
            printDirectory(root, 0, first_arg, second_arg);
            root.close();
        }
        else {
            Serial.println(F("Error: SD Card is not initialized, can't list files."));
        }
    }
}

void download_one_file(char * filename) {
    if(filename != NULL && init_sdcard_ok) {
        File dataFile = SD.open(filename, FILE_READ);
        char last_char_read = NULL;
        if (dataFile) {
            uint32_t byteCounter = 0;
            while (dataFile.available()) {
                last_char_read = dataFile.read();
                byteCounter++;
                if(isprint(last_char_read) || isspace(last_char_read)) {
                    Serial.write(last_char_read);
                    byteCounter = 0;
                }
                if(byteCounter > 1200) {
                    break;
                }
            }
            dataFile.close();
        }
        //else {
        //  Serial.print("Error: Failed to open file named \"");
        //  Serial.print(filename);
        //  Serial.print(F("\""));
        //}
        if(last_char_read != '\n') {
            Serial.println();
        }
    }
}

void crack_datetime_filename(char * filename, uint8_t target_array[4]) {
    char temp_str[3] = {0, 0, 0};
    for(uint8_t ii = 0; ii < 4; ii++) {
        strncpy(temp_str, &(filename[ii * 2]), 2);
        target_array[ii] = atoi(temp_str);
    }

    target_array[0] += 30; // YY is offset from 2000, but epoch time is offset from 1970
}

void make_datetime_filename(uint8_t src_array[4], char * target_filename, uint8_t max_len) {
    snprintf(target_filename, max_len, "%02d%02d%02d%02d.csv",
             src_array[0] - 30, // YY is offset from 2000, but epoch time is offset from 1970
             src_array[1],
             src_array[2],
             src_array[3]);
}

void advanceByOneHour(uint8_t src_array[4]) {

    tmElements_t tm;
    tm.Year   = src_array[0];
    tm.Month  = src_array[1];
    tm.Day    = src_array[2];
    tm.Wday   = 0;
    tm.Hour   = src_array[3];
    tm.Minute = 0;
    tm.Second = 0;

    time_t seconds_since_epoch = makeTime(tm);
    seconds_since_epoch += SECS_PER_HOUR;
    breakTime(seconds_since_epoch, tm);

    src_array[0] = tm.Year;
    src_array[1] = tm.Month;
    src_array[2] = tm.Day;
    src_array[3] = tm.Hour;
}

int8_t compareCrackedDates(uint8_t * date1, uint8_t * date2) {
    uint32_t d1 = 0;
    uint32_t d2 = 0;
    for(int8_t ii = 0; ii < 4; ii++) {
        d1 |= date1[ii];
        d2 |= date2[ii];
        if (ii != 3) {
            d1 <<= 8;
            d2 <<= 8;
        }
    }

    int8_t ret = (d1 == d2) ? 0 : ( (d1 > d2) ? 1 : -1);

    // Serial.println(d1, HEX);
    // Serial.println(d2, HEX);
    // Serial.println(ret);

    return ret;
}

// does the behavior of executing the one_file_function on a single file
// or on each file in a range of files
void fileop_command_delegate(char *arg, void (*one_file_function)(char *))
{
    char *first_arg = NULL;
    char *second_arg = NULL;

    trim_string(arg);

    first_arg = strtok(arg, " ");
    second_arg = strtok(NULL, " ");

    if (second_arg == NULL)
    {
        one_file_function(first_arg);
    }
    else
    {
        uint8_t cur_date[4] = {0, 0, 0, 0};
        uint8_t end_date[4] = {0, 0, 0, 0};
        crack_datetime_filename(first_arg, cur_date);
        crack_datetime_filename(second_arg, end_date);

        // starting from cur_date, download the file with that name
        char cur_date_filename[16] = {0};
        boolean finished_last_file = false;
        unsigned long previousMillis = millis();
        const long interval = 1000;
        while (!finished_last_file)
        {
            unsigned long currentMillis = millis();
            if (currentMillis - previousMillis >= interval)
            {
                previousMillis = currentMillis;
                petWatchdog();
            }
            memset(cur_date_filename, 0, 16);
            make_datetime_filename(cur_date, cur_date_filename, 15);

            if (SD.exists(cur_date_filename))
            {
                one_file_function(cur_date_filename);
            }

            if ( compareCrackedDates(end_date, cur_date) <= 0 )
            {
                finished_last_file = true;
            }
            else
            {
                advanceByOneHour(cur_date);
            }
        }
    }
    delayForWatchdog();
}

void download_command(char *arg)
{
    Serial.println(header_row);
    fileop_command_delegate(arg, download_one_file);
    Serial.println("Info: Done downloading.");
}

void delete_one_file(char * filename) {
    if(filename != NULL && init_sdcard_ok) {
        if (SD.remove(filename)) {
            Serial.print("Info: Removed file named \"");
            Serial.print(filename);
            Serial.println(F("\""));
        }
//    else {
//      Serial.print("Error: Failed to delete file named \"");
//      Serial.print(filename);
//      Serial.println(F("\""));
//    }
    }
}

void delete_command(char * arg) {
    fileop_command_delegate(arg, delete_one_file);
    Serial.println("Info: Done deleting.");
}

void set_mqtt_password(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    // we've reserved 32-bytes of EEPROM for a MQTT password
    // so the argument's length must be <= 31
    char password[32] = {0};
    uint16_t len = strlen(arg);
    if (len < 32) {
        strncpy(password, arg, len);
        eeprom_write_block(password, (void *) EEPROM_MQTT_PASSWORD, 32);
        recomputeAndStoreConfigChecksum();
    }
    else {
        Serial.println(F("Error: MQTT password must be less than 32 characters in length"));
    }
}

void set_mqtt_topic_prefix(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    // we've reserved 64-bytes of EEPROM for a MQTT prefix
    // so the argument's length must be <= 63
    char prefix[64] = {0};
    uint16_t len = strlen(arg);
    if (len < 64) {
        strncpy(prefix, arg, len);
        eeprom_write_block(prefix, (void *) EEPROM_MQTT_TOPIC_PREFIX, 64);
        recomputeAndStoreConfigChecksum();
    }
    else {
        Serial.println(F("Error: MQTT prefix must be less than 64 characters in length"));
    }
}

void set_user_latitude(char * arg) {
    set_float_param(arg, (float *) EEPROM_USER_LATITUDE_DEG, NULL);
}

void set_user_longitude(char * arg) {
    set_float_param(arg, (float *) EEPROM_USER_LONGITUDE_DEG, NULL);
}

void set_user_location_enable(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    lowercase(arg);

    if (strcmp(arg, "enable") == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_USER_LOCATION_EN, 1);
        recomputeAndStoreConfigChecksum();
    }
    else if (strcmp(arg, "disable") == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_USER_LOCATION_EN, 0);
        recomputeAndStoreConfigChecksum();
    }
    else {
        Serial.print(F("Error: expected 'enable' or 'disable' but got '"));
        Serial.print(arg);
        Serial.println("'");
    }

    user_location_override = eeprom_read_byte((uint8_t *) EEPROM_USER_LOCATION_EN) == 1 ? true : false;
}




void topic_suffix_config(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    lowercase(arg);

    if (strcmp(arg, "enable") == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_MQTT_TOPIC_SUFFIX_ENABLED, 1);
        recomputeAndStoreConfigChecksum();
    }
    else if (strcmp(arg, "disable") == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_MQTT_TOPIC_SUFFIX_ENABLED, 0);
        recomputeAndStoreConfigChecksum();
    }
    else {
        Serial.print(F("Error: expected 'enable' or 'disable' but got '"));
        Serial.print(arg);
        Serial.println("'");
    }
}


void set_mqtt_server(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    // we've reserved 32-bytes of EEPROM for an MQTT server name
    // so the argument's length must be <= 31
    char server[32] = {0};
    uint16_t len = strlen(arg);
    if (len < 32) {
        strncpy(server, arg, len);
        eeprom_write_block(server, (void *) EEPROM_MQTT_SERVER_NAME, 32);
        recomputeAndStoreConfigChecksum();
    }
    else {
        Serial.println(F("Error: MQTT server name must be less than 32 characters in length"));
    }
}

void set_mqtt_username(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    // we've reserved 32-bytes of EEPROM for an MQTT username
    // so the argument's length must be <= 31
    char username[32] = {0};
    uint16_t len = strlen(arg);
    if (len < 32) {
        strncpy(username, arg, len);
        eeprom_write_block(username, (void *) EEPROM_MQTT_USERNAME, 32);
        recomputeAndStoreConfigChecksum();
    }
    else {
        Serial.println(F("Error: MQTT username must be less than 32 characters in length"));
    }
}
void set_mqtt_client_id(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    // we've reserved 32-bytes of EEPROM for an MQTT client ID
    // but in fact an MQTT client ID must be between 1 and 23 characters
    // and must start with an letter
    char client_id[32] = {0};

    uint16_t len = strlen(arg);
    if ((len >= 1) && (len <= 23)) {
        strncpy(client_id, arg, len);
        eeprom_write_block(client_id, (void *) EEPROM_MQTT_CLIENT_ID, 32);
        recomputeAndStoreConfigChecksum();
    }
    else {
        Serial.println(F("Error: MQTT client ID must be less between 1 and 23 characters in length"));
    }
}

void set_mqtt_authentication(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    if (strncmp("enable", arg, 6) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_MQTT_AUTH, 1);
        recomputeAndStoreConfigChecksum();
    }
    else if(strncmp("disable", arg, 7) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_MQTT_AUTH, 0);
        recomputeAndStoreConfigChecksum();
    }
    else {
        Serial.print(F("Error: Invalid parameter provided to 'mqttauth' command - \""));
        Serial.print(arg);
        Serial.println("\", must be either \"enable\" or \"disable\"");
        return;
    }
}

void set_mqtt_port(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    uint16_t len = strlen(arg);
    boolean valid = true;

    for(uint16_t ii = 0; ii < len; ii++) {
        if(!isdigit(arg[ii])) {
            valid = false;
        }
    }

    uint32_t port = 0xFFFFFFFF;
    if(valid) {
        port = (uint32_t) strtoul(arg, NULL, 10);
    }

    if(valid && (port < 0x10000) && (port > 0)) {
        eeprom_write_dword((uint32_t *) EEPROM_MQTT_PORT, port);
        recomputeAndStoreConfigChecksum();
    }
    else {
        Serial.print(F("Error: Invalid parameter provided to 'mqttport' command - \""));
        Serial.print(arg);
        Serial.println("\", must a number between 1 and 65535 inclusive");
        return;
    }
}

void set_update_server_name(char * arg) {

    static char server[32] = {0};
    memset(server, 0, 32);

    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    trim_string(arg); // leading and trailing spaces are not relevant
    uint16_t len = strlen(arg);

    // we've reserved 32-bytes of EEPROM for an update server name
    // so the argument's length must be <= 31
    if (len < 32) {
        strncpy(server, arg, 31); // copy the argument as a case-sensitive server name
        lowercase(arg);           // in case it's the "disable" special case, make arg case insensitive

        if(strncmp(arg, "disable", 7) == 0) {
            memset(server, 0, 32); // wipe out the update server name
        }

        eeprom_write_block(server, (void *) EEPROM_UPDATE_SERVER_NAME, 32);
        recomputeAndStoreConfigChecksum();
    }
    else {
        Serial.println(F("Error: Update server name must be less than 32 characters in length"));
    }
}

void set_update_filename(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    // we've reserved 32-bytes of EEPROM for an update server name
    // so the argument's length must be <= 31
    char filename[32] = {0};
    uint16_t len = strlen(arg);
    if (len < 32) {
        strncpy(filename, arg, len);
        eeprom_write_block(filename, (void *) EEPROM_UPDATE_FILENAME, 32);
        recomputeAndStoreConfigChecksum();
    }
    else {
        Serial.println(F("Error: Update filename must be less than 32 characters in length"));
    }
}

void set_ntp_server(char * arg) {

    static char server[32] = {0};
    memset(server, 0, 32);

    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    trim_string(arg); // leading and trailing spaces are not relevant
    uint16_t len = strlen(arg);

    // we've reserved 32-bytes of EEPROM for an NTP server name
    // so the argument's length must be <= 31
    if (len < 32) {
        strncpy(server, arg, 31); // copy the argument as a case-sensitive server name
        lowercase(arg);           // in case it's the "disable" special case, make arg case insensitive

        if(strncmp(arg, "disable", 7) == 0) {
            eeprom_write_byte((uint8_t *) EEPROM_USE_NTP, 0);
            memset(server, 0, 32); // wipe out the NTP server name
        }

        eeprom_write_block(server, (void *) EEPROM_NTP_SERVER_NAME, 32);
        recomputeAndStoreConfigChecksum();
    }
    else {
        Serial.println(F("Error: NTP server name must be less than 32 characters in length"));
    }
}

void backup(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    boolean valid = true;
    char tmp[32] = {0};
    uint16_t backup_check = eeprom_read_word((const uint16_t *) EEPROM_BACKUP_CHECK);

    if (strncmp("mac", arg, 3) == 0) {
        configInject(F("init mac\r")); // make sure the ESP8266 mac address is in EEPROM
        Serial.println();
        eeprom_read_block(tmp, (const void *) EEPROM_MAC_ADDRESS, 6);
        eeprom_write_block(tmp, (void *) EEPROM_BACKUP_MAC_ADDRESS, 6);

        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_MAC_ADDRESS_BIT)) {
            CLEAR_BIT(backup_check, BACKUP_STATUS_MAC_ADDRESS_BIT);
            eeprom_write_word((uint16_t *) EEPROM_BACKUP_CHECK, backup_check);
        }
    }
    else if (strncmp("mqttpwd", arg, 7) == 0) {
        eeprom_read_block(tmp, (const void *) EEPROM_MQTT_PASSWORD, 32);
        eeprom_write_block(tmp, (void *) EEPROM_BACKUP_MQTT_PASSWORD, 32);

        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_MQTT_PASSSWORD_BIT)) {
            CLEAR_BIT(backup_check, BACKUP_STATUS_MQTT_PASSSWORD_BIT);
            eeprom_write_word((uint16_t *) EEPROM_BACKUP_CHECK, backup_check);
        }
    }
    else if (strncmp("key", arg, 3) == 0) {
        eeprom_read_block(tmp, (const void *) EEPROM_PRIVATE_KEY, 32);
        eeprom_write_block(tmp, (void *) EEPROM_BACKUP_PRIVATE_KEY, 32);

        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_PRIVATE_KEY_BIT)) {
            CLEAR_BIT(backup_check, BACKUP_STATUS_PRIVATE_KEY_BIT);
            eeprom_write_word((uint16_t *) EEPROM_BACKUP_CHECK, backup_check);
        }
    }

    else if (strncmp("particulate", arg, 11) == 0) {
        eeprom_read_block(tmp, (const void *) EEPROM_PM1P0_CAL_OFFSET, 4);
        eeprom_write_block(tmp, (void *) EEPROM_BACKUP_PM1P0_CAL_OFFSET, 4);
        eeprom_read_block(tmp, (const void *) EEPROM_PM2P5_CAL_OFFSET, 4);
        eeprom_write_block(tmp, (void *) EEPROM_BACKUP_PM2P5_CAL_OFFSET, 4);
        eeprom_read_block(tmp, (const void *) EEPROM_PM10P0_CAL_OFFSET, 4);
        eeprom_write_block(tmp, (void *) EEPROM_BACKUP_PM10P0_CAL_OFFSET, 4);

        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_PARTICULATE_CALIBRATION_BIT)) {
            CLEAR_BIT(backup_check, BACKUP_STATUS_PARTICULATE_CALIBRATION_BIT);
            eeprom_write_word((uint16_t *) EEPROM_BACKUP_CHECK, backup_check);
        }
    }
    else if (strncmp("co2", arg, 3) == 0) {
        eeprom_read_block(tmp, (const void *) EEPROM_CO2_CAL_OFFSET, 4);
        eeprom_write_block(tmp, (void *) EEPROM_BACKUP_CO2_CAL_OFFSET, 4);

        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_CO2_CALIBRATION_BIT)) {
            CLEAR_BIT(backup_check, BACKUP_STATUS_CO2_CALIBRATION_BIT);
            eeprom_write_word((uint16_t *) EEPROM_BACKUP_CHECK, backup_check);
        }
    }
    else if (strncmp("tvoc", arg, 4) == 0) {
        float current_value = eeprom_read_float((const float *) EEPROM_ECO2_CAL_OFFSET);
        if(isnan(current_value)) {
            eeprom_write_float((float *) EEPROM_ECO2_CAL_OFFSET, 0.0f);
        }
        current_value = eeprom_read_float((const float *) EEPROM_ECO2_CAL_SLOPE);
        if(isnan(current_value)) {
            eeprom_write_float((float *) EEPROM_ECO2_CAL_SLOPE, 1.0f);
        }

        current_value = eeprom_read_float((const float *) EEPROM_TVOC_CAL_OFFSET);
        if(isnan(current_value)) {
            eeprom_write_float((float *) EEPROM_TVOC_CAL_OFFSET, 0.0f);
        }
        current_value = eeprom_read_float((const float *) EEPROM_TVOC_CAL_SLOPE);
        if(isnan(current_value)) {
            eeprom_write_float((float *) EEPROM_TVOC_CAL_SLOPE, 1.0f);
        }

        current_value = eeprom_read_float((const float *) EEPROM_RESISTANCE_CAL_OFFSET);
        if(isnan(current_value)) {
            eeprom_write_float((float *) EEPROM_RESISTANCE_CAL_OFFSET, 0.0f);
        }
        current_value = eeprom_read_float((const float *) EEPROM_RESISTANCE_CAL_SLOPE);
        if(isnan(current_value)) {
            eeprom_write_float((float *) EEPROM_RESISTANCE_CAL_SLOPE, 1.0f);
        }

        eeprom_read_block(tmp, (const void *) EEPROM_TVOC_CAL_OFFSET, 4);
        eeprom_write_block(tmp, (void *) EEPROM_BACKUP_TVOC_CAL_OFFSET, 4);

        eeprom_read_block(tmp, (const void *) EEPROM_TVOC_CAL_SLOPE, 4);
        eeprom_write_block(tmp, (void *) EEPROM_BACKUP_TVOC_CAL_SLOPE, 4);

        eeprom_read_block(tmp, (const void *) EEPROM_ECO2_CAL_OFFSET, 4);
        eeprom_write_block(tmp, (void *) EEPROM_BACKUP_ECO2_CAL_OFFSET, 4);

        eeprom_read_block(tmp, (const void *) EEPROM_ECO2_CAL_SLOPE, 4);
        eeprom_write_block(tmp, (void *) EEPROM_BACKUP_ECO2_CAL_SLOPE, 4);

        eeprom_read_block(tmp, (const void *) EEPROM_RESISTANCE_CAL_OFFSET, 4);
        eeprom_write_block(tmp, (void *) EEPROM_BACKUP_RESISTANCE_CAL_OFFSET, 4);

        eeprom_read_block(tmp, (const void *) EEPROM_RESISTANCE_CAL_SLOPE, 4);
        eeprom_write_block(tmp, (void *) EEPROM_BACKUP_RESISTANCE_CAL_SLOPE, 4);

        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_TVOC_CALIBRATION_BIT)) {
            CLEAR_BIT(backup_check, BACKUP_STATUS_TVOC_CALIBRATION_BIT);
            eeprom_write_word((uint16_t *) EEPROM_BACKUP_CHECK, backup_check);
        }

        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_ECO2_CALIBRATION_BIT)) {
            CLEAR_BIT(backup_check, BACKUP_STATUS_ECO2_CALIBRATION_BIT);
            eeprom_write_word((uint16_t *) EEPROM_BACKUP_CHECK, backup_check);
        }
    }

    else if (strncmp("temp", arg, 4) == 0) {
        eeprom_read_block(tmp, (const void *) EEPROM_TEMPERATURE_OFFSET, 4);
        eeprom_write_block(tmp, (void *) EEPROM_BACKUP_TEMPERATURE_OFFSET, 4);
        eeprom_read_block(tmp, (const void *) EEPROM_TEMPERATURE_OFFLINE_OFFSET, 4);
        eeprom_write_block(tmp, (void *) EEPROM_BACKUP_TEMPERATURE_OFFLINE_OFFSET, 4);

        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_TEMPERATURE_CALIBRATION_BIT)) {
            CLEAR_BIT(backup_check, BACKUP_STATUS_TEMPERATURE_CALIBRATION_BIT);
            eeprom_write_word((uint16_t *) EEPROM_BACKUP_CHECK, backup_check);
        }
    }
    else if (strncmp("hum", arg, 3) == 0) {
        eeprom_read_block(tmp, (const void *) EEPROM_HUMIDITY_OFFSET, 4);
        eeprom_write_block(tmp, (void *) EEPROM_BACKUP_HUMIDITY_OFFSET, 4);
        eeprom_read_block(tmp, (const void *) EEPROM_HUMIDITY_OFFLINE_OFFSET, 4);
        eeprom_write_block(tmp, (void *) EEPROM_BACKUP_HUMIDITY_OFFLINE_OFFSET, 4);

        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_HUMIDITY_CALIBRATION_BIT)) {
            CLEAR_BIT(backup_check, BACKUP_STATUS_HUMIDITY_CALIBRATION_BIT);
            eeprom_write_word((uint16_t *) EEPROM_BACKUP_CHECK, backup_check);
        }
    }
    else if (strncmp("tz", arg, 2) == 0) {
        eeprom_read_block(tmp, (const void *) EEPROM_NTP_TZ_OFFSET_HRS, 4);
        eeprom_write_block(tmp, (void *) EEPROM_BACKUP_NTP_TZ_OFFSET_HRS, 4);

        if (!BIT_IS_CLEARED(backup_check, BACKUP_STATUS_TIMEZONE_CALIBRATION_BIT)) {
            CLEAR_BIT(backup_check, BACKUP_STATUS_TIMEZONE_CALIBRATION_BIT);
            eeprom_write_word((uint16_t *) EEPROM_BACKUP_CHECK, backup_check);
        }
    }
    else if (strncmp("all", arg, 3) == 0) {
        valid = false;
        configInject(F("backup mqttpwd\r"));
        configInject(F("backup key\r"));

        configInject(F("backup particulate\r"));
        configInject(F("backup co2\r"));
        configInject(F("backup tvoc\r"));

        configInject(F("backup temp\r"));
        configInject(F("backup hum\r"));
        configInject(F("backup mac\r"));
        configInject(F("backup tz\r"));
        Serial.println();
    }
    else {
        valid = false;
        Serial.print(F("Error: Invalid parameter provided to 'backup' command - \""));
        Serial.print(arg);
        Serial.println(F("\""));
    }

    if (valid) {
        recomputeAndStoreConfigChecksum();
    }
}

void printYesOrNo(uint8_t eeprom_address) {
    printYesOrNo(eeprom_address, 2); // use normal c-rules
}

void printYesOrNo(uint8_t eeprom_address, uint8_t fixed_value) {
    uint8_t value = eeprom_read_byte((uint8_t *) eeprom_address);
    const char * yes = "Yes";
    const char * no = "No";
    if(fixed_value == 0) {
        if(value == 0) {
            Serial.println(no);
        }
        else {
            Serial.println(yes);
        }
    }
    else if(fixed_value == 1) { // 1 is true, anything else is false
        if(value == 1) {
            Serial.println(yes);
        }
        else {
            Serial.println(no);
        }
    }
    else { // normal C rules
        if(value != 0) {
            Serial.println(yes);
        }
        else {
            Serial.println(no);
        }
    }
}

boolean convertStringToFloat(char * str_to_convert, float * target) {
    char * end_ptr;
    *target = strtod(str_to_convert, &end_ptr);
    if (end_ptr != (str_to_convert + strlen(str_to_convert))) {
        return false;
    }
    return true;
}

void set_float_param(char * arg, float * eeprom_address, float (*conversion)(float)) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    // read the value at that address from eeprom
    float current_value = eeprom_read_float((const float *) eeprom_address);

    float value = 0.0;
    if (convertStringToFloat(arg, &value)) {
        if (conversion) {
            value = conversion(value);
        }

        if(current_value != value) {
            eeprom_write_float(eeprom_address, value);
            recomputeAndStoreConfigChecksum();
        }
    }
    else {
        Serial.print(F("Error: Failed to convert string \""));
        Serial.print(arg);
        Serial.println(F("\" to decimal number."));
    }
}

void set_ntp_timezone_offset(char * arg) {
    set_float_param(arg, (float *) EEPROM_NTP_TZ_OFFSET_HRS, NULL);
}

boolean add_baseline_voltage_characterization(char * arg, uint32_t eeprom_table_base_address) {
    // there should be three values provided
    // a temperature in degC        [float]
    // a slope in Volts / degC      [float]
    // an intercept in Volts        [float]
    // tokenization should already be in progress when this function is called

    char * token = strtok(NULL, " "); // advance to the next token
    if(token == NULL) {
        Serial.println(F("Error: No temperature provided"));
        return false;
    }

    if (!convertStringToFloat(token, &(baseline_voltage_struct.temperature_degC))) {
        Serial.print(F("Error: Failed to convert temperature string \""));
        Serial.print(token);
        Serial.println(F("\" to decimal number."));
        return false;
    }

    token = strtok(NULL, " "); // advance to the next token
    if(token == NULL) {
        Serial.println(F("Error: No slope provided"));
        return false;
    }

    if (!convertStringToFloat(token, &(baseline_voltage_struct.slope_volts_per_degC))) {
        Serial.print(F("Error: Failed to convert slope string \""));
        Serial.print(token);
        Serial.println(F("\" to decimal number."));
        return false;
    }


    token = strtok(NULL, " "); // advance to the next token
    if(token == NULL) {
        Serial.println(F("Error: No intercpet provided"));
        return false;
    }

    if (!convertStringToFloat(token, &(baseline_voltage_struct.intercept_volts))) {
        Serial.print(F("Error: Failed to convert intercept string \""));
        Serial.print(token);
        Serial.println(F("\" to decimal number."));
        return false;
    }

    // if you got this far, you've managed to parse three numbers and save them to the temp struct
    // and we should commit the results to EEPROM, and we should do so at the first available
    // index where the temperature is currently NaN, if there are no such spaces then report an
    // error instructing the user to clear the table because it's full
    baseline_voltage_t tmp;

    // keep track of the highest temperature we've seen until we find an empty slot
    // and enforce the constraint that the temperature's in the table are monotonically increasing
    // any large negative number would do to initialize, but this is as cold as it gets, because Physics
    float temperature_of_first_valid_entry_degC = -273.15;
    boolean empty_location_found = false;
    for(uint8_t ii = 0; ii < 5; ii++) {
        eeprom_read_block((void *) &tmp, (void *) (eeprom_table_base_address + (ii*sizeof(baseline_voltage_t))), sizeof(baseline_voltage_t));
        if(!valid_temperature_characterization_struct(&tmp)) {
            // ok we've found a slot where our new entry might be able to go, but first we have to enforce the monotinicity constraint
            // so that the search process is easier later
            if(temperature_of_first_valid_entry_degC >= baseline_voltage_struct.temperature_degC) {
                // monotonicity contraint violation
                Serial.println(F("Error: Entries must be added in increasing order of temperature"));
                return false;
            }
            empty_location_found = true;
            // write the newly parsed struct at this index
            eeprom_write_block((void *) &baseline_voltage_struct, (void *) (eeprom_table_base_address + (ii*sizeof(baseline_voltage_t))), sizeof(baseline_voltage_t));
            break;
        }
        else {
            temperature_of_first_valid_entry_degC = tmp.temperature_degC;
        }
    }

    if(!empty_location_found) {
        Serial.print(F("Error: Table is full, please run '"));
        if(eeprom_table_base_address == EEPROM_ECO2_BASELINE_VOLTAGE_TABLE) {
            Serial.print(F("eco2"));
        }
        else if(eeprom_table_base_address == EEPROM_TVOC_BASELINE_VOLTAGE_TABLE) {
            Serial.print(F("tvoc"));
        }
        else if(eeprom_table_base_address == EEPROM_RESISTANCE_BASELINE_VOLTAGE_TABLE) {
            Serial.print(F("res"));
        }
        else {
            Serial.print(F("co2"));
        }

        Serial.print(F("_blv clear' first"));
        Serial.println();
        return false;
    }

    return true;

}

void resetSensors() {
    digitalWrite(sensor_enable, LOW);
    pinMode(9, INPUT);  // co2 serial
    pinMode(10, INPUT); // co2 serial
    pinMode(A4, INPUT); // pm serial
    pinMode(A5, INPUT); // pm serial
    pinMode(A7, INPUT); // pm serial

    delay(1000);
    watchdogForceReset();
}
void set_pm1p0_offset(char * arg) {
    set_float_param(arg, (float *) EEPROM_PM1P0_CAL_OFFSET, 0);
}

void set_pm2p5_offset(char * arg) {
    set_float_param(arg, (float *) EEPROM_PM2P5_CAL_OFFSET, 0);
}

void set_pm10p0_offset(char * arg) {
    set_float_param(arg, (float *) EEPROM_PM10P0_CAL_OFFSET, 0);
}

void begin_pm(char * arg) {
    char sen = *arg;
    if(sen == 'a' || sen == 'A') {
        pmsx003_1.begin();
    } else if(sen == 'b' || sen == 'B') {
        pmsx003_2.begin();
    } else {
        Serial.print(F("Error: Expected argument of 'a' or 'b', but got '"));
        Serial.print(sen);
        Serial.println("'");
    }
}

void test_pm(char * arg, boolean silent) {
    char sen = *arg;
    if(sen == 'a' || sen == 'A') {
        if(!pmsx003_1.getSample(&instant_pm1p0_ugpm3_a, &instant_pm2p5_ugpm3_a, &instant_pm10p0_ugpm3_a)) {
            if(!silent) {
                Serial.println(F("PM Sensor A test failed"));
            }
        } else if(!silent) {
            Serial.println(F("PM Sensor A test passed"));
            Serial.print(F("PM1.0 = "));
            Serial.println(instant_pm1p0_ugpm3_a, 2);
            Serial.print(F("PM2.5 = "));
            Serial.println(instant_pm2p5_ugpm3_a, 2);
            Serial.print(F(" PM10 = "));
            Serial.println(instant_pm10p0_ugpm3_a, 2);
        }
    } else if(sen == 'b' || sen == 'B') {
        if(!pmsx003_2.getSample(&instant_pm1p0_ugpm3_b, &instant_pm2p5_ugpm3_b, &instant_pm10p0_ugpm3_b)) {
            if(!silent) {
                Serial.println(F("PM Sensor B test failed"));
            }
        } else if(!silent) {
            Serial.println(F("PM Sensor B test passed"));
            Serial.print(F("PM1.0 = "));
            Serial.println(instant_pm1p0_ugpm3_b, 2);
            Serial.print(F("PM2.5 = "));
            Serial.println(instant_pm2p5_ugpm3_b, 2);
            Serial.print(F(" PM10 = "));
            Serial.println(instant_pm10p0_ugpm3_b, 2);
        }
    } else if(!silent) {
        Serial.print(F("Error: Expected argument of 'a' or 'b', but got '"));
        Serial.print(sen);
        Serial.println("'");
    }
}

void test_pm(char * arg) {
    test_pm(arg, false);
}

void pmsen(char * arg) {
    if(strcmp_P(arg, PSTR("reset")) == 0) {
        digitalWrite(sensor_enable, LOW);
        delay(1000);
        digitalWrite(sensor_enable, HIGH);
    } else {
        Serial.print(F("Error: Expected argument of 'reset', but got '"));
        Serial.print(arg);
        Serial.println("'");
    }
}

void cmd_pm_enable(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    if(strcmp_P(arg, PSTR("a")) == 0 || strcmp_P(arg, PSTR("A")) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_PM_A_ENABLE, 1);
        pm_enable_sensor_a = 1;
    }
    else if(strcmp_P(arg, PSTR("b")) == 0 || strcmp_P(arg, PSTR("B")) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_PM_B_ENABLE, 1);
        pm_enable_sensor_b = 1;
    }
    recomputeAndStoreConfigChecksum();
}

void cmd_pm_disable(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    if(strcmp_P(arg, PSTR("a")) == 0 || strcmp_P(arg, PSTR("A")) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_PM_A_ENABLE, 0);
        pm_enable_sensor_a = 0;
    }
    else if(strcmp_P(arg, PSTR("b")) == 0 || strcmp_P(arg, PSTR("B")) == 0) {
        eeprom_write_byte((uint8_t *) EEPROM_PM_B_ENABLE, 0);
        pm_enable_sensor_b = 0;
    }
    recomputeAndStoreConfigChecksum();
}

uint8_t collectParticulate(void) {
    return collectParticulate(true, true);
}

uint8_t collectParticulate(boolean force_reset_on_failure, boolean change_lcd_on_reset) {
    //Serial.print("Particulate:");
    uint8_t num_failed_sensors = 0;
    boolean retA = true; // assume success
    boolean retB = true; // assume success

    boolean senA_constraint_failure = false;
    boolean senB_constraint_failure = false;

    static const float MAX_PM_VALUE = 1500.0f;
    static const float MAX_PM_DIFF = 100.0f;

    if(pm_enable_sensor_a) {
        if(!pmsx003_1.getSample(&instant_pm1p0_ugpm3_a, &instant_pm2p5_ugpm3_a, &instant_pm10p0_ugpm3_a)) {
            Serial.println(F("\nError: Failed to communicate with Particulate sensor A, restarting"));
            Serial.flush();
            if(force_reset_on_failure) {
                watchdogForceReset(change_lcd_on_reset);
            }
            retA = false;
            num_failed_sensors++;
        }
        else {
            float c_instant_pm1p0_ugpm3_a = constrain(instant_pm1p0_ugpm3_a, 0.0f, MAX_PM_VALUE);
            float c_instant_pm2p5_ugpm3_a = constrain(instant_pm2p5_ugpm3_a, 0.0f, MAX_PM_VALUE);
            float c_instant_pm10p0_ugpm3_a = constrain(instant_pm10p0_ugpm3_a, 0.0f, MAX_PM_VALUE);

            if( c_instant_pm1p0_ugpm3_a != instant_pm1p0_ugpm3_a ||
                    c_instant_pm2p5_ugpm3_a != instant_pm2p5_ugpm3_a ||
                    c_instant_pm10p0_ugpm3_a != instant_pm10p0_ugpm3_a
              ) {
                senA_constraint_failure = true;
                Serial.println(F("Warning: PM Sensor A experienced value constraint violation"));
            }
        }
    }

    if(pm_enable_sensor_b) {
        if(!pmsx003_2.getSample(&instant_pm1p0_ugpm3_b, &instant_pm2p5_ugpm3_b, &instant_pm10p0_ugpm3_b)) {
            Serial.println(F("\nError: Failed to communicate with Particulate sensor B, restarting"));
            Serial.flush();
            if(force_reset_on_failure) {
                watchdogForceReset(change_lcd_on_reset);
            }
            retB = false;
            num_failed_sensors++;
        }
        else {
            float c_instant_pm1p0_ugpm3_b = constrain(instant_pm1p0_ugpm3_b, 0.0f, MAX_PM_VALUE);
            float c_instant_pm2p5_ugpm3_b = constrain(instant_pm2p5_ugpm3_b, 0.0f, MAX_PM_VALUE);
            float c_instant_pm10p0_ugpm3_b = constrain(instant_pm10p0_ugpm3_b, 0.0f, MAX_PM_VALUE);

            if( c_instant_pm1p0_ugpm3_b != instant_pm1p0_ugpm3_b ||
                    c_instant_pm2p5_ugpm3_b != instant_pm2p5_ugpm3_b ||
                    c_instant_pm10p0_ugpm3_b != instant_pm10p0_ugpm3_b
              ) {
                pm_enable_sensor_b = 0;
                Serial.println(F("Warning: PM Sensor B experienced value constraint violation"));
            }
        }
    }

    const float pm10p0_sen_diff = abs(instant_pm10p0_ugpm3_a - instant_pm10p0_ugpm3_b);
    const float pm2p5_sen_diff = abs(instant_pm2p5_ugpm3_a - instant_pm2p5_ugpm3_b);
    const float pm1p0_sen_diff = abs(instant_pm1p0_ugpm3_a - instant_pm1p0_ugpm3_b);

    boolean pm_sen_diff_failure = false;
    if((pm10p0_sen_diff > MAX_PM_DIFF) ||
            (pm2p5_sen_diff > MAX_PM_DIFF) ||
            (pm1p0_sen_diff > MAX_PM_DIFF)) {
        pm_sen_diff_failure = true;
    }

    if(!pm_enable_sensor_a && senA_constraint_failure && pm_sen_diff_failure) {
        pm_enable_sensor_a = true;
        Serial.println(F("Warning: Disabling PM Sensor A because of value constraint and difference violation"));
    }

    if(!pm_enable_sensor_b && senB_constraint_failure && pm_sen_diff_failure) {
        pm_enable_sensor_b = true;
        Serial.println(F("Warning: Disabling PM Sensor B because of value constraint and difference violation"));
    }

    if(pm_enable_sensor_a && retA) {
        addSample(A_PM1P0_SAMPLE_BUFFER, instant_pm1p0_ugpm3_a);
        addSample(A_PM2P5_SAMPLE_BUFFER, instant_pm2p5_ugpm3_a);
        addSample(A_PM10P0_SAMPLE_BUFFER, instant_pm10p0_ugpm3_a);
    }

    if(pm_enable_sensor_b && retB) {
        addSample(B_PM1P0_SAMPLE_BUFFER, instant_pm1p0_ugpm3_b);
        addSample(B_PM2P5_SAMPLE_BUFFER, instant_pm2p5_ugpm3_b);
        addSample(B_PM10P0_SAMPLE_BUFFER, instant_pm10p0_ugpm3_b);
    }

    if(sample_buffer_idx == (sample_buffer_depth - 1)) {
        particulate_ready = true;
    }

    //Serial.println();
    return num_failed_sensors;
}

void pm1p0_convert_to_ugpm3(float average, float * temperature_compensated_value) {
    static boolean first_access = true;
    static float pm1p0_zero_ugpm3 = 0.0f;
    if(first_access) {
        pm1p0_zero_ugpm3 = eeprom_read_float((const float *) EEPROM_PM1P0_CAL_OFFSET);
        int32_t as_long = *((int32_t * ) (&pm1p0_zero_ugpm3));
        if(as_long == -1) {
            pm1p0_zero_ugpm3 = 0;
        }
        first_access = false;
    }

    // TODO: if we find there are temperature effects to compensate for, calculate parameters for compensation here
    // TODO: apply compensation formula using temperature dependant parameters here

    *temperature_compensated_value = average - pm1p0_zero_ugpm3;
    if(*temperature_compensated_value <= 0.0f) {
        *temperature_compensated_value = 0.0f;
    }
}

void pm2p5_convert_to_ugpm3(float average, float * temperature_compensated_value) {
    static boolean first_access = true;
    static float pm2p5_zero_ugpm3 = 0.0f;
    if(first_access) {
        pm2p5_zero_ugpm3 = eeprom_read_float((const float *) EEPROM_PM2P5_CAL_OFFSET);
        int32_t as_long = *((int32_t * ) (&pm2p5_zero_ugpm3));
        if(as_long == -1) {
            pm2p5_zero_ugpm3 = 0;
        }
        first_access = false;
    }

    // TODO: if we find there are temperature effects to compensate for, calculate parameters for compensation here
    // TODO: apply compensation formula using temperature dependant parameters here

    *temperature_compensated_value = average - pm2p5_zero_ugpm3;
    if(*temperature_compensated_value <= 0.0f) {
        *temperature_compensated_value = 0.0f;
    }
}

void pm10p0_convert_to_ugpm3(float average, float * temperature_compensated_value) {
    static boolean first_access = true;
    static float pm10p0_zero_ugpm3 = 0.0f;
    if(first_access) {
        pm10p0_zero_ugpm3 = eeprom_read_float((const float *) EEPROM_PM10P0_CAL_OFFSET);
        int32_t as_long = *((int32_t * ) (&pm10p0_zero_ugpm3));
        if(as_long == -1) {
            pm10p0_zero_ugpm3 = 0;
        }
        first_access = false;
    }

    // TODO: if we find there are temperature effects to compensate for, calculate parameters for compensation here
    // TODO: apply compensation formula using temperature dependant parameters here

    *temperature_compensated_value = average - pm10p0_zero_ugpm3;
    if(*temperature_compensated_value <= 0.0f) {
        *temperature_compensated_value = 0.0f;
    }
}

boolean publishParticulate() {
    clearTempBuffers();
    uint16_t num_samples = particulate_ready ? sample_buffer_depth : sample_buffer_idx;
    float pm1p0_compensated_value = 0.0f;
    float pm2p5_compensated_value = 0.0f;
    float pm10p0_compensated_value = 0.0f;

    float pm1p0_moving_average = 0.0f;
    float pm2p5_moving_average = 0.0f;
    float pm10p0_moving_average = 0.0f;

    if(pm_enable_sensor_a) {
        pm1p0_moving_average = calculateAverage(&(sample_buffer[A_PM1P0_SAMPLE_BUFFER][0]), num_samples);
        pm2p5_moving_average = calculateAverage(&(sample_buffer[A_PM2P5_SAMPLE_BUFFER][0]), num_samples);
        pm10p0_moving_average = calculateAverage(&(sample_buffer[A_PM10P0_SAMPLE_BUFFER][0]), num_samples);
        pm1p0_convert_to_ugpm3(pm1p0_moving_average, &pm1p0_compensated_value);
        pm2p5_convert_to_ugpm3(pm2p5_moving_average, &pm2p5_compensated_value);
        pm10p0_convert_to_ugpm3(pm10p0_moving_average, &pm10p0_compensated_value);
    }

    // hold on to these compensated averages
    pm1p0_ugpm3 = pm1p0_compensated_value;
    pm2p5_ugpm3 = pm2p5_compensated_value;
    pm10p0_ugpm3 = pm10p0_compensated_value;

    snprintf(scratch, SCRATCH_BUFFER_SIZE-1,
             "{"
             "\"serial-number\":\"%s\","
             "\"converted-units\":\"ug/m^3\","
             "\"sensor-part-number\":\"PMS5003\",",
             mqtt_client_id);

    appendAsJSON(scratch, "pm1p0_a", pm1p0_compensated_value, true);
    appendAsJSON(scratch, "pm2p5_a", pm2p5_compensated_value, true);
    appendAsJSON(scratch, "pm10p0_a", pm10p0_compensated_value, true);

    if(pm_enable_sensor_b) {
        pm1p0_moving_average = calculateAverage(&(sample_buffer[B_PM1P0_SAMPLE_BUFFER][0]), num_samples);
        pm2p5_moving_average = calculateAverage(&(sample_buffer[B_PM2P5_SAMPLE_BUFFER][0]), num_samples);
        pm10p0_moving_average = calculateAverage(&(sample_buffer[B_PM10P0_SAMPLE_BUFFER][0]), num_samples);
        pm1p0_convert_to_ugpm3(pm1p0_moving_average, &pm1p0_compensated_value);
        pm2p5_convert_to_ugpm3(pm2p5_moving_average, &pm2p5_compensated_value);
        pm10p0_convert_to_ugpm3(pm10p0_moving_average, &pm10p0_compensated_value);
    }

    appendAsJSON(scratch, "pm1p0_b", pm1p0_compensated_value, true);
    appendAsJSON(scratch, "pm2p5_b", pm2p5_compensated_value, true);
    appendAsJSON(scratch, "pm10p0_b", pm10p0_compensated_value, true);

    // the displayed value is the combined average
    pm1p0_ugpm3 = (pm1p0_ugpm3 + pm1p0_compensated_value) / (pm_enable_sensor_a + pm_enable_sensor_b);
    pm2p5_ugpm3 = (pm2p5_ugpm3 + pm2p5_compensated_value) / (pm_enable_sensor_a + pm_enable_sensor_b);
    pm10p0_ugpm3 = (pm10p0_ugpm3 + pm10p0_compensated_value) / (pm_enable_sensor_a + pm_enable_sensor_b);

    appendAsJSON(scratch, "pm1p0", pm1p0_ugpm3, true);
    appendAsJSON(scratch, "pm2p5", pm2p5_ugpm3, true);
    appendAsJSON(scratch, "pm10p0", pm10p0_ugpm3, false);

    strcat(scratch, gps_mqtt_string);
    strcat(scratch, "}");

    replace_character(scratch, '\'', '\"'); // replace single quotes with double quotes

    strcat(MQTT_TOPIC_STRING, MQTT_TOPIC_PREFIX);
    strcat(MQTT_TOPIC_STRING, "particulate");
    if(mqtt_suffix_enabled) {
        strcat(MQTT_TOPIC_STRING, "/");
        strcat(MQTT_TOPIC_STRING, mqtt_client_id);
    }
    return mqttPublish(MQTT_TOPIC_STRING, scratch);
}
float pressure_scale_factor(void) {
    float ret = 1.0f;

    static boolean first_access = true;
    static int16_t user_altitude_meters = 0.0f;

    if(first_access) {
        first_access = false;
        user_altitude_meters = (int16_t) eeprom_read_word((uint16_t *) EEPROM_ALTITUDE_METERS);
    }

    int16_t altitude_meters = user_altitude_meters;
    if(!user_location_override && (gps_altitude != TinyGPS::GPS_INVALID_F_ALTITUDE)) {
        altitude_meters = (int16_t) gps_altitude;
    }

    if(altitude_meters != -1) {
        // calculate scale factor of altitude and temperature
        const float kelvin_offset = 273.15f;
        const float lapse_rate_kelvin_per_meter = -0.0065f;
        const float pressure_exponentiation_constant = 5.2558774324f;

        float outside_temperature_kelvin = kelvin_offset + (temperature_degc - reported_temperature_offset_degC);
        float outside_temperature_kelvin_at_sea_level = outside_temperature_kelvin - lapse_rate_kelvin_per_meter * altitude_meters; // lapse rate is negative
        float pow_arg = 1.0f + ((lapse_rate_kelvin_per_meter * altitude_meters) / outside_temperature_kelvin_at_sea_level);
        ret = powf(pow_arg, pressure_exponentiation_constant);
    }

    return ret;
}
void co2_baseline_voltage_characterization_command(char * arg) {
    baseline_voltage_characterization_command(arg, EEPROM_CO2_BASELINE_VOLTAGE_TABLE);
}

void set_co2_offset(char * arg) {
    set_float_param(arg, (float *) EEPROM_CO2_CAL_OFFSET, 0);
}

void collectCO2(void) {
    //Serial.print("CO2:");
    pinMode(9, INPUT_PULLUP);
    pinMode(10, OUTPUT);
    digitalWrite(10, HIGH);
    delay(10);
    if(k30.getSample(&instant_co2_ppm)) {
        //Serial.print(instant_co2_ppm, 1);
        addSample(CO2_SAMPLE_BUFFER, instant_co2_ppm);
        if(sample_buffer_idx == (sample_buffer_depth - 1)) {
            co2_ready = true;
        }
    }
    else {
        Serial.println(F("Error: Failed to communicate with CO2 sensor, restarting"));
        Serial.flush();
        resetSensors();
//    watchdogForceReset();
    }

    //Serial.println();
}

void co2_convert_to_ppm(float average, float * converted_value, float * temperature_compensated_value) {
    static boolean first_access = true;
    static float co2_zero_ppm = 0.0f;
    if(first_access) {
        co2_zero_ppm = eeprom_read_float((const float *) EEPROM_CO2_CAL_OFFSET);
        int32_t as_long = *((int32_t * ) (&co2_zero_ppm));
        if(as_long == -1) {
            co2_zero_ppm = 0;
        }
        first_access = false;
    }

    // TODO: if we find there are temperature effects to compensate for, calculate parameters for compensation here
    // TODO: apply compensation formula using temperature dependant parameters here

    // there's no interpretation needed for this sensor, we don't actually have any "raw" data
    *converted_value = average;

    *temperature_compensated_value = *converted_value - co2_zero_ppm; // no compensation yet
    if(*temperature_compensated_value <= 0.0f) {
        *temperature_compensated_value = 0.0f;
    }
}

boolean publishCO2() {
    clearTempBuffers();
    uint16_t num_samples = co2_ready ? sample_buffer_depth : sample_buffer_idx;
    float converted_value = 0.0f, compensated_value = 0.0f;
    float co2_moving_average = calculateAverage(&(sample_buffer[CO2_SAMPLE_BUFFER][0]), num_samples);
    co2_convert_to_ppm(co2_moving_average, &converted_value, &compensated_value);
    co2_ppm = compensated_value;
    //safe_dtostrf(co2_moving_average, -8, 5, raw_value_string, 16);
    safe_dtostrf(converted_value, -8, 1, converted_value_string, 16);
    safe_dtostrf(compensated_value, -8, 1, compensated_value_string, 16);
    safe_dtostrf(instant_co2_ppm, -8, 1, raw_instant_value_string, 16);

    //trim_string(raw_value_string);
    trim_string(converted_value_string);
    trim_string(compensated_value_string);
    trim_string(raw_instant_value_string);

    //replace_nan_with_null(raw_value_string);
    replace_nan_with_null(converted_value_string);
    replace_nan_with_null(compensated_value_string);
    replace_nan_with_null(raw_instant_value_string);

    snprintf(scratch, SCRATCH_BUFFER_SIZE-1,
             "{"
             "\"serial-number\":\"%s\","
             "\"raw-instant-value\":%s,"
             "\"converted-value\":%s,"
             "\"converted-units\":\"ppm\","
             "\"compensated-value\":%s,"
             "\"sensor-part-number\":\"SE-0018\""
             "%s"
             "}",
             mqtt_client_id,
             raw_instant_value_string,
             converted_value_string,
             compensated_value_string,
             gps_mqtt_string);

    replace_character(scratch, '\'', '\"'); // replace single quotes with double quotes

    strcat(MQTT_TOPIC_STRING, MQTT_TOPIC_PREFIX);
    strcat(MQTT_TOPIC_STRING, "co2");
    if(mqtt_suffix_enabled) {
        strcat(MQTT_TOPIC_STRING, "/");
        strcat(MQTT_TOPIC_STRING, mqtt_client_id);
    }
    return mqttPublish(MQTT_TOPIC_STRING, scratch);
}
boolean clear_baseline_voltage_characterization(uint32_t eeprom_table_base_address) {
    baseline_voltage_t tmp;
    uint32_t erase_float = 0xFFFFFFFF;
    boolean deleted_at_least_one_entry = false;
    for(uint8_t ii = 0; ii < 5; ii++) {
        eeprom_read_block((void *) &tmp, (void *) (eeprom_table_base_address + (ii*sizeof(baseline_voltage_t))), sizeof(baseline_voltage_t));
        if(!isnan(tmp.temperature_degC)) {
            deleted_at_least_one_entry = true;
            tmp.temperature_degC = *((float *) (&erase_float));
            tmp.slope_volts_per_degC = *((float *) (&erase_float));
            tmp.intercept_volts = *((float *) (&erase_float));
            eeprom_write_block((const void *) &tmp, (void *) (eeprom_table_base_address + (ii*sizeof(baseline_voltage_t))), sizeof(baseline_voltage_t));
        }
    }

    if(deleted_at_least_one_entry) {
        return true;
    }
    return false;
}

void baseline_voltage_characterization_command(char * arg, uint32_t eeprom_table_base_address) {
    boolean valid = true;

    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    trim_string(arg);
    lowercase(arg);

    // make sure there's at least one argument
    char * token = strtok(arg, " "); // tokenize the string on spaces
    if(token == NULL) {
        Serial.println(F("Error: no arguments provided"));
        return;
    }

    // the first argument should be either "add", "show", or "clear"
    if(strcmp(token, "add") == 0) {
        valid = add_baseline_voltage_characterization(token, eeprom_table_base_address); // tokenization in progress!
    }
    else if(strcmp(token, "clear") == 0) {
        valid = clear_baseline_voltage_characterization(eeprom_table_base_address);
    }
    else if(strcmp(token, "show") == 0) {
        valid = false;
        print_baseline_voltage_characterization(eeprom_table_base_address);
    }
    else {
        Serial.print(F("Error: valid sub-commands are [add, clear] but got '"));
        Serial.print(token);
        Serial.println("'");
        return;
    }

    if (valid) {
        recomputeAndStoreConfigChecksum();
    }
}

void load_temperature_characterization_entry(uint32_t eeprom_table_base_address, uint8_t index) {
    eeprom_read_block((void *) &baseline_voltage_struct, (void *) (eeprom_table_base_address + (index*sizeof(baseline_voltage_t))), sizeof(baseline_voltage_t));
}

boolean load_and_validate_temperature_characterization_entry(uint32_t eeprom_table_base_address, uint8_t index) {
    return valid_temperature_characterization_entry(eeprom_table_base_address, index);
}

boolean find_and_load_temperature_characterization_entry(uint32_t eeprom_table_base_address, float target_temperature_degC) {
    // this search requires that table entries are monotonically increasing in temperature
    // which is to say entry[0].temperature_degC < entry[1].temperature_degC < ... < entry[4].temperature_degC
    // this is enforced by the add entry mechanism

    // finds the first entry that has a temperature that is <= target_temperature
    // as a side-effect, if such an entry is found, it is loaded into baseline_voltage_struct

    int8_t index_of_highest_temperature_that_is_less_than_or_equal_to_target_temperature = -1;
    for(uint8_t ii = 0; ii < 5; ii++) {
        if(load_and_validate_temperature_characterization_entry(eeprom_table_base_address, ii)) {
            if(baseline_voltage_struct.temperature_degC <= target_temperature_degC) {
                index_of_highest_temperature_that_is_less_than_or_equal_to_target_temperature = ii;
            }
            else {
                break;
            }
        }
    }

    if(index_of_highest_temperature_that_is_less_than_or_equal_to_target_temperature >= 0) {
        load_and_validate_temperature_characterization_entry(eeprom_table_base_address,
                index_of_highest_temperature_that_is_less_than_or_equal_to_target_temperature);
        return true;
    }

    // if we got to here it means that the target temperature is colder than the coldest characterized value
    // or there are no valid entries
    return false;

}

boolean valid_temperature_characterization_struct(baseline_voltage_t * temperature_characterization_struct_p) {
    if(isnan(temperature_characterization_struct_p->temperature_degC)) {
        return false;
    }

    if(isnan(temperature_characterization_struct_p->slope_volts_per_degC)) {
        return false;
    }

    if(isnan(temperature_characterization_struct_p->intercept_volts)) {
        return false;
    }

    if(temperature_characterization_struct_p->temperature_degC < -273.15) {
        return false;
    }

    if(temperature_characterization_struct_p->temperature_degC > 60.0) {
        return false;
    }

    return true;
}

boolean valid_temperature_characterization_entry(uint32_t eeprom_table_base_address, uint8_t index) {
    // the entry is valid only if none of the three fields are NaN
    // read the requested table entry into RAM, note side effect is baseline_voltage_struct is loaded with the data
    load_temperature_characterization_entry(eeprom_table_base_address, index);
    return valid_temperature_characterization_struct(&baseline_voltage_struct);
}

boolean valid_temperature_characterization(uint32_t eeprom_table_base_address) {
    // the table is valid only if the first entry is valid
    if(load_and_validate_temperature_characterization_entry(eeprom_table_base_address, 0)) {
        return true;
    }
    return false;
}

void print_baseline_voltage_characterization_entry(uint32_t eeprom_table_base_address, uint8_t index) {

    if(valid_temperature_characterization_entry(eeprom_table_base_address, index)) {
        Serial.print(F("        "));
        Serial.print(index);
        Serial.print(F("\t"));
        Serial.print(baseline_voltage_struct.temperature_degC,8);
        Serial.print(F("\t"));
        Serial.print(baseline_voltage_struct.slope_volts_per_degC,8);
        Serial.print(F("\t"));
        Serial.print(baseline_voltage_struct.intercept_volts,8);
        Serial.println();
    }

}

void print_baseline_voltage_characterization(uint32_t eeprom_table_base_address) {
    Serial.print(F("        "));
    Serial.println(F("idx\ttemp [degC]\tslope [V/degC]\tintercept [V]"));
    Serial.print(F("        "));
    Serial.println(F("---------------------------------------------------------"));
    if(!load_and_validate_temperature_characterization_entry(eeprom_table_base_address, 0)) {
        Serial.print(F("        "));
        Serial.println(F("No valid entries found."));
    }
    else {
        for(uint8_t ii = 0; ii < 5; ii++) {
            if(load_and_validate_temperature_characterization_entry(eeprom_table_base_address, ii)) {
                print_baseline_voltage_characterization_entry(eeprom_table_base_address, ii);
            }
            else {
                break;
            }
        }
    }
}
void eco2_baseline_voltage_characterization_command(char * arg) {
    baseline_voltage_characterization_command(arg, EEPROM_ECO2_BASELINE_VOLTAGE_TABLE);
}

void tvoc_baseline_voltage_characterization_command(char * arg) {
    baseline_voltage_characterization_command(arg, EEPROM_TVOC_BASELINE_VOLTAGE_TABLE);
}

void res_baseline_voltage_characterization_command(char * arg) {
    baseline_voltage_characterization_command(arg, EEPROM_RESISTANCE_BASELINE_VOLTAGE_TABLE);
}

void eco2_slope_command(char * arg) {
    set_float_param(arg, (float *) EEPROM_ECO2_CAL_SLOPE, 0);
}

void eco2_offset_command(char * arg) {
    set_float_param(arg, (float *) EEPROM_ECO2_CAL_OFFSET, 0);
}

void tvoc_slope_command(char * arg) {
    set_float_param(arg, (float *) EEPROM_TVOC_CAL_SLOPE, 0);
}

void tvoc_offset_command(char * arg) {
    set_float_param(arg, (float *) EEPROM_TVOC_CAL_OFFSET, 0);
}

void res_slope_command(char * arg) {
    set_float_param(arg, (float *) EEPROM_RESISTANCE_CAL_SLOPE, 0);
}

void res_offset_command(char * arg) {
    set_float_param(arg, (float *) EEPROM_RESISTANCE_CAL_OFFSET, 0);
}

void co2_equivalent_compensation(float average, float * converted_value, float * temperature_compensated_value) {
    static boolean first_access = true;
    static float co2_offset = 0.0f;
    static float co2_slope = 1.0f;
    if(first_access) {
        co2_offset = eeprom_read_float((const float *) EEPROM_ECO2_CAL_OFFSET);
        co2_slope = eeprom_read_float((const float *) EEPROM_ECO2_CAL_SLOPE);

        if(isnan(co2_offset)) {
            co2_offset = 0.0f;
        }

        if(isnan(co2_slope)) {
            co2_slope = 1.0f;
        }

        first_access = false;
    }

    // there's no interpretation needed for this sensor
    *converted_value = average;

    // use the BLV table to correct temperature dependent baseline if available
    *temperature_compensated_value = *converted_value;
    float baseline_value_at_temperature = 0.0f;
    if(valid_temperature_characterization(EEPROM_ECO2_BASELINE_VOLTAGE_TABLE)) {
        // do the math a little differently in this case
        // first figure out what baseline_offset_voltage_at_temperature is based on the characterization
        // (1) find the first entry in the table where temperature_degc is >= the table temperature
        // (2) if no such entry exists, then it is colder than the coldest characterized temperature
        // (3) use the associated slope and intercept to determine baseline_offset_voltage_at_temperature
        //     using the formula baseline_offset_voltage_at_temperature = slope * temperature_degc + intercept
        if(find_and_load_temperature_characterization_entry(EEPROM_ECO2_BASELINE_VOLTAGE_TABLE, temperature_degc)) {
            // great we have a useful entry in the table for this temperature, pull the slope and intercept
            // and use them to evaluate the baseline voltage at the current temperature
            baseline_value_at_temperature =
                temperature_degc * baseline_voltage_struct.slope_volts_per_degC + baseline_voltage_struct.intercept_volts;
        }
        else {
            // it's colder than the coldest characterized temperature
            // in this special case, instead of extrapolating, we will just evaluate the entry line
            // at coldest characterized temperature, and use that
            load_temperature_characterization_entry(EEPROM_ECO2_BASELINE_VOLTAGE_TABLE, 0);
            baseline_value_at_temperature =
                baseline_voltage_struct.temperature_degC * baseline_voltage_struct.slope_volts_per_degC + baseline_voltage_struct.intercept_volts;
        }


        // then do the math with that number, if we did the characterization properly, then
        *temperature_compensated_value -= baseline_value_at_temperature;
    }

    // apply config-based linear transform
    *temperature_compensated_value *= co2_slope;
    *temperature_compensated_value += co2_offset;

    if(*temperature_compensated_value <= 0.0f) {
        *temperature_compensated_value = 0.0f;
    }
}

void tvoc_compensation(float average, float * converted_value, float * temperature_compensated_value) {
    static boolean first_access = true;
    static float tvoc_offset = 0.0f;
    static float tvoc_slope = 1.0f;
    if(first_access) {
        tvoc_offset = eeprom_read_float((const float *) EEPROM_TVOC_CAL_OFFSET);
        tvoc_slope = eeprom_read_float((const float *) EEPROM_TVOC_CAL_SLOPE);

        if(isnan(tvoc_offset)) {
            tvoc_offset = 0.0f;
        }

        if(isnan(tvoc_slope)) {
            tvoc_slope = 1.0f;
        }
    }

    // there's no interpretation needed for this sensor
    *converted_value = average;

    // use the BLV table to correct temperature dependent baseline if available
    *temperature_compensated_value = *converted_value;
    float baseline_value_at_temperature = 0.0f;
    if(valid_temperature_characterization(EEPROM_TVOC_BASELINE_VOLTAGE_TABLE)) {
        // do the math a little differently in this case
        // first figure out what baseline_offset_voltage_at_temperature is based on the characterization
        // (1) find the first entry in the table where temperature_degc is >= the table temperature
        // (2) if no such entry exists, then it is colder than the coldest characterized temperature
        // (3) use the associated slope and intercept to determine baseline_offset_voltage_at_temperature
        //     using the formula baseline_offset_voltage_at_temperature = slope * temperature_degc + intercept
        if(find_and_load_temperature_characterization_entry(EEPROM_TVOC_BASELINE_VOLTAGE_TABLE, temperature_degc)) {
            // great we have a useful entry in the table for this temperature, pull the slope and intercept
            // and use them to evaluate the baseline voltage at the current temperature
            baseline_value_at_temperature =
                temperature_degc * baseline_voltage_struct.slope_volts_per_degC + baseline_voltage_struct.intercept_volts;
        }
        else {
            // it's colder than the coldest characterized temperature
            // in this special case, instead of extrapolating, we will just evaluate the entry line
            // at coldest characterized temperature, and use that
            load_temperature_characterization_entry(EEPROM_TVOC_BASELINE_VOLTAGE_TABLE, 0);
            baseline_value_at_temperature =
                baseline_voltage_struct.temperature_degC * baseline_voltage_struct.slope_volts_per_degC + baseline_voltage_struct.intercept_volts;
        }

        // then do the math with that number, if we did the characterization properly, then
        *temperature_compensated_value -= baseline_value_at_temperature;
    }

    // apply config-based linear transform
    *temperature_compensated_value *= tvoc_slope;
    *temperature_compensated_value += tvoc_offset;

    if(*temperature_compensated_value <= 0.0f) {
        *temperature_compensated_value = 0.0f;
    }
}

void resistance_compensation(float average, float * converted_value, float * temperature_compensated_value) {
    static boolean first_access = true;
    static float resistance_offset = 0.0f;
    static float resistance_slope = 1.0f;
    if(first_access) {
        resistance_offset = eeprom_read_float((const float *) EEPROM_RESISTANCE_CAL_OFFSET);
        resistance_slope = eeprom_read_float((const float *) EEPROM_RESISTANCE_CAL_SLOPE);

        if(isnan(resistance_offset)) {
            resistance_offset = 0.0f;
        }

        if(isnan(resistance_slope)) {
            resistance_slope = 1.0f;
        }
    }

    // there's no interpretation needed for this sensor
    *converted_value = average;

    // use the BLV table to correct temperature dependent baseline if available
    *temperature_compensated_value = *converted_value;
    float baseline_value_at_temperature = 0.0f;
    if(valid_temperature_characterization(EEPROM_RESISTANCE_BASELINE_VOLTAGE_TABLE)) {
        // do the math a little differently in this case
        // first figure out what baseline_offset_voltage_at_temperature is based on the characterization
        // (1) find the first entry in the table where temperature_degc is >= the table temperature
        // (2) if no such entry exists, then it is colder than the coldest characterized temperature
        // (3) use the associated slope and intercept to determine baseline_offset_voltage_at_temperature
        //     using the formula baseline_offset_voltage_at_temperature = slope * temperature_degc + intercept
        if(find_and_load_temperature_characterization_entry(EEPROM_RESISTANCE_BASELINE_VOLTAGE_TABLE, temperature_degc)) {
            // great we have a useful entry in the table for this temperature, pull the slope and intercept
            // and use them to evaluate the baseline voltage at the current temperature
            baseline_value_at_temperature =
                temperature_degc * baseline_voltage_struct.slope_volts_per_degC + baseline_voltage_struct.intercept_volts;
        }
        else {
            // it's colder than the coldest characterized temperature
            // in this special case, instead of extrapolating, we will just evaluate the entry line
            // at coldest characterized temperature, and use that
            load_temperature_characterization_entry(EEPROM_RESISTANCE_BASELINE_VOLTAGE_TABLE, 0);
            baseline_value_at_temperature =
                baseline_voltage_struct.temperature_degC * baseline_voltage_struct.slope_volts_per_degC + baseline_voltage_struct.intercept_volts;
        }

        // then do the math with that number, if we did the characterization properly, then
        *temperature_compensated_value -= baseline_value_at_temperature;
    }

    // apply config-based linear transform
    *temperature_compensated_value *= resistance_slope;
    *temperature_compensated_value += resistance_offset;

    if(*temperature_compensated_value <= 0.0f) {
        *temperature_compensated_value = 0.0f;
    }
}

void collectCO2Equivalent(void) {
    if(!iaqcore_failed && iaqcore_ok) {
        selectSlot1();
        if(iaqcore.getStatus() == AMS_IAQ_CORE_C_STATUS_OK) {
            instant_co2_equivalent_ppm = iaqcore.getCO2EquivalentPPM();
            addSample(CO2_EQUIVALENT_SAMPLE_BUFFER, instant_co2_equivalent_ppm);
            if(sample_buffer_idx == (sample_buffer_depth - 1)) {
                iaqcore_ready = true;
            }
        }
        else {
            addSample(CO2_EQUIVALENT_SAMPLE_BUFFER, instant_co2_equivalent_ppm); // add a zero
        }
    }
}

void collectTVOC(void) {
    if(!iaqcore_failed && iaqcore_ok) {
        selectSlot1();
        if(iaqcore.getStatus() == AMS_IAQ_CORE_C_STATUS_OK) {
            instant_tvoc_ppb = iaqcore.getTVOCEquivalentPPB();
            addSample(TVOC_SAMPLE_BUFFER, instant_tvoc_ppb);
            if(sample_buffer_idx == (sample_buffer_depth - 1)) {
                iaqcore_ready = true;
            }
        }
        else {
            addSample(TVOC_SAMPLE_BUFFER, instant_tvoc_ppb); // add a zero
        }
    }
}

void collectResistance(void) {
    if(!iaqcore_failed && iaqcore_ok) {
        selectSlot1();
        if(iaqcore.getStatus() == AMS_IAQ_CORE_C_STATUS_OK) {
            instant_resistance_ohms = iaqcore.getResistanceOhms();
            addSample(RESISTANCE_SAMPLE_BUFFER, instant_resistance_ohms);
            if(sample_buffer_idx == (sample_buffer_depth - 1)) {
                iaqcore_ready = true;
            }
        }
        else {
            addSample(RESISTANCE_SAMPLE_BUFFER, instant_resistance_ohms); // add a zero
        }
    }
}

boolean publishIAQCore() {
    clearTempBuffers();
    uint16_t num_samples = iaqcore_ready ? sample_buffer_depth : sample_buffer_idx;
    float converted_value = 0.0f, compensated_value = 0.0f;

    // equivalent co2
    float eco2_moving_average = calculateAverage(&(sample_buffer[CO2_EQUIVALENT_SAMPLE_BUFFER][0]), num_samples);
    co2_equivalent_compensation(eco2_moving_average, &converted_value, &compensated_value);
    co2_equivalent_ppm = compensated_value;

    safe_dtostrf(converted_value, -8, 2, converted_value_string, 16);     // the averaged value, not corrected for temperature effects
    safe_dtostrf(compensated_value, -8, 2, compensated_value_string, 16); // the averaged value, corrected for temperature effects
    safe_dtostrf(instant_co2_equivalent_ppm, -8, 2, raw_instant_value_string, 16); // the instantaneous value, not corrected for temperature effects

    co2_equivalent_compensation(instant_co2_equivalent_ppm, &converted_value, &compensated_value); // re-compute on the instant value rather than the moving average value
    safe_dtostrf(compensated_value, -8, 2, compensated_instant_value_string, 16); // the instantaneous value, corrected for temperature effects

    // tvoc

    float tvoc_moving_average = calculateAverage(&(sample_buffer[TVOC_SAMPLE_BUFFER][0]), num_samples);
    tvoc_compensation(tvoc_moving_average, &converted_value, &compensated_value);
    tvoc_ppb = compensated_value;

    safe_dtostrf(converted_value, -8, 2, converted_value_string_2, 16);     // the averaged value, not corrected for temperature effects
    safe_dtostrf(compensated_value, -8, 2, compensated_value_string_2, 16); // the averaged value, corrected for temperature effects
    safe_dtostrf(instant_tvoc_ppb, -8, 2, raw_instant_value_string_2, 16); // the instantaneous value, not corrected for temperature effects

    tvoc_compensation(instant_tvoc_ppb, &converted_value, &compensated_value); // re-compute on the instant value rather than the moving average value
    safe_dtostrf(compensated_value, -8, 2, compensated_instant_value_string_2, 16); // the instantaneous value, corrected for temperature effects


    // resistance

    float resistance_moving_average = calculateAverage(&(sample_buffer[RESISTANCE_SAMPLE_BUFFER][0]), num_samples);
    resistance_compensation(resistance_moving_average, &converted_value, &compensated_value);
    resistance_ohms = compensated_value;

    safe_dtostrf(converted_value, -8, 2, converted_value_string_3, 16);     // the averaged value, not corrected for temperature effects
    safe_dtostrf(compensated_value, -8, 2, compensated_value_string_3, 16); // the averaged value, corrected for temperature effects
    safe_dtostrf(instant_resistance_ohms, -8, 2, raw_instant_value_string_3, 16); // the instantaneous value, not corrected for temperature effects

    resistance_compensation(instant_resistance_ohms, &converted_value, &compensated_value); // re-compute on the instant value rather than the moving average value
    safe_dtostrf(compensated_value, -8, 2, compensated_instant_value_string_3, 16); // the instantaneous value, corrected for temperature effects

    // clean up strings and compose message

    //trim_string(raw_value_string);
    trim_string(converted_value_string);
    trim_string(compensated_value_string);
    trim_string(raw_instant_value_string);
    trim_string(compensated_instant_value_string);

    trim_string(converted_value_string_2);
    trim_string(compensated_value_string_2);
    trim_string(raw_instant_value_string_2);
    trim_string(compensated_instant_value_string_2);

    trim_string(converted_value_string_3);
    trim_string(compensated_value_string_3);
    trim_string(raw_instant_value_string_3);
    trim_string(compensated_instant_value_string_3);

    //replace_nan_with_null(raw_value_string);
    replace_nan_with_null(converted_value_string);
    replace_nan_with_null(compensated_value_string);
    replace_nan_with_null(raw_instant_value_string);
    replace_nan_with_null(compensated_instant_value_string);

    replace_nan_with_null(converted_value_string_2);
    replace_nan_with_null(compensated_value_string_2);
    replace_nan_with_null(raw_instant_value_string_2);
    replace_nan_with_null(compensated_instant_value_string_2);

    replace_nan_with_null(converted_value_string_3);
    replace_nan_with_null(compensated_value_string_3);
    replace_nan_with_null(raw_instant_value_string_3);
    replace_nan_with_null(compensated_instant_value_string_3);

    snprintf(scratch, SCRATCH_BUFFER_SIZE-1,
             "{"
             "\"serial-number\":\"%s\","
             "\"raw-instant-co2\":%s,"
             "\"converted-co2\":%s,"
             "\"compensated-co2\":%s,"
             "\"compensated-instant-co2\":%s,"
             "\"raw-instant-tvoc\":%s,"
             "\"converted-tvoc\":%s,"
             "\"compensated-tvoc\":%s,"
             "\"compensated-instant-tvoc\":%s,"
             "\"raw-instant-resistance\":%s,"
             "\"converted-resistance\":%s,"
             "\"compensated-resistance\":%s,"
             "\"compensated-instant-resistance\":%s,"
             "\"co2-units\":\"ppm\","
             "\"tvoc-units\":\"ppb\","
             "\"resistance-units\":\"ohm\","
             "\"sensor-part-number\":\"AMS iAQ-core C\""
             "%s"
             "}",
             mqtt_client_id,
             raw_instant_value_string,
             converted_value_string,
             compensated_value_string,
             compensated_instant_value_string,
             raw_instant_value_string_2,
             converted_value_string_2,
             compensated_value_string_2,
             compensated_instant_value_string_2,
             raw_instant_value_string_3,
             converted_value_string_3,
             compensated_value_string_3,
             compensated_instant_value_string_3,
             gps_mqtt_string);

    replace_character(scratch, '\'', '\"'); // replace single quotes with double quotes

    strcat(MQTT_TOPIC_STRING, MQTT_TOPIC_PREFIX);
    strcat(MQTT_TOPIC_STRING, "voc");
    if(mqtt_suffix_enabled) {
        strcat(MQTT_TOPIC_STRING, "/");
        strcat(MQTT_TOPIC_STRING, mqtt_client_id);
    }
    return mqttPublish(MQTT_TOPIC_STRING, scratch);
}

void set_reported_temperature_offset(char * arg) {
    set_float_param(arg, (float *) EEPROM_TEMPERATURE_OFFSET, 0);
}

void set_reported_humidity_offset(char * arg) {
    set_float_param(arg, (float *) EEPROM_HUMIDITY_OFFSET, 0);
}

void set_reported_temperature_offline_offset(char * arg) {
    set_float_param(arg, (float *) EEPROM_TEMPERATURE_OFFLINE_OFFSET, 0);
}

void set_reported_humidity_offline_offset(char * arg) {
    set_float_param(arg, (float *) EEPROM_HUMIDITY_OFFLINE_OFFSET, 0);
}

void set_private_key(char * arg) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    // we've reserved 32-bytes of EEPROM for a private key
    // only exact 64-character hex representation is accepted
    uint8_t key[32] = {0};
    uint16_t len = strlen(arg);
    if (len == 64) {
        // process the characters as pairs
        for (uint8_t ii = 0; ii < 32; ii++) {
            char tmp[3] = {0};
            tmp[0] = arg[ii * 2];
            tmp[1] = arg[ii * 2 + 1];
            if (isxdigit(tmp[0]) && isxdigit(tmp[1])) {
                key[ii] = (uint8_t) strtoul(tmp, NULL, 16);
            }
            else {
                Serial.print(F("Error: Invalid hex value found ["));
                Serial.print(tmp);
                Serial.println(F("]"));
                return;
            }
        }

        eeprom_write_block(key, (void *) EEPROM_PRIVATE_KEY, 32);
        recomputeAndStoreConfigChecksum();
    }
    else {
        Serial.println(F("Error: Private key must be exactly 64 characters long, "));
        Serial.print(F("       but was "));
        Serial.print(len);
        Serial.println(F(" characters long."));
    }
}

void recomputeAndStoreConfigChecksum(void) {
    if(!configMemoryUnlocked(__LINE__)) {
        return;
    }

    uint16_t crc = computeEepromChecksum();
    eeprom_write_word((uint16_t *) EEPROM_CRC_CHECKSUM, crc);
}

uint16_t computeEepromChecksum(void) {
    uint16_t crc = 0;

    // there are EEPROM_CONFIG_MEMORY_SIZE - 2 bytes to compute the CRC16 over
    // the first byte is located at EEPROM_CRC_CHECKSUM + 2
    // the last byte is located at EEPROM_CONFIG_MEMORY_SIZE - 1
    for (uint16_t ii = 0; ii < EEPROM_CONFIG_MEMORY_SIZE - 2; ii++) {
        uint8_t value = eeprom_read_byte((uint8_t *) (EEPROM_CRC_CHECKSUM + 2 + ii));
        crc = _crc16_update(crc, value);
    }
    return crc;
}

uint16_t getStoredEepromChecksum(void) {
    return eeprom_read_word((const uint16_t *) EEPROM_CRC_CHECKSUM);
}

uint16_t computeFlashChecksum(void) {
    uint16_t crc = 0;
    // there are EEPROM_CONFIG_MEMORY_SIZE - 2 bytes to compute the CRC16 over
    // the first byte is located at SECOND_TO_LAST_4K_PAGE_ADDRESS + 2
    // the last byte is located at EEPROM_CONFIG_MEMORY_SIZE - 1
    for (uint16_t ii = 0; ii < EEPROM_CONFIG_MEMORY_SIZE - 2; ii++) {
        uint8_t value = flash.readByte(((uint32_t) SECOND_TO_LAST_4K_PAGE_ADDRESS) + 2UL + ((uint32_t) ii));
        crc = _crc16_update(crc, value);
    }
    return crc;
}

uint16_t getStoredFlashChecksum(void) {
    uint16_t stored_crc = flash.readByte(((uint32_t) SECOND_TO_LAST_4K_PAGE_ADDRESS) + 1UL);
    stored_crc <<= 8;
    stored_crc |= flash.readByte(((uint32_t) SECOND_TO_LAST_4K_PAGE_ADDRESS) + 0UL);
    return stored_crc;
}

/****** GAS SENSOR SUPPORT FUNCTIONS ******/

void selectNoSlot(void) {
    pinMode(7, OUTPUT);
    pinMode(9, OUTPUT);
    pinMode(10, OUTPUT);
    digitalWrite(7, LOW);
    digitalWrite(9, LOW);
    digitalWrite(10, LOW);
    delay(10);
}

void selectSlot1(void) {
    selectNoSlot();
    pinMode(10, OUTPUT);
    digitalWrite(10, HIGH);
    delay(10);
}

void selectSlot2(void) {
    selectNoSlot();
    pinMode(9, OUTPUT);
    digitalWrite(9, HIGH);
    delay(10);
}

void selectSlot3(void) {
    selectNoSlot();
    pinMode(7, OUTPUT);
    digitalWrite(7, HIGH);
    delay(10);
}

/****** LCD SUPPORT FUNCTIONS ******/
void safe_dtostrf(float value, signed char width, unsigned char precision, char * target_buffer, uint16_t target_buffer_length) {
    char meta_format_string[16] = "%%.%df";
    char format_string[16] = {0};

    if((target_buffer != NULL) && (target_buffer_length > 0)) {
        snprintf(format_string, 15, meta_format_string, precision); // format string should come out to something like "%.2f"
        snprintf(target_buffer, target_buffer_length - 1, format_string, value);
    }

}

void backlightOn(void) {
    uint8_t backlight_behavior = eeprom_read_byte((uint8_t *) EEPROM_BACKLIGHT_STARTUP);
    if(backlight_behavior != BACKLIGHT_ALWAYS_OFF) {
        g_backlight_turned_on = true; // set global flag
        digitalWrite(A6, HIGH);
    }
}

void backlightOff(void) {
    uint8_t backlight_behavior = eeprom_read_byte((uint8_t *) EEPROM_BACKLIGHT_STARTUP);
    if(backlight_behavior != BACKLIGHT_ALWAYS_ON) {
        g_backlight_turned_on = false; // clear global flag
        digitalWrite(A6, LOW);
    }
}

void lcdFrownie(uint8_t pos_x, uint8_t pos_y) {
    if((pos_x < 16) && (pos_y < 2)) {
        lcd.setCursor(pos_x, pos_y);
        lcd.write((byte) 1);
    }
}

void lcdSmiley(uint8_t pos_x, uint8_t pos_y) {
    if((pos_x < 16) && (pos_y < 2)) {
        lcd.setCursor(pos_x, pos_y);
        lcd.write((byte) 0);
    }
}

void lcdBars(uint8_t numBars) {
    for(uint8_t ii = 0; ii < numBars && ii < 5; ii++) {
        lcd.setCursor(5+ii, 1);
        lcd.write(3); // full bar
    }

    if(numBars < 5) {
        for(uint8_t ii = 0; ii <  5 - numBars; ii++) {
            lcd.setCursor(5 + numBars + ii, 1);
            lcd.write((byte) 2); // empty bar
        }
    }
}

void setLCD_P(const char * str PROGMEM) {
    char tmp[33] = {0};
    strncpy_P(tmp, str, 32);
    setLCD(tmp);
}

//void dumpDisplayBuffer(void){
//  Serial.print(F("Debug: Line 1: "));
//  for(uint8_t ii = 0; ii < 17; ii++){
//    Serial.print(F("0x"));
//    Serial.print((uint8_t) g_lcd_buffer[0][ii],HEX);
//    if(ii != 16){
//      Serial.print(F(","));
//    }
//  }
//  Serial.println();
//  Serial.print(F("Debug: Line 2: "));
//  for(uint8_t ii = 0; ii < 17; ii++){
//    Serial.print(F("0x"));
//    Serial.print((uint8_t) g_lcd_buffer[1][ii],HEX);
//    if(ii != 16){
//      Serial.print(F(","));
//    }
//  }
//  Serial.println();
//}

void repaintLCD(void) {
    //dumpDisplayBuffer();
    //  if(strlen((char *) &(g_lcd_buffer[0])) <= 16){
    //    lcd.setCursor(0,0);
    //    lcd.print((char *) &(g_lcd_buffer[0]));
    //  }
    //
    //  if(strlen((char *) &(g_lcd_buffer[1])) <= 16){
    //    lcd.setCursor(0,1);
    //    lcd.print((char *) &(g_lcd_buffer[1]));
    //  }

    char tmp[2] = " ";
    for(uint8_t line = 0; line < 2; line++) {
        for(uint8_t column = 0; column < 16; column++) {
            if(last_painted[line][column] != g_lcd_buffer[line][column]) {
                tmp[0] = g_lcd_buffer[line][column];
                lcd.setCursor(column, line);
                lcd.print(tmp);
            }
            last_painted[line][column] = g_lcd_buffer[line][column];
        }
    }

    g_lcd_buffer[0][16] = '\0'; // ensure null termination
    g_lcd_buffer[1][16] = '\0'; // ensure null termination
}

void setLCD(const char * str) {
    clearLCD();
    uint16_t original_length = strlen(str);
    strncpy((char *) &(g_lcd_buffer[0]), str, 16);
    if(original_length > 16) {
        strncpy((char *) &(g_lcd_buffer[1]), str + 16, 16);
    }
    repaintLCD();
}

void updateLCD(const char * str, uint8_t pos_x, uint8_t pos_y, uint8_t num_chars) {
    updateLCD(str, pos_x, pos_y, num_chars, true);
}

void updateLCD(const char * str, uint8_t pos_x, uint8_t pos_y, uint8_t num_chars, boolean repaint) {
    uint16_t len = strlen(str);
    char * ptr = 0;
    if((pos_y == 0) || (pos_y == 1)) {
        ptr = (char *) &(g_lcd_buffer[pos_y]);
    }

    uint8_t x = 0;  // display buffer index
    uint8_t ii = 0; // input string index
    for(x = pos_x, ii = 0;  (x < 16) && (ii < len) && (ii < num_chars); x++, ii++) {
        // don't allow the injection of non-printable characters into the display buffer
        if(isprint(str[ii])) {
            ptr[x] = str[ii];
        }
        else {
            ptr[x] = ' ';
        }
    }

    if(repaint) {
        repaintLCD();
    }
}

void clearLCD() {
    clearLCD(true);
}

void clearLCD(boolean repaint) {
    memset((uint8_t *) &(g_lcd_buffer[0]), ' ', 16);
    memset((uint8_t *) &(g_lcd_buffer[1]), ' ', 16);
    memset((uint8_t *) &(last_painted[0]), ' ', 16);
    memset((uint8_t *) &(last_painted[1]), ' ', 16);

    g_lcd_buffer[0][16] = '\0';
    g_lcd_buffer[1][16] = '\0';
    last_painted[0][16] = '\0';
    last_painted[1][16] = '\0';

    lcd.clear();
    if(repaint) {
        repaintLCD();
    }
}

boolean index_of(char ch, char * str, uint16_t * index) {
    uint16_t len = strlen(str);
    for(uint16_t ii = 0; ii < len; ii++) {
        if(str[ii] == ch) {
            *index = ii;
            return true;
        }
    }

    return false;
}

void ltrim_string(char * str) {
    uint16_t num_leading_spaces = 0;
    uint16_t len = strlen(str);
    for(uint16_t ii = 0; ii < len; ii++) {
        if(!isspace(str[ii])) {
            break;
        }
        num_leading_spaces++;
    }

    if(num_leading_spaces > 0) {
        // copy the string left, including the null terminator
        // which is why this loop is <= len
        for(uint16_t ii = 0; ii <= len; ii++) {
            str[ii] = str[ii + num_leading_spaces];
        }
    }
}

void rtrim_string(char * str) {
    // starting at the last character in the string
    // overwrite space characters with null characteres
    // until you reach a non-space character
    // or you overwrite the entire string
    int16_t ii = strlen(str) - 1;
    while(ii >= 0) {
        if(isspace(str[ii])) {
            str[ii] = '\0';
        }
        else {
            break;
        }
        ii--;
    }
}

void trim_string(char * str) {
    ltrim_string(str);

    //Serial.print(F("ltrim: "));
    //Serial.println(str);

    rtrim_string(str);

    //Serial.print(F("rtrim: "));
    //Serial.println(str);
}

void replace_nan_with_null(char * str) {
    if(strcmp(str, "nan") == 0) {
        strcpy(str, "null");
    }
}

void replace_character(char * str, char find_char, char replace_char) {
    uint16_t len = strlen(str);
    for(uint16_t ii = 0; ii < len; ii++) {
        if(str[ii] == find_char) {
            str[ii] = replace_char;
        }
    }
}

// returns false if truncating the string to the field width
// would result in alter the whole number part of the represented value
// otherwise truncates the string to the field_width and returns true
boolean truncate_float_string(char * str, uint8_t field_width) {
    // if there is a decimal point in the string after the field_width character
    // the value doesn't fit on a line at all, let alone after truncation
    // examples for field_width := 3
    //             v-- field boundardy (for field_width := 3)
    // Case 0:  0.3|4  is ok (and will be truncated to 0.3)
    // Case 0b: 0. |   is ok (and will be truncated to 0)
    // Case 0c: -0.|3  is ok (and will be truncated to 0)
    // Case 1:  1.2|4  is ok (and will be truncated to 1.2)
    // Case 1b: 1. |   is ok (and will be truncated to 1)
    // Case 1c: 1  |   is ok (and will be truncated to 1)
    // Case 2b: 12.|5  is ok (and will be truncated to 13)
    // Cas3 2c: 12.|   is ok (and will be truncated to 12)
    // Cas3 2d: 12 |   is ok (and will be truncated to 12)
    // Case 3:  123|.4 is ok (and will be truncated to 123)
    // Case 3b: 123|.  is ok (and will be truncated to 123)
    // Case 3c: 123|   is ok (and will be truncated to 123)
    // Case 3d: -12|3  is not ok (because it would be truncated to -12)
    // Case 3f: -12|3. is not ok (because it would be truncated to -12)
    // Case 4:  123|4  is not ok (because it would be truncated to 123)
    // Case 4b: 123|4. is not ok (because it would be truncated to 123)
    //          012|345678901234567 (index for reference)

    uint16_t period_index = 0;

    // first trim the string to remove leading and trailing ' ' characters
    trim_string(str);

    uint16_t len = strlen(str);
    boolean string_contains_decimal_point = index_of('.', str, &period_index);

    //Serial.print(F("len > field_width: "));
    //Serial.print(len);
    //Serial.print(F(" > "));
    //Serial.print(field_width);
    //Serial.print(F(", string_contains_decimal_point = "));
    //Serial.print(string_contains_decimal_point);
    //Serial.print(F(", period_index = "));
    //Serial.println(period_index);

    if(len > field_width) {
        if(string_contains_decimal_point) {
            // there's a decimal point in the string somewhere
            // and the string is longer than the field width
            if(period_index > field_width) {
                // the decimal point occurs at least
                // two characters past the field boundary
                return false;
            }
        }
        else {
            // it's a pure whole number
            // and there's not enough room in the field to hold it
            return false;
        }
    }

    // first truncate the string to the field width if it's longer than the field width
    if(len > field_width) {
        str[field_width] = '\0';
        //Serial.print(F("truncated step 1: "));
        //Serial.println(str);
    }

    len = strlen(str);
    // if the last character in the string is a decimal point, lop it off
    if((len > 0) && (str[len-1] == '.')) {
        str[len-1] = '\0';
        //Serial.print(F("truncated step 2:"));
        //Serial.println(str);
    }

    // it's already adequately truncated if len <= field_width
    return true;
}

// the caller must ensure that there is adequate memory
// allocated to str, so that it can be safely padded
// to target length
void leftpad_string(char * str, uint16_t target_length) {
    uint16_t len = strlen(str);
    if(len < target_length) {
        uint16_t pad_amount = target_length - len;

        // shift the string (including the null temrinator) right by the pad amount
        // by walking backwards from the end to the start
        // and copying characters over (copying the null terminator is why it starts at len)
        for(int16_t ii = len; ii >= 0; ii--) {
            str[ii + pad_amount] = str[ii];
        }

        // then put spaces in the front
        for(uint16_t ii = 0; ii < pad_amount; ii++) {
            str[ii] = ' ';
        }
    }
}

void updateLCD(float value, uint8_t pos_x, uint8_t pos_y, uint8_t field_width) {
    updateLCD(value, pos_x, pos_y, field_width, true);
}

void updateLCD(float value, uint8_t pos_x, uint8_t pos_y, uint8_t field_width, boolean repaint) {
    static char tmp[64] = {0};
    static char asterisks_field[17] = {0};
    memset(tmp, 0, 64);
    memset(asterisks_field, 0, 17);

    for(uint8_t ii = 0; (ii < field_width) && (ii < 16); ii++) {
        asterisks_field[ii] = '*';
    }

    //Serial.print(F("value: "));
    //Serial.println(value,8);

    safe_dtostrf(value, -16, 6, tmp, 16);

    //Serial.print(F("dtostrf: "));
    //Serial.println(tmp);

    if(!truncate_float_string(tmp, field_width)) {
        updateLCD(asterisks_field, pos_x, pos_y, field_width);
        return;
    }

    //Serial.print(F("truncate: "));
    //Serial.println(tmp);

    leftpad_string(tmp, field_width);
    //Serial.print(F("leftpad_string: "));
    //Serial.println(tmp);

    updateLCD(tmp, pos_x, pos_y, field_width, repaint);
}

void updateLCD(uint32_t ip, uint8_t line_number) {
    char tmp[17] = {0};
    snprintf(tmp, 16, "%d.%d.%d.%d",
             (uint8_t)(ip >> 24),
             (uint8_t)(ip >> 16),
             (uint8_t)(ip >> 8),
             (uint8_t)(ip >> 0));

    updateLCD(tmp, line_number);
}

void updateLCD(const char * str, uint8_t line_number) {
    // center the string on the line
    char tmp[17] = {0};
    uint16_t original_len = strlen(str);
    if(original_len < 16) {
        uint8_t num_empty_chars_on_line = 16 - original_len;
        // pad the front of the string with spaces
        uint8_t half_num_empty_chars_on_line = num_empty_chars_on_line / 2;
        for(uint8_t ii = 0; ii < half_num_empty_chars_on_line; ii++) {
            tmp[ii] = ' ';
        }
    }
    uint16_t len = strlen(tmp);  // length of the front padding
    if((original_len + len) <= 16) {
        strcat(tmp, str); // concatenate the string into the front padding-
    }

    len = strlen(tmp);
    if(len < 16) {
        // pad the tail of the string with spaces
        uint8_t num_trailing_spaces = 16 - len;
        for(uint8_t ii = 0; ii < num_trailing_spaces; ii++) {
            tmp[len + ii] = ' ';
        }
    }

    if(line_number < 2) {
        updateLCD(tmp, 0, line_number, 16);
    }
}

void updateLCD(int32_t value, uint8_t pos_x, uint8_t pos_y, uint8_t num_chars) {
    char tmp[17] = {0};
    snprintf(tmp, num_chars, "%ld", value);
    updateLCD(tmp, pos_x, pos_y, num_chars);
}

void updateLCD(char value, uint8_t pos_x, uint8_t pos_y, uint8_t num_chars) {
    char tmp[17] = {0};
    tmp[0] = value;
    updateLCD(tmp, pos_x, pos_y, num_chars);
}

void updateCornerDot(void) {
    static uint8_t on = 0;
    on = 1 - on;
    if(on == 1) {
        updateLCD('.', 15, 1, 1);
    }
    else {
        updateLCD(' ', 15, 1, 1);
    }
}

void updateLcdProgressDots(void) {
    static uint8_t cnt = 0;
    cnt++;
    uint8_t num_dots = cnt % 4;
    switch(num_dots) {
    case 0:
        updateLCD("   ", 1);
        break;
    case 1:
        updateLCD(".  ", 1);
        break;
    case 2:
        updateLCD(".. ", 1);
        break;
    case 3:
        updateLCD("...", 1);
        break;
    }
}

/****** WIFI SUPPORT FUNCTIONS ******/
void displayRSSI(void) {
    char ssid[33] = {0};
    static ap_scan_result_t res = {0};
    int8_t max_rssi = -256;
    boolean found_ssid = false;
    uint8_t target_network_secMode = WLAN_SEC_AUTO;
    uint8_t network_security_mode = eeprom_read_byte((const uint8_t *) EEPROM_SECURITY_MODE);
    uint8_t num_results_found = 0;

    eeprom_read_block(ssid, (const void *) EEPROM_SSID, 32);

    setLCD_P(PSTR(" SCANNING WI-FI "
                  "                "));
    Serial.println(F("Info: Beginning Network Scan..."));
    SUCCESS_MESSAGE_DELAY();

    boolean foundSSID = false;
    uint8_t num_scan_attempts = 0;
    do {
        foundSSID = esp.scanForAccessPoint(ssid, &res, &num_results_found);
        num_scan_attempts++;
        if(!foundSSID) {
            delay(100);
        }
    } while(!foundSSID && (num_scan_attempts <= 3));

    Serial.print(F("Info: Network Scan found "));
    Serial.print(num_results_found);
    Serial.println(F(" networks"));

    Serial.print(F("Info: Access Point \""));
    Serial.print(ssid);
    Serial.print(F("\", "));
    if(foundSSID) {
        target_network_secMode = res.security;
        Serial.print(F("RSSI = "));
        Serial.println(res.rssi);
        int8_t rssi_dbm = res.rssi;
        lcdBars(rssi_to_bars(rssi_dbm));
        lcdSmiley(15, 1); // lower right corner

        if(network_security_mode == WLAN_SEC_AUTO) {
            allowed_to_write_config_eeprom = true;
            if(target_network_secMode == 0) {
                set_network_security_mode("open");
            }
            else if(target_network_secMode == 1) {
                set_network_security_mode("wep");
            }
            else if(target_network_secMode == 2) {
                set_network_security_mode("wpa");
            }
            else if((target_network_secMode == 3) || (target_network_secMode == 4)) {
                set_network_security_mode("wpa2");
            }
            allowed_to_write_config_eeprom = false;
        }

        ERROR_MESSAGE_DELAY(); // ERROR is intentional here, to get a longer delay
    }
    else {
        Serial.println(F("Not Found."));
        Serial.println(F("Info: Attempting to connect anyway."));
        updateLCD("NOT FOUND", 1);
        lcdFrownie(15, 1); // lower right corner
        ERROR_MESSAGE_DELAY();
    }
}

uint8_t rssi_to_bars(int8_t rssi_dbm) {
    uint8_t num_bars = 0;
    if (rssi_dbm < -87) {
        num_bars = 0;
    }
    else if (rssi_dbm < -82) {
        num_bars = 1;
    }
    else if (rssi_dbm < -77) {
        num_bars = 2;
    }
    else if (rssi_dbm < -72) {
        num_bars = 3;
    }
    else if (rssi_dbm < -67) {
        num_bars = 4;
    }
    else {
        num_bars = 5;
    }

    return num_bars;
}

boolean restartWifi() {
    if(!connectedToNetwork()) {
        delayForWatchdog();
        petWatchdog();
        current_millis = millis();
        reconnectToAccessPoint();
        current_millis = millis();
        delayForWatchdog();
        petWatchdog();
        acquireIpAddress();
        current_millis = millis();
        delayForWatchdog();
        petWatchdog();
        displayConnectionDetails();

        clearLCD();
    }

    return connectedToNetwork();
}

bool displayConnectionDetails(void) {
    uint32_t ipAddress, netmask, gateway;

    if(!esp.getIPAddress(&ipAddress, &gateway, &netmask))
    {
        Serial.println(F("Error: Unable to retrieve the IP Address!"));
        return false;
    }
    else
    {
        char ip_str[16] = {0};

        Serial.print(F("Info: IP Addr: "));
        memset(ip_str, 0, 16);
        esp.IpUint32ToString(ipAddress, &(ip_str[0]));
        Serial.print((char *) ip_str);
        Serial.println();

        Serial.print(F("Info: Netmask: "));
        memset(ip_str, 0, 16);
        esp.IpUint32ToString(netmask, &(ip_str[0]));
        Serial.print((char *) ip_str);
        Serial.println();

        Serial.print(F("Info: Gateway: "));
        memset(ip_str, 0, 16);
        esp.IpUint32ToString(gateway, &(ip_str[0]));
        Serial.print((char *) ip_str);
        Serial.println();

        updateLCD(ipAddress, 1);
        lcdSmiley(15, 1); // lower right corner
        SUCCESS_MESSAGE_DELAY();

        return true;
    }
}

void reconnectToAccessPoint(void) {
    static char ssid[33] = {0};
    static char network_password[32] = {0};
    static uint8_t connect_method = 0;
    static uint8_t network_security_mode = 0;
    static boolean first_access = true;
    static uint8_t mac_address[6] = {0};
    if(first_access) {
        first_access = false;
        connect_method = eeprom_read_byte((const uint8_t *) EEPROM_CONNECT_METHOD);
        network_security_mode = eeprom_read_byte((const uint8_t *) EEPROM_SECURITY_MODE);
        eeprom_read_block(ssid, (const void *) EEPROM_SSID, 32);
        eeprom_read_block(network_password, (const void *) EEPROM_NETWORK_PWD, 31);
        eeprom_read_block(mac_address, (const void *) EEPROM_MAC_ADDRESS, 6);
    }

    esp.reset();
    esp.setNetworkMode(1);

    if (!esp.setMacAddress(mac_address)) {
        Serial.println(F("Error: Failed to write MAC address to ESP8266"));
    }

    switch(connect_method) {
    case CONNECT_METHOD_DIRECT:
        Serial.print(F("Info: Connecting to Access Point with SSID \""));
        Serial.print(ssid);
        Serial.print(F("\"..."));
        setLCD_P(PSTR("CONNECTING TO AP"));
        updateLCD(ssid, 1);
        delayForWatchdog();
        petWatchdog();

        if(!esp.connectToNetwork((char *) ssid, (char *) network_password, 30000)) {
            Serial.print(F("Error: Failed to connect to Access Point with SSID: "));
            Serial.println(ssid);
            Serial.flush();
            updateLCD("FAILED", 1);
            lcdFrownie(15, 1);
            ERROR_MESSAGE_DELAY();
            // watchdogForceReset();
            wdt_reset_pending = true;
            return;
        }

        // if your configured for static ip address then setStaticIP
        // otherwise setDHCP

        Serial.println(F("OK."));
        updateLCD("CONNECTED", 1);
        lcdSmiley(15, 1);
        SUCCESS_MESSAGE_DELAY();
        break;
    default:
        Serial.println(F("Error: Connection method not currently supported"));
        break;
    }
}

void acquireIpAddress(void) {
    static boolean first_access = true;
    static uint32_t static_ip = 0;
    static uint32_t static_gateway = 0;
    static uint32_t static_netmask = 0;

    uint8_t noip[4] = {0};

    if(first_access) {
        uint8_t ip[4] = {0};
        first_access = false;
        eeprom_read_block(ip, (const void *) EEPROM_STATIC_IP_ADDRESS, 4);
        static_ip = esp.IpArrayToIpUint32((uint8_t *) &(ip[0]));
        eeprom_read_block(ip, (const void *) EEPROM_STATIC_NETMASK, 4);
        static_netmask = esp.IpArrayToIpUint32((uint8_t *) &(ip[0]));
        eeprom_read_block(ip, (const void *) EEPROM_STATIC_GATEWAY, 4);
        static_gateway = esp.IpArrayToIpUint32((uint8_t *) &(ip[0]));
    }

    // if it's DHCP we're configured for, engage DHCP process
    if (static_ip == 0) {
        /* Wait for DHCP to complete */
        Serial.print(F("Info: Request DHCP..."));
        setLCD_P(PSTR(" REQUESTING IP  "));

        const long dhcp_timeout_duration_ms = 60000L;
        unsigned long previous_dhcp_timeout_millis = current_millis;
        uint32_t ii = 0;
        if(esp.setDHCP()) {
            Serial.println(F("OK."));
        }
        else {
            Serial.println(F("Error: Failed to acquire IP address via DHCP. Rebooting."));
            Serial.flush();
            updateLCD("FAILED", 1);
            lcdFrownie(15, 1);
            ERROR_MESSAGE_DELAY();
            // watchdogForceReset();
            wdt_reset_pending = true;
            return;
        }
    }
    else {
        Serial.print(F("Info: Setting Static IP configuration..."));
        if(!esp.setStaticIPAddress(static_ip, static_netmask, static_gateway, 0)) {
            Serial.println(F("Failed."));
        }
        else {
            Serial.println(F("OK."));
        }
    }
}

boolean connectedToNetwork(void) {
    return esp.connectedToNetwork();
}

void espIpToArray(uint32_t ip, uint8_t * ip_array) {
    for(uint8_t ii = 0; ii < 4; ii++) {
        ip_array[ii] = ip & 0xff;
        ip >>= 8;
    }
}

uint32_t arrayToESP8266Ip(uint8_t * ip_array) {
    uint32_t ip = 0;
    for(int8_t ii = 3; ii > 0; ii++) {
        ip |= ip_array[ii];
        ip <<= 8;
    }
    return ip;
}

/****** MQTT SUPPORT FUNCTIONS ******/
void clearTempBuffers(void) {
    memset(converted_value_string, 0, 64);
    memset(compensated_value_string, 0, 64);
    memset(raw_instant_value_string, 0, 64);
    memset(raw_value_string, 0, 64);
    memset(scratch, 0, SCRATCH_BUFFER_SIZE);
    scratch_idx = 0;
    memset(MQTT_TOPIC_STRING, 0, 128);
    memset(response_body, 0, 256);




}

boolean mqttResolve(void) {
    uint32_t ip = 0;
    static boolean resolved = false;

    char mqtt_server_name[32] = {0};
    if(!resolved) {
        eeprom_read_block(mqtt_server_name, (const void *) EEPROM_MQTT_SERVER_NAME, 31);
        setLCD_P(PSTR("   RESOLVING"));
        updateLCD("MQTT SERVER", 1);
        SUCCESS_MESSAGE_DELAY();
        if  (!esp.getHostByName(mqtt_server_name, &ip) || (ip == 0))  {
            Serial.print(F("Error: Couldn't resolve '"));
            Serial.print(mqtt_server_name);
            Serial.println(F("'"));

            updateLCD("FAILED", 1);
            lcdFrownie(15, 1);
            ERROR_MESSAGE_DELAY();
            return false;
        }
        else {
            resolved = true;
            espIpToArray(ip, mqtt_server_ip);
            Serial.print(F("Info: Resolved \""));
            Serial.print(mqtt_server_name);
            Serial.print(F("\" to IP address "));
            char ip_str[16] = {0};
            esp.IpUint32ToString(ip, &(ip_str[0]));
            Serial.print((char *) ip_str);

            updateLCD(ip, 1);
            lcdSmiley(15, 1);
            SUCCESS_MESSAGE_DELAY();
            Serial.println();
        }
    }
    return true;
}

boolean mqttDisconnect(void) {
    Serial.print(F("Info: Disconnecting from MQTT Server..."));
    mqtt_client.disconnect();
    Serial.println("OK.");
}

boolean mqttReconnect(void) {
    static boolean first_access = true;
    static char mqtt_username[32] = {0};
    static char mqtt_password[32] = {0};
    static uint8_t mqtt_auth_enabled = 0;
    static uint32_t mqtt_port = 0;

    boolean loop_return_flag = true;

    if(!mqttResolve()) {
        return false;
    }

    if(first_access) {
        first_access = false;
        loop_return_flag = false;
        eeprom_read_block(mqtt_username, (const void *) EEPROM_MQTT_USERNAME, 31);
        eeprom_read_block(mqtt_client_id, (const void *) EEPROM_MQTT_CLIENT_ID, 31);
        eeprom_read_block(mqtt_password, (const void *) EEPROM_MQTT_PASSWORD, 31);
        eeprom_read_block(MQTT_TOPIC_PREFIX, (const void *) EEPROM_MQTT_TOPIC_PREFIX, 63);
        mqtt_suffix_enabled = eeprom_read_byte((const uint8_t *) EEPROM_MQTT_TOPIC_SUFFIX_ENABLED);
        mqtt_auth_enabled = eeprom_read_byte((const uint8_t *) EEPROM_MQTT_AUTH);
        mqtt_port = eeprom_read_dword((const uint32_t *) EEPROM_MQTT_PORT);

        mqtt_client.setServer(mqtt_server_ip, mqtt_port);
        mqtt_client.setClient(esp);
    }
    else {
        loop_return_flag = mqtt_client.loop();
    }

    if(!loop_return_flag) {
        Serial.print(F("Info: Connecting to MQTT Broker with Client ID \""));
        Serial.print(mqtt_client_id);
        Serial.print(F("\" "));
        boolean connect_status = false;
        if(mqtt_auth_enabled) {
            Serial.print(F("using Authentication..."));
            connect_status = mqtt_client.connect(mqtt_client_id, mqtt_username, mqtt_password);
        }
        else {
            Serial.print(F("Without Authentication..."));
            connect_status = mqtt_client.connect(mqtt_client_id);
        }

        if (connect_status) {
            Serial.println(F("OK."));
            return true;
        }
        else {
            Serial.println(F("Failed."));
            return false;
        }
    }

    return loop_return_flag;
}

boolean mqttPublish(char * topic, char *str) {
    // try to reconnect up to 10 times
    uint8_t num_publish_attempts = 0;
    uint8_t publish_worked = false;
    boolean response_status = true;

    uint32_t space_required = 5;
    space_required += strlen(topic);
    space_required += strlen(str);

    do {
        Serial.print(F("Info: MQTT publishing to topic "));
        Serial.print(topic);
        Serial.print(F("..."));

        if(space_required >= 1023) {
            Serial.println(F("Aborted."));
            response_status = false;
            break;
        }
        publish_worked = mqtt_client.publish(topic, str);
        if(publish_worked) {
            Serial.println(F("OK."));
            response_status = true;
        }
        else {
            Serial.println(F("Failed."));
            uint8_t num_reconnect_attempts = 0;
            uint8_t reconnect_worked = false;
            do {
                if(!reconnect_worked) {
                    mqtt_client.disconnect();
                    delay(200);
                }
                reconnect_worked = mqttReconnect();
                num_reconnect_attempts++;
            } while (!reconnect_worked && (num_reconnect_attempts < 5));
            response_status = false;
        }

        num_publish_attempts++;
        if(!publish_worked) {
            delay(200);
        }
    } while(!publish_worked && (num_publish_attempts < 5));

    // removed because doing this seems to cause
    // overlapping ping handling during publishing
    // which for some reason the ESP_AT_Client code
    // doesn't handle gracefully
    // mqtt_client.loop();

    return response_status;
}


boolean publishHeartbeat() {
    clearTempBuffers();
    static uint32_t post_counter = 0;
    uint8_t sample = pgm_read_byte(&heartbeat_waveform[heartbeat_waveform_index++]);

    char hasPressureString[16] = {0};
    if(init_bmp280_ok) {
        strcpy_P(hasPressureString,PSTR(",\"pressure\""));
    }

    snprintf(scratch, SCRATCH_BUFFER_SIZE-1,
             "{"
             "\"serial-number\":\"%s\","
             "\"converted-value\":%d,"
             "\"firmware-version\":\"%s\","
             "\"publishes\":[\"co2\",\"particulate\",\"voc\",\"temperature\",\"humidity\"%s],"
             "\"counter\":%lu"
             "}", mqtt_client_id, sample, firmware_version, hasPressureString, post_counter++);

    if(heartbeat_waveform_index >= NUM_HEARTBEAT_WAVEFORM_SAMPLES) {
        heartbeat_waveform_index = 0;
    }

    replace_character(scratch, '\'', '\"');

    strcat(MQTT_TOPIC_STRING, MQTT_TOPIC_PREFIX);
    strcat(MQTT_TOPIC_STRING, "heartbeat");
    if(mqtt_suffix_enabled) {
        strcat(MQTT_TOPIC_STRING, "/");
        strcat(MQTT_TOPIC_STRING, mqtt_client_id);
    }
    return mqttPublish(MQTT_TOPIC_STRING, scratch);
}

float toFahrenheit(float degC) {
    return  (degC * 9.0f / 5.0f) + 32.0f;
}

boolean publishTemperature() {
    clearTempBuffers();
    temperature_degc = instant_temperature_degc;
    float raw_temperature = temperature_degc;
    float reported_temperature = temperature_degc - reported_temperature_offset_degC;
    if(temperature_units == 'F') {
        reported_temperature = toFahrenheit(reported_temperature);
        raw_temperature = toFahrenheit(raw_temperature);
        safe_dtostrf(toFahrenheit(instant_temperature_degc), -6, 2, raw_instant_value_string, 16);
    }
    else {
        safe_dtostrf(instant_temperature_degc, -6, 2, raw_instant_value_string, 16);
    }
    safe_dtostrf(reported_temperature, -6, 2, converted_value_string, 16);
    safe_dtostrf(raw_temperature, -6, 2, raw_value_string, 16);

    trim_string(converted_value_string);
    trim_string(raw_value_string);
    trim_string(raw_instant_value_string);

    replace_nan_with_null(converted_value_string);
    replace_nan_with_null(raw_value_string);
    replace_nan_with_null(raw_instant_value_string);

    snprintf(scratch, SCRATCH_BUFFER_SIZE-1,
             "{"
             "\"serial-number\":\"%s\","
             "\"converted-value\":%s,"
             "\"converted-units\":\"deg%c\","
             "\"raw-value\":%s,"
             "\"raw-units\":\"deg%c\","
             "\"sensor-part-number\":\"SHT25\""
             "%s"
             "}",
             mqtt_client_id,
             converted_value_string,
             temperature_units,
             raw_value_string,
             temperature_units,
             gps_mqtt_string);

    replace_character(scratch, '\'', '\"');

    strcat(MQTT_TOPIC_STRING, MQTT_TOPIC_PREFIX);
    strcat(MQTT_TOPIC_STRING, "temperature");
    if(mqtt_suffix_enabled) {
        strcat(MQTT_TOPIC_STRING, "/");
        strcat(MQTT_TOPIC_STRING, mqtt_client_id);
    }

    return mqttPublish(MQTT_TOPIC_STRING, scratch);
}

boolean publishHumidity() {
    clearTempBuffers();
    relative_humidity_percent = instant_humidity_percent;
    float raw_humidity = constrain(relative_humidity_percent, 0.0f, 100.0f);
    float reported_humidity = constrain(relative_humidity_percent - reported_humidity_offset_percent, 0.0f, 100.0f);

    safe_dtostrf(reported_humidity, -6, 2, converted_value_string, 16);
    safe_dtostrf(raw_humidity, -6, 2, raw_value_string, 16);
    safe_dtostrf(instant_humidity_percent, -6, 2, raw_instant_value_string, 16);

    trim_string(converted_value_string);
    trim_string(raw_value_string);
    trim_string(raw_instant_value_string);

    replace_nan_with_null(converted_value_string);
    replace_nan_with_null(raw_value_string);
    replace_nan_with_null(raw_instant_value_string);

    snprintf(scratch, SCRATCH_BUFFER_SIZE-1,
             "{"
             "\"serial-number\":\"%s\","
             "\"converted-value\":%s,"
             "\"converted-units\":\"percent\","
             "\"raw-value\":%s,"
             "\"raw-units\":\"percent\","
             "\"sensor-part-number\":\"SHT25\""
             "%s"
             "}",
             mqtt_client_id,
             converted_value_string,
             raw_value_string,
             gps_mqtt_string);

    replace_character(scratch, '\'', '\"');

    strcat(MQTT_TOPIC_STRING, MQTT_TOPIC_PREFIX);
    strcat(MQTT_TOPIC_STRING, "humidity");
    if(mqtt_suffix_enabled) {
        strcat(MQTT_TOPIC_STRING, "/");
        strcat(MQTT_TOPIC_STRING, mqtt_client_id);
    }
    return mqttPublish(MQTT_TOPIC_STRING, scratch);
}

void collectTemperature(void) {
    if(init_sht25_ok) {
        if(sht25.getTemperature(&instant_temperature_degc)) {
            temperature_ready = true;
        }
    }
}

void collectHumidity(void) {
    if(init_sht25_ok) {
        if(sht25.getRelativeHumidity(&instant_humidity_percent)) {
            humidity_ready = true;
        }
    }
}

// TODO: create a vector to collect a touch sample every half second or so
//       and move all calls to collectTouch out of main processing into vector

void collectTouch(void) {
    static uint8_t sample_write_index = 0;
    int32_t t = touch.capacitiveSensor(5);
    // Serial.print("\nt: "); Serial.println(t);
    touch_sample_buffer[sample_write_index++] = t;
    if(sample_write_index == TOUCH_SAMPLE_BUFFER_DEPTH) {
        sample_write_index = 0;
    }
}

boolean processTouchVerbose(boolean verbose_output) {
    const uint32_t touch_event_threshold = 12UL;
    static boolean first_time = true;
    static unsigned long touch_start_millis = 0UL;
    long backlight_interval = 60000L;
    static boolean backlight_is_on = false;
    boolean ret = false;

    if(first_time) {
        first_time = false;
        backlight_interval = ((long) eeprom_read_word((uint16_t *) EEPROM_BACKLIGHT_DURATION)) * 1000;
    }

    float touch_moving_average = calculateAverage(touch_sample_buffer, TOUCH_SAMPLE_BUFFER_DEPTH);
    if(verbose_output) {
        Serial.print(F("Info: Average Touch Reading: "));
        Serial.println(touch_moving_average);
    }

    if(touch_moving_average > touch_event_threshold) {
        backlightOn();
        backlight_is_on = true;
        if(verbose_output) {
            Serial.println(F("Info: Turning backlight on."));
        }
        touch_start_millis = current_millis;
        ret = true;
    }

    if((current_millis - touch_start_millis) >= backlight_interval) {
        if(backlight_is_on) {
            if(verbose_output) {
                Serial.println(F("Info: Turning backlight off (timer expired)."));
            }
            backlightOff();
            backlight_is_on = false;
        }
    }

    return ret;
}

boolean processTouch(void) {
    return processTouchVerbose(true);
}

boolean processTouchQuietly(void) {
    return processTouchVerbose(false);
}

void advanceSampleBufferIndex(void) {
    sample_buffer_idx++;
    if((sample_buffer_idx >= sample_buffer_depth) || (sample_buffer_idx >= MAX_SAMPLE_BUFFER_DEPTH)) {
        sample_buffer_idx = 0;
    }
}

void addSample(uint8_t sample_type, float value) {
    if((sample_type < NUM_SAMPLE_BUFFERS) && (sample_buffer_idx < MAX_SAMPLE_BUFFER_DEPTH)) {
        sample_buffer[sample_type][sample_buffer_idx] = value;
    }
}

void collectPressure(void) {
    static boolean first = true;
    if(!init_bmp280_ok && first) {
        init_bmp280_ok = bme.begin();
    }
    first = false;

    if(init_bmp280_ok) {
        instant_pressure_pa = bme.readPressure();
        instant_altitude_m = bme.readAltitude();
        pressure_ready = true;
    }
}

boolean publishPressure() {
    clearTempBuffers();

    safe_dtostrf(instant_pressure_pa, -8, 1, converted_value_string, 16);
    trim_string(converted_value_string);
    replace_nan_with_null(converted_value_string);

    safe_dtostrf(instant_altitude_m, -8, 1, compensated_value_string, 16);
    trim_string(compensated_value_string);
    replace_nan_with_null(compensated_value_string);

    snprintf(scratch, SCRATCH_BUFFER_SIZE-1,
             "{"
             "\"serial-number\":\"%s\","
             "\"pressure-units\":\"Pa\","
             "\"pressure\":%s,"
             "\"altitude-units\":\"m\","
             "\"altitude\":%s,"
             "\"sensor-part-number\":\"BMP280\""
             "%s"
             "}",
             mqtt_client_id,
             converted_value_string,
             compensated_value_string,
             gps_mqtt_string);

    replace_character(scratch, '\'', '\"'); // replace single quotes with double quotes

    strcat(MQTT_TOPIC_STRING, MQTT_TOPIC_PREFIX);
    strcat(MQTT_TOPIC_STRING, "pressure");
    if(mqtt_suffix_enabled) {
        strcat(MQTT_TOPIC_STRING, "/");
        strcat(MQTT_TOPIC_STRING, mqtt_client_id);
    }
    return mqttPublish(MQTT_TOPIC_STRING, scratch);
}

void appendAsJSON(char * tgt, char * key, float value, boolean trailing_comma) {
    strcat(tgt, "\"");
    strcat(tgt, key);
    strcat(tgt, "\":");

    memset(converted_value_string, 0, 64);
    safe_dtostrf(value, -8, 1, converted_value_string, 16);
    trim_string(converted_value_string);
    replace_nan_with_null(converted_value_string);

    strcat(tgt, converted_value_string);
    if(trailing_comma) {
        strcat(tgt, ",");
    }
}

void petWatchdog(void) {
    tinywdt.pet();
}

void delayForWatchdog(void) {
    delay(120);
}

void watchdogForceReset(void) {
    watchdogForceReset(true);
}

void watchdogForceReset(boolean changeLCD) {
    Serial.println(F("Info: Attempting Watchdog Forced Restart."));
    if(changeLCD) {
        setLCD_P(PSTR("   ATTEMPTING   "
                      "  FORCED RESET  "));
        backlightOn();
        ERROR_MESSAGE_DELAY();
    }

    tinywdt.force_reset();

    for(;;) {
        delay(1000);
        Serial.println(F("Error: Watchdog Force Restart failed. Manual may be required."));
        soft_restart();

        setLCD_P(PSTR("AUTORESET FAILED"
                      " RESET REQUIRED "));
        backlightOn();
        ERROR_MESSAGE_DELAY();
    }
}

void watchdogInitialize(void) {
    tinywdt.begin(100, 65000);
    delay(50);
}

// modal operation loop functions
void loop_wifi_mqtt_mode(void) {
    static uint8_t num_mqtt_connect_retries = 0;
    static uint8_t num_mqtt_intervals_without_wifi = 0;
    static uint8_t publish_counter = 0;
    static boolean first = true;

    // mqtt publish timer intervals
    static unsigned long previous_mqtt_publish_millis = 0;

    if(mqtt_stay_connected) {
        mqttReconnect(); // mqtt_client.loop gets called in here
    }

    if(first || (current_millis - previous_mqtt_publish_millis >= reporting_interval)) {
        previous_mqtt_publish_millis = current_millis;

        if (!first) { // the first time it happens in the main loop
            printCsvDataLine();
        }

        if(connectedToNetwork()) {
            num_mqtt_intervals_without_wifi = 0;

            // try to reconnect up to 10 times
            uint8_t num_reconnect_attempts = 0;
            uint8_t reconnect_worked = false;
            while(!reconnect_worked && (num_reconnect_attempts < 5)) {
                reconnect_worked = mqttReconnect();
                num_reconnect_attempts++;
                if(!reconnect_worked) {
                    mqtt_client.disconnect();
                    delay(200);
                }
            }

            if(reconnect_worked) {
                //connected to MQTT server and connected to Wi-Fi network
                num_mqtt_connect_retries = 0;
                if((publish_counter % 10) == 0) { // only publish heartbeats every 10 reporting intervals
                    if(!publishHeartbeat()) {
                        Serial.println(F("Error: Failed to publish Heartbeat."));
                    }
                }

                if(init_sht25_ok) {
                    if(temperature_ready|| (sample_buffer_idx > 0)) {
                        if(!publishTemperature()) {
                            Serial.println(F("Error: Failed to publish Temperature."));
                        }
                    }
                }

                if(init_sht25_ok) {
                    if(humidity_ready || (sample_buffer_idx > 0)) {
                        if(!publishHumidity()) {
                            Serial.println(F("Error: Failed to publish Humidity."));
                        }
                    }
                }

                if(particulate_ready || (sample_buffer_idx > 0)) {
                    if(!publishParticulate()) {
                        Serial.println(F("Error: Failed to publish Particulate."));
                    }
                }
                if(co2_ready || (sample_buffer_idx > 0)) {
                    if(!publishCO2()) {
                        Serial.println(F("Error: Failed to publish CO2."));
                    }
                }
                if(iaqcore_ready || (sample_buffer_idx > 0)) {
                    if(!publishIAQCore()) {
                        Serial.println(F("Error: Failed to publish iAQ-core."));
                    }
                }

                if(init_bmp280_ok && (pressure_ready || (sample_buffer_idx > 0))) {
                    if(!publishPressure()) {
                        Serial.println(F("Error: Failed to publish Pressure."));
                    }
                }


            }
            else {
                // not connected to MQTT server
                num_mqtt_connect_retries++;
                Serial.print(F("Warn: Failed to connect to MQTT server "));
                Serial.print(num_mqtt_connect_retries);
                Serial.print(F(" consecutive time"));
                if(num_mqtt_connect_retries > 1) {
                    Serial.print(F("s"));
                }
                Serial.println();

                if(num_mqtt_connect_retries >= 5) {
                    Serial.println(F("Error: MQTT Connect Failed 5 consecutive times. Forcing reboot."));
                    Serial.flush();
                    setLCD_P(PSTR("  MQTT SERVER   "
                                  "    FAILURE     "));
                    lcdFrownie(15, 1);
                    ERROR_MESSAGE_DELAY();
                    // watchdogForceReset();
                    wdt_reset_pending = true;
                    return;
                }
            }
        }
        else {
            // not connected to Wi-Fi network
            num_mqtt_intervals_without_wifi++;
            Serial.print(F("Warn: Failed to connect to Wi-Fi network "));
            Serial.print(num_mqtt_intervals_without_wifi);
            Serial.print(F(" consecutive time"));
            if(num_mqtt_intervals_without_wifi > 1) {
                Serial.print(F("s"));
            }
            Serial.println();
            if(num_mqtt_intervals_without_wifi >= 5) {
                Serial.println(F("Error: Wi-Fi Re-connect Failed 5 consecutive times. Forcing reboot."));
                Serial.flush();
                setLCD_P(PSTR(" WI-FI NETWORK  "
                              "    FAILURE     "));
                lcdFrownie(15, 1);
                ERROR_MESSAGE_DELAY();
                // watchdogForceReset();
                wdt_reset_pending = true;
                return true;
            }

            restartWifi();
        }

        publish_counter++;
        if(!mqtt_stay_connected) {
            mqttDisconnect();
        }
    }

    first = false;

}

void loop_offline_mode(void) {

    // write record timer intervals
    static unsigned long previous_write_record_millis = 0;
    static boolean first = true;

    if((current_millis - previous_write_record_millis >= reporting_interval)) {
        previous_write_record_millis = current_millis;
        if (!first) { // first time happens in main loop
            printCsvDataLine();
        }
    }
    first = false;
}

/****** SIGNAL PROCESSING MATH SUPPORT FUNCTIONS ******/

float calculateAverage(float * buf, uint16_t num_samples) {
    float average = 0.0f;
    for(uint16_t ii = 0; ii < num_samples; ii++) {
        average += buf[ii];
    }

    return average / num_samples;
}

void printCsvDataLine() {
    static boolean first = true;
    char * dataString = &(scratch[0]);
    clearTempBuffers();
    static uint32_t call_counter = 0;
    int num_samples;

    uint16_t len = 0;
    uint16_t dataStringRemaining = SCRATCH_BUFFER_SIZE-1;

    if(first) {
        first = false;
        Serial.print(F("csv: "));
        Serial.print(header_row);
        Serial.println();
    }

    Serial.print(F("csv: "));
    printCurrentTimestamp(dataString, &dataStringRemaining);
    Serial.print(F(","));
    appendToString(",", dataString, &dataStringRemaining);

    if(init_sht25_ok && (temperature_ready || (sample_buffer_idx > 0))) {
        temperature_degc = instant_temperature_degc;
        float reported_temperature = temperature_degc - reported_temperature_offset_degC;
        if(temperature_units == 'F') {
            reported_temperature = toFahrenheit(reported_temperature);
        }
        Serial.print(reported_temperature, 2);
        appendToString(reported_temperature, 2, dataString, &dataStringRemaining);
    }
    else {
        Serial.print(F("---"));
        appendToString("---", dataString, &dataStringRemaining);
    }

    Serial.print(F(","));
    appendToString(",", dataString, &dataStringRemaining);

    if(init_sht25_ok && (humidity_ready || (sample_buffer_idx > 0))) {
        relative_humidity_percent = instant_humidity_percent;
        float reported_relative_humidity = constrain(relative_humidity_percent - reported_humidity_offset_percent, 0.0f, 100.0f);
        Serial.print(reported_relative_humidity, 2);
        appendToString(reported_relative_humidity, 2, dataString, &dataStringRemaining);
    }
    else {
        Serial.print(F("---"));
        appendToString("---", dataString, &dataStringRemaining);
    }

    Serial.print(F(","));
    appendToString(",", dataString, &dataStringRemaining);

    float co2_moving_average = 0.0f;
    num_samples = co2_ready ? sample_buffer_depth : sample_buffer_idx;
    if(co2_ready || (sample_buffer_idx > 0)) {
        float converted_value = 0.0f, compensated_value = 0.0f;
        co2_moving_average = calculateAverage(&(sample_buffer[CO2_SAMPLE_BUFFER][0]), num_samples);
        co2_convert_to_ppm(co2_moving_average, &converted_value, &compensated_value);
        co2_ppm = compensated_value;

        Serial.print(co2_moving_average, 1);
        appendToString(co2_moving_average, 1, dataString, &dataStringRemaining);
    }
    else {
        Serial.print(F("---"));
        appendToString("---", dataString, &dataStringRemaining);
    }

    Serial.print(F(","));
    appendToString("," , dataString, &dataStringRemaining);

    float pm1p0_moving_average = 0.0f;
    float pm2p5_moving_average = 0.0f;
    float pm10p0_moving_average = 0.0f;
    num_samples = particulate_ready ? sample_buffer_depth : sample_buffer_idx;
    if(particulate_ready || (sample_buffer_idx > 0)) {
        float converted_value = 0.0f, compensated_value = 0.0f;
        pm1p0_moving_average = calculateAverage(&(sample_buffer[A_PM1P0_SAMPLE_BUFFER][0]), num_samples);
        pm1p0_convert_to_ugpm3(pm1p0_moving_average, &compensated_value);
        pm1p0_ugpm3 = compensated_value;
        pm1p0_moving_average = calculateAverage(&(sample_buffer[B_PM1P0_SAMPLE_BUFFER][0]), num_samples);
        pm1p0_convert_to_ugpm3(pm1p0_moving_average, &compensated_value);
        pm1p0_ugpm3 = (pm1p0_ugpm3 + compensated_value) / 2;

        Serial.print(pm1p0_ugpm3, 1);
        appendToString(pm1p0_ugpm3, 1, dataString, &dataStringRemaining);

        Serial.print(F(","));
        appendToString("," , dataString, &dataStringRemaining);

        pm2p5_moving_average = calculateAverage(&(sample_buffer[A_PM2P5_SAMPLE_BUFFER][0]), num_samples);
        pm2p5_convert_to_ugpm3(pm2p5_moving_average, &compensated_value);
        pm2p5_ugpm3 = compensated_value;
        pm2p5_moving_average = calculateAverage(&(sample_buffer[B_PM2P5_SAMPLE_BUFFER][0]), num_samples);
        pm2p5_convert_to_ugpm3(pm2p5_moving_average, &compensated_value);
        pm2p5_ugpm3 = (pm2p5_ugpm3 + compensated_value) / 2;

        Serial.print(pm2p5_ugpm3, 1);
        appendToString(pm2p5_ugpm3, 1, dataString, &dataStringRemaining);

        Serial.print(F(","));
        appendToString("," , dataString, &dataStringRemaining);

        pm10p0_moving_average = calculateAverage(&(sample_buffer[A_PM10P0_SAMPLE_BUFFER][0]), num_samples);
        pm10p0_convert_to_ugpm3(pm10p0_moving_average, &compensated_value);
        pm10p0_ugpm3 = compensated_value;
        pm10p0_moving_average = calculateAverage(&(sample_buffer[B_PM10P0_SAMPLE_BUFFER][0]), num_samples);
        pm10p0_convert_to_ugpm3(pm10p0_moving_average, &compensated_value);
        pm10p0_ugpm3 = (pm10p0_ugpm3 + compensated_value) / 2;

        Serial.print(pm10p0_ugpm3, 1);
        appendToString(pm10p0_ugpm3, 1, dataString, &dataStringRemaining);
    }
    else {
        Serial.print(F("---"));
        appendToString("---", dataString, &dataStringRemaining);
    }

    Serial.print(F(","));
    appendToString("," , dataString, &dataStringRemaining);

    float eco2_moving_average = 0.0f;
    float tvoc_moving_average = 0.0f;
    float resistance_moving_average = 0.0f;
    num_samples = iaqcore_ready ? sample_buffer_depth : sample_buffer_idx;
    if(iaqcore_ready || (sample_buffer_idx > 0)) {
        float converted_value = 0.0f, compensated_value = 0.0f;
        eco2_moving_average = calculateAverage(&(sample_buffer[CO2_EQUIVALENT_SAMPLE_BUFFER][0]), num_samples);
        co2_equivalent_compensation(eco2_moving_average, &converted_value, &compensated_value);
        co2_equivalent_ppm = compensated_value;

        tvoc_moving_average = calculateAverage(&(sample_buffer[TVOC_SAMPLE_BUFFER][0]), num_samples);
        tvoc_compensation(tvoc_moving_average, &converted_value, &compensated_value);
        tvoc_ppb = compensated_value;

        resistance_moving_average = calculateAverage(&(sample_buffer[RESISTANCE_SAMPLE_BUFFER][0]), num_samples);
        resistance_compensation(resistance_moving_average, &converted_value, &compensated_value);
        resistance_ohms = compensated_value;

        Serial.print(eco2_moving_average, 0);
        appendToString(eco2_moving_average, 0, dataString, &dataStringRemaining);

        Serial.print(F(","));
        appendToString("," , dataString, &dataStringRemaining);

        Serial.print(tvoc_moving_average, 0);
        appendToString(tvoc_moving_average, 0, dataString, &dataStringRemaining);

        Serial.print(F(","));
        appendToString("," , dataString, &dataStringRemaining);

        Serial.print(resistance_moving_average, 0);
        appendToString(resistance_moving_average, 0, dataString, &dataStringRemaining);
    }
    else {
        Serial.print(F("---"));
        appendToString("---", dataString, &dataStringRemaining);

        Serial.print(F(","));
        appendToString("," , dataString, &dataStringRemaining);

        Serial.print(F("---"));
        appendToString("---", dataString, &dataStringRemaining);

        Serial.print(F(","));
        appendToString("," , dataString, &dataStringRemaining);

        Serial.print(F("---"));
        appendToString("---", dataString, &dataStringRemaining);
    }

    Serial.print(F(","));
    appendToString("," , dataString, &dataStringRemaining);





    if(pressure_ready || (sample_buffer_idx > 0)) {
        pressure_pa = instant_pressure_pa;

        Serial.print(instant_pressure_pa, 1);
        appendToString(instant_pressure_pa, 1, dataString, &dataStringRemaining);
    }
    else {
        Serial.print(F("---"));
        appendToString("---", dataString, &dataStringRemaining);
    }

    Serial.print(gps_csv_string);
    appendToString(gps_csv_string, dataString, &dataStringRemaining);

    Serial.println();
    appendToString("\n", dataString, &dataStringRemaining);

    boolean sdcard_write_succeeded = true;
    char filename[16] = {0};
//  if(init_sdcard_ok){
    if (SD.begin(16)) {
        getNowFilename(filename, 15);
        File dataFile = SD.open(filename, FILE_WRITE);
        if(dataFile) {
            dataFile.print(dataString);
            dataFile.close();
        }
        else {
            sdcard_write_succeeded = false;
        }
    }

    if(mode == SUBMODE_OFFLINE) {
        if(((call_counter % 10) == 0) && sdcard_write_succeeded) { // once every 10 reports
            display_offline_mode_banner = true;
        }
        else if(sdcard_write_succeeded) { // otherwise display the data (implied, or no sd card installed)
            display_offline_mode_banner = false;
        }
        else { // if( !sdcard_write_succeeded )
            Serial.print("Error: Failed to open SD card file named \"");
            Serial.print(filename);
            Serial.println(F("\""));
            clearLCD(false);
            setLCD_P(PSTR("  SD CARD FILE  "
                          "  OPEN FAILED   "));
            lcdFrownie(15, 1);
        }

    }

    call_counter++;
}

boolean mode_requires_wifi(uint8_t opmode) {
    boolean requires_wifi = false;

    if(opmode == SUBMODE_NORMAL) {
        requires_wifi = true;
    }

    return requires_wifi;
}

/****** INITIALIZATION SUPPORT FUNCTIONS ******/

// the following defines are what goes in the SPI flash where to signal to the bootloader
#define LAST_4K_PAGE_ADDRESS      0x7F000     // the start address of the last 4k page
#define MAGIC_NUMBER              0x00ddba11  // this word at the end of SPI flash
// is a signal to the bootloader to
// think about loading it
#define MAGIC_NUMBER_ADDRESS      0x7FFFC     // the last 4 bytes are the magic number
#define CRC16_CHECKSUM_ADDRESS    0x7FFFA     // the two bytes before the magic number
// are the expected checksum of the file
#define FILESIZE_ADDRESS          0x7FFF6     // the four bytes before the checksum
// are the stored file size

#define IDLE_TIMEOUT_MS  10000     // Amount of time to wait (in milliseconds) with no data
// received before closing the connection.  If you know the server
// you're accessing is quick to respond, you can reduce this value.

void invalidateSignature(void) {
    flash_file_size = 0;
    flash_signature = 0;
    while(flash.busy()) {
        ;
    }
    flash.blockErase4K(LAST_4K_PAGE_ADDRESS);
    while(flash.busy()) {
        ;
    }
}

uint16_t download_body_crc16_checksum = 0;
uint32_t download_body_bytes_received = 0;
boolean download_past_header = false;
uint32_t download_content_length = 0;

void downloadHandleHeader(char * key, char * value) {
//  Serial.print("\"");
//  Serial.print(key);
//  Serial.print("\" => \"");
//  Serial.print(value);
//  Serial.println("\"");

    if(strcmp(key, "Content-Length") == 0) {
        download_content_length = strtoul(value, NULL, 10);
    }
}

uint32_t downloadProcessHeader(uint8_t * data, uint32_t data_length) {
    uint32_t start_index = 0;
    static uint8_t header_guard_index = 0;
    static boolean past_first_line = false;
    static char key[64] = {0};
    static char value[64] = {0};
    static uint8_t key_or_value = 0;
    static uint8_t keyval_index = 0;

    if(!download_past_header) {
        for(uint32_t ii = 0; ii < data_length; ii++) {
            switch(header_guard_index) {
            case 0:
                if(data[ii] == '\r') header_guard_index++;
                else if(data[ii] == ':') {
                    key_or_value = 1;
                    keyval_index = 0;
                }
                else if(past_first_line) {
                    if(keyval_index < 63) {
                        if(!((keyval_index == 0) && (data[ii] == ' '))) { // strip leading spaces
                            if(key_or_value == 0) key[keyval_index++] = data[ii];
                            else value[keyval_index++] = data[ii];
                        }
                    }
                    else {
                        // warning the key string doesn't fit in 64 characters
                    }
                }
                break;
            case 1:
                if(data[ii] == '\n') {
                    header_guard_index++;

                    if(past_first_line) {
                        downloadHandleHeader((char *) key, (char *) value);
                    }

                    past_first_line = true;
                    key_or_value = 0;
                    keyval_index = 0;
                    memset(key, 0, 64);
                    memset(value, 0, 64);
                }
                else header_guard_index = 0;
                break;
            case 2:
                if(data[ii] == '\r') header_guard_index++;
                else {
                    key[keyval_index++] = data[ii];
                    header_guard_index = 0;
                }
                break;
            case 3:
                if(data[ii] == '\n') header_guard_index++;
                else header_guard_index = 0;
                break;
            case 4:
                download_past_header = true;
                start_index = ii;
                header_guard_index = 0;
                break;
            }
        }
    }

    return start_index;
}

void downloadFile(char * hostname, uint16_t port, char * filename, void (*responseBodyProcessor)(uint8_t *, uint32_t)) {
    unsigned long total_bytes_read = 0;
    uint8_t mybuffer[64] = {0};

    // re-initialize the globals
    download_body_crc16_checksum = 0;
    download_body_bytes_received = 0;
    download_past_header = false;
    download_content_length = 0;

    /* Try connecting to the website.
       Note: HTTP/1.1 protocol is used to keep the server from closing the connection before all data is read.
    */
    esp.connect(hostname, port);
    if (esp.connected()) {
        memset(scratch, 0, SCRATCH_BUFFER_SIZE);
        snprintf(scratch, SCRATCH_BUFFER_SIZE-1, "GET /%s HTTP/1.1\r\nHost: %s\r\n\r\n\r\n", filename, hostname);
        esp.print(scratch);
        //Serial.print(scratch);
    } else {
        Serial.println(F("Error: Server Connection failed"));
        return;
    }

    Serial.println(F("Info: -------------------------------------"));

    /* Read data until either the connection is closed, or the idle timeout is reached. */
    unsigned long lastRead = millis();
    unsigned long num_bytes_read = 0;
    unsigned long start_time = millis();
    uint32_t loop_counter = 0;

    while (esp.connected(false) && (millis() - lastRead < IDLE_TIMEOUT_MS)) {
        while (esp.available()) {
            //char c = esp.read();
            num_bytes_read = esp.read(mybuffer, 64);
            total_bytes_read += num_bytes_read;

            if(loop_counter == 4096) {
                Serial.print("Info: ");
            }

            loop_counter++;
            if((loop_counter % 4096) == 0) {
                Serial.print(".");
                updateCornerDot();
                petWatchdog();
            }

            if(responseBodyProcessor != 0) {
                responseBodyProcessor(mybuffer, num_bytes_read); // signal end of stream
            }

            lastRead = millis();
        }
    }

    esp.stop();

    Serial.println();
    Serial.println("Info: Download Complete");
    Serial.print("Info: Total Bytes: ");
    Serial.println(total_bytes_read);
    Serial.print("Info: File Size: ");
    Serial.println(download_body_bytes_received);
    Serial.print("Info: Checksum: ");
    Serial.println(download_body_crc16_checksum);
    Serial.print("Info: Duration: ");
    Serial.println(millis() - start_time);

}

void processChkResponseData(uint8_t * data, uint32_t data_length) {
    uint32_t start_index = downloadProcessHeader(data, data_length);
    char * endPtr;
    static char buff[64] = {0};
    static uint8_t buff_idx = 0;

    if(download_past_header) {
        for(uint32_t ii = start_index; ii < data_length; ii++) {
            download_body_bytes_received++;
            download_body_crc16_checksum = _crc16_update(download_body_crc16_checksum, data[ii]);
            if(buff_idx < 63) {
                buff[buff_idx++] = data[ii];
            }
        }

        if(download_body_bytes_received == download_content_length) {
            integrity_num_bytes_total = strtoul(buff, &endPtr, 10);
            if(endPtr != 0) {
                integrity_crc16_checksum = strtoul(endPtr, 0, 10);
                downloaded_integrity_file = true;
            }
            Serial.println("Info: Integrity Checks: ");
            Serial.print(  "Info:    File Size: ");
            Serial.println(integrity_num_bytes_total);
            Serial.print(  "Info:    CRC16 Checksum: ");
            Serial.println(integrity_crc16_checksum);
        }

    }

}

void processHexResponseData(uint8_t * data, uint32_t data_length) {
    uint32_t start_index = downloadProcessHeader(data, data_length);
    static uint8_t page[256] = {0};
    static uint16_t page_idx = 0;
    static uint32_t page_address = 0;
    static uint32_t local_download_body_bytes_received = 0;

    if(download_past_header) {
        for(uint32_t ii = start_index; ii < data_length; ii++) {
            download_body_bytes_received++;
            download_body_crc16_checksum = _crc16_update(download_body_crc16_checksum, data[ii]);

            if(page_idx < 256) {
                page[page_idx++] = data[ii];
                if(page_idx >= 256) {
                    page_idx = 0;
                }
            }

            if((download_body_bytes_received == download_content_length) || (page_idx == 0)) {
                uint16_t top_bound = 256;
                if(page_idx != 0) {
                    top_bound = page_idx;
                }
                flash.writeBytes(page_address, page, top_bound);

                // clear the page
                memset(page, 0, 256);

                // advance the page address
                page_address += 256;
            }
        }

        if(download_body_bytes_received == download_content_length) {
            if((download_body_bytes_received == integrity_num_bytes_total) && (download_body_crc16_checksum == integrity_crc16_checksum)) {
                integrity_check_succeeded = true;
                Serial.println(F("Info: Integrity Check Succeeded!"));
            }
            else {
                Serial.println(F("Error: Integrity Check Failed!"));
                Serial.print(F("Error: Expected Checksum: "));
                Serial.print(integrity_crc16_checksum);
                Serial.print(F(", Actual Checksum: "));
                Serial.println(download_body_crc16_checksum);
                Serial.print(F("Error: Expected Filesize: "));
                Serial.print(integrity_num_bytes_total);
                Serial.print(F(", Actual Filesize: "));
                Serial.println(download_body_bytes_received);
            }
        }

    }

}

void checkForFirmwareUpdates() {
    static char filename[64] = {0};
    memset(filename, 0, 64);

    if(updateServerResolve()) {
        // try and download the integrity check file up to three times
        setLCD_P(PSTR("  CHECKING FOR  "
                      "    UPDATES     "));
        eeprom_read_block(filename, (const void *) EEPROM_UPDATE_FILENAME, 31);
        strncat_P(filename, PSTR(".chk"), 4);

        downloadFile(update_server_name, 80, filename, processChkResponseData);
        if(downloaded_integrity_file) {
            lcdSmiley(15, 1);
            SUCCESS_MESSAGE_DELAY();
            delayForWatchdog();
            petWatchdog();
        }

        if(downloaded_integrity_file &&
                (integrity_num_bytes_total > 0) && (integrity_crc16_checksum > 0) &&
                (integrity_num_bytes_total != ULONG_MAX) && (integrity_crc16_checksum != ULONG_MAX)) {
            // compare the just-retrieved signature file contents
            // to the signature already stored in flash
            if((flash_file_size != integrity_num_bytes_total) ||
                    (flash_signature != integrity_crc16_checksum)) {

                setLCD_P(PSTR("UPDATE AVAILABLE"
                              "  DOWNLOADING   "));
                SUCCESS_MESSAGE_DELAY();
                delayForWatchdog();
                petWatchdog();

                memset(filename, 0, 64); // switch to the hex extension
                eeprom_read_block(filename, (const void *) EEPROM_UPDATE_FILENAME, 31);
                strncat_P(filename, PSTR(".hex"), 4);

                setLCD_P(PSTR("     PLEASE     "
                              "      WAIT      "));

                Serial.print(F("Info: Downloading \""));
                Serial.print(filename);
                Serial.print(F("\""));
                Serial.println();

                //before starting the download of the hex file, first erase the flash memory up to the second to last page
                for(uint32_t page = 0; page < SECOND_TO_LAST_4K_PAGE_ADDRESS; page+=4096) {
                    while(flash.busy()) {
                        ;
                    }
                    flash.blockErase4K(page);
                    while(flash.busy()) {
                        ;
                    }
                }

                invalidateSignature(); // this makes sure a failed download doesn't induce an integrity check failure

                downloadFile(update_server_name, 80, filename, processHexResponseData);
                while(flash.busy()) {
                    ;
                }
                if(integrity_check_succeeded) {
                    // also write these parameters to their rightful place in the SPI flash
                    // for consumption by the bootloader
                    invalidateSignature();

                    flash.writeByte(CRC16_CHECKSUM_ADDRESS + 0, (integrity_crc16_checksum >> 8) & 0xff);
                    flash.writeByte(CRC16_CHECKSUM_ADDRESS + 1, (integrity_crc16_checksum >> 0) & 0xff);

                    flash.writeByte(FILESIZE_ADDRESS + 0, (integrity_num_bytes_total >> 24) & 0xff);
                    flash.writeByte(FILESIZE_ADDRESS + 1, (integrity_num_bytes_total >> 16) & 0xff);
                    flash.writeByte(FILESIZE_ADDRESS + 2, (integrity_num_bytes_total >> 8)  & 0xff);
                    flash.writeByte(FILESIZE_ADDRESS + 3, (integrity_num_bytes_total >> 0)  & 0xff);

                    flash.writeByte(MAGIC_NUMBER_ADDRESS + 0, MAGIC_NUMBER >> 24);
                    flash.writeByte(MAGIC_NUMBER_ADDRESS + 1, MAGIC_NUMBER >> 16);
                    flash.writeByte(MAGIC_NUMBER_ADDRESS + 2, MAGIC_NUMBER >> 8);
                    flash.writeByte(MAGIC_NUMBER_ADDRESS + 3, MAGIC_NUMBER >> 0);
                    Serial.println(F("Info: Wrote Magic Number"));

                    Serial.println(F("Info: Firmware Update Complete. Reseting to apply changes."));
                    setLCD_P(PSTR("APPLYING UPDATES"
                                  "WAIT ONE MINUTE "));
                    lcdSmiley(15, 1);
                    SUCCESS_MESSAGE_DELAY();
                    watchdogForceReset(false);
                }
                else {
                    Serial.println(F("Error: Firmware Update Failed. Try again later by resetting."));
                    setLCD_P(PSTR(" UPDATE FAILED  "
                                  "  RETRY LATER   "));
                    lcdFrownie(15, 1);
                    ERROR_MESSAGE_DELAY();
                }
            }
            else {
                Serial.println("Info: Signature matches, skipping HEX download.");
                setLCD_P(PSTR("SOFTWARE ALREADY"
                              "   UP TO DATE   "));
                SUCCESS_MESSAGE_DELAY();
                delayForWatchdog();
                petWatchdog();
            }
        }
        else {
            Serial.println("Error: Failed to download integrity check file, skipping Hex file download");
        }
    }
}

boolean updateServerResolve(void) {
    static boolean resolved = false;

    if(connectedToNetwork()) {
        if(!resolved) {
            eeprom_read_block(update_server_name, (const void *) EEPROM_UPDATE_SERVER_NAME, 31);

            if(strlen(update_server_name) == 0) {
                return false; // this is as indication that OTA updates are disabled
            }

            setLCD_P(PSTR("   RESOLVING"));
            updateLCD("UPDATE SERVER", 1);
            SUCCESS_MESSAGE_DELAY();

            if  (!esp.getHostByName(update_server_name, &update_server_ip32) || (update_server_ip32 == 0)) {
                Serial.print(F("Error: Couldn't resolve '"));
                Serial.print(update_server_name);
                Serial.println(F("'"));

                updateLCD("FAILED", 1);
                lcdFrownie(15, 1);
                ERROR_MESSAGE_DELAY();
                return false;
            }
            else {
                resolved = true;
                Serial.print(F("Info: Resolved \""));
                Serial.print(update_server_name);
                Serial.print(F("\" to IP address "));
                char ip_str[16] = {0};
                esp.IpUint32ToString(update_server_ip32, &(ip_str[0]));
                Serial.print((char *) ip_str);

                updateLCD(update_server_ip32, 1);
                lcdSmiley(15, 1);
                SUCCESS_MESSAGE_DELAY();
                Serial.println();
            }
        }

        // connected to network and resolution succeeded
        return true;
    }

    // not connected to network
    return false;
}

void getCurrentFirmwareSignature(void) {
    // retrieve the current signature parameters
    flash_file_size = flash.readByte(FILESIZE_ADDRESS);
    flash_file_size <<= 8;
    flash_file_size |= flash.readByte(FILESIZE_ADDRESS+1);
    flash_file_size <<= 8;
    flash_file_size |= flash.readByte(FILESIZE_ADDRESS+2);
    flash_file_size <<= 8;
    flash_file_size |= flash.readByte(FILESIZE_ADDRESS+3);

    flash_signature = flash.readByte(CRC16_CHECKSUM_ADDRESS);
    flash_signature <<= 8;
    flash_signature |= flash.readByte(CRC16_CHECKSUM_ADDRESS+1);

    Serial.print(F("Info: Current firmware signature: "));
    Serial.print(flash_file_size);
    Serial.print(F(" "));
    Serial.print(flash_signature);
    Serial.println();
}

/****** CONFIGURATION MIRRORING SUPPORT FUNCTIONS ******/
void commitConfigToMirroredConfig(void) {
    if(!mirrored_config_matches_eeprom_config()) {
        mirrored_config_copy_from_eeprom(); // create a valid mirrored config from the current settings
        if(!mirrored_config_integrity_check()) {
            Serial.println(F("Error: Mirrored configuration commit failed to validate."));
            //TODO: should something be written to the LCD here?
        }
    }
    else {
        Serial.println(F("Info: Mirrored configuration already matches current configuration."));
    }
}

boolean mirrored_config_matches_eeprom_config(void) {
    boolean ret = true;

    // compare each corresponding byte of the Flash into the EEPROM
    for (uint16_t ii = 0; ii < EEPROM_CONFIG_MEMORY_SIZE; ii++) {
        uint8_t flash_value = flash.readByte(((uint32_t) SECOND_TO_LAST_4K_PAGE_ADDRESS) + ((uint32_t) ii));
        uint8_t eeprom_value = eeprom_read_byte((uint8_t *) (EEPROM_CRC_CHECKSUM + ii));
        if(flash_value != eeprom_value) {
            ret = false;
            break;
        }
    }

    return ret;
}

boolean configMemoryUnlocked(uint16_t call_id) {
    if(!allowed_to_write_config_eeprom) {
        Serial.print(F("Error: Config Memory is not unlocked, called from line number "));
        Serial.println(call_id);
        return false;
    }

    return allowed_to_write_config_eeprom;
}

boolean mirrored_config_integrity_check() {
    boolean ret = false;
    uint16_t computed_crc = computeFlashChecksum();

    // interpret the CRC, little endian
    uint16_t stored_crc = getStoredFlashChecksum();

    if(stored_crc == computed_crc) {
        ret = true;
    }

    return ret;
}


void mirrored_config_restore(void) {
    if(!allowed_to_write_config_eeprom) {
        return;
    }

    // copy each byte from the Flash into the EEPROM
    for (uint16_t ii = 0; ii < EEPROM_CONFIG_MEMORY_SIZE; ii++) {
        uint8_t value = flash.readByte(((uint32_t) SECOND_TO_LAST_4K_PAGE_ADDRESS) + ((uint32_t) ii));
        eeprom_write_byte((uint8_t *) (EEPROM_CRC_CHECKSUM + ii), value);
    }
}

boolean mirrored_config_restore_and_validate(void) {
    boolean integrity_check_passed = false;

    if(mirrored_config_integrity_check()) {
        mirrored_config_restore();
        integrity_check_passed = checkConfigIntegrity();
        if(integrity_check_passed) {
            Serial.println(F("Info: Successfully restored to last valid configuration."));
        }
        else {
            Serial.println(F("Info: Restored last valid configuration, but it's still not valid. Possibly catastrophic."));
        }
    }
    else {
        Serial.println(F("Error: Mirrored configuration is not valid, cannot restore to last valid configuration."));
    }

    return integrity_check_passed;
}

void mirrored_config_copy_from_eeprom(void) {

    mirrored_config_erase();
    Serial.print(F("Info: Writing mirrored config..."));

    // copy each byte from the EEPROM into the Flash
    for (uint16_t ii = 0; ii < EEPROM_CONFIG_MEMORY_SIZE; ii++) {
        uint8_t value = eeprom_read_byte((uint8_t *) (EEPROM_CRC_CHECKSUM + ii));
        flash.writeByte(((uint32_t) SECOND_TO_LAST_4K_PAGE_ADDRESS) + ((uint32_t) ii), value);
    }
    Serial.println(F("OK."));
}

void mirrored_config_erase(void) {
    Serial.print(F("Info: Erasing mirrored config..."));
    flash.blockErase4K(SECOND_TO_LAST_4K_PAGE_ADDRESS);
    Serial.println(F("OK."));
}

/****** TIMESTAMPING SUPPORT FUNCTIONS ******/
time_t AQE_now(void) {
    selectSlot3();
    DateTime t = rtc.now();
    return (time_t) t.unixtime();
}

void currentTimestamp(char * dst, uint16_t max_len) {
    time_t n = now();

    snprintf(dst, max_len, "%02d/%02d/%04d %02d:%02d:%02d",
             month(n),
             day(n),
             year(n),
             hour(n),
             minute(n),
             second(n));
}

void printCurrentTimestamp(char * append_to, uint16_t * append_to_capacity_and_update) {
    char datetime[32] = {0};
    currentTimestamp(datetime, 31);
    Serial.print(datetime);

    appendToString(datetime, append_to, append_to_capacity_and_update);
}

void appendToString(char * str, char * append_to, uint16_t * append_to_capacity_and_update) {
    if(append_to != 0) {
        uint16_t len = strlen(str);
        if(*append_to_capacity_and_update >= len) {
            strcat(append_to, str);
            *append_to_capacity_and_update -= len;
        }
    }
}

void appendToString(float val, uint8_t digits_after_decimal_point, char * append_to, uint16_t * append_to_capacity_and_update) {
    char temp[32] = {0};
    safe_dtostrf(val, 0, digits_after_decimal_point, temp, 31);
    appendToString(temp, append_to, append_to_capacity_and_update);
}

void getNowFilename(char * dst, uint16_t max_len) {
    time_t n = now();
    snprintf(dst, max_len, "%02d%02d%02d%02d.csv",
             year(n) % 100,
             month(n),
             day(n),
             hour(n));
}

void rtcClearOscillatorStopFlag(void) {
    Wire.beginTransmission(DS3231_ADDRESS);
    Wire.write(DS3231_REG_CONTROL);
    Wire.endTransmission();

    // control registers
    Wire.requestFrom(DS3231_ADDRESS, 2);
    uint8_t creg = Wire.read();
    uint8_t sreg = Wire.read();

    sreg &= ~_BV(7); // clear bit 7 (msbit)

    Wire.beginTransmission(DS3231_ADDRESS);
    Wire.write((uint8_t) DS3231_REG_STATUS_CTL);
    Wire.write((uint8_t) sreg);
    Wire.endTransmission();
}

/****** GPS SUPPORT FUNCTIONS ******/
void updateGpsStrings(void) {
    const char * gps_lat_lng_field_mqtt_template = ",\"__location\":{\"lat\":%.6f,\"lon\":%.6f}";
    const char * gps_lat_lng_alt_field_mqtt_template  = ",\"__location\":{\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.2f}";
    const char * gps_lat_lng_field_csv_template = ",%.6f,%.6f,---";
    const char * gps_lat_lng_alt_field_csv_template  = ",%.6f,%.6f,%.2f";

    static boolean first = true;
    if(first) {
        first = false;
        float tmp = eeprom_read_float((float *) EEPROM_USER_LATITUDE_DEG);
        // Serial.println(tmp,6);
        if(!isnan(tmp) && (tmp >= -90.0f) && (tmp <= 90.0f)) {
            user_latitude = tmp;
        }

        tmp = eeprom_read_float((float *) EEPROM_USER_LONGITUDE_DEG);
        // Serial.println(tmp,6);
        if(!isnan(tmp) && (tmp >= -180.0f) && (tmp <= 180.0f)) {
            user_longitude = tmp;
        }

        int16_t l_tmp = (int16_t) eeprom_read_word((uint16_t *) EEPROM_ALTITUDE_METERS);
        // Serial.println(l_tmp);
        if(tmp != -1) {
            user_altitude = 1.0f * l_tmp;
        }

        // Serial.println(user_latitude, 6);
        // Serial.println(user_longitude, 6);
        // Serial.println(user_altitude, 2);
    }

    memset(gps_mqtt_string, 0, GPS_MQTT_STRING_LENGTH);
    memset(gps_csv_string, 0, GPS_CSV_STRING_LENGTH);

    if(user_location_override && (user_latitude != TinyGPS::GPS_INVALID_F_ANGLE) && (user_longitude != TinyGPS::GPS_INVALID_F_ANGLE)) {
        if(user_altitude != -1.0f) {
            snprintf(gps_mqtt_string, GPS_MQTT_STRING_LENGTH-1, gps_lat_lng_alt_field_mqtt_template, user_latitude, user_longitude, user_altitude);
            snprintf(gps_csv_string, GPS_CSV_STRING_LENGTH-1, gps_lat_lng_alt_field_csv_template, user_latitude, user_longitude, user_altitude);
        }
        else {
            snprintf(gps_mqtt_string, GPS_MQTT_STRING_LENGTH-1, gps_lat_lng_field_mqtt_template, user_latitude, user_longitude);
            snprintf(gps_csv_string, GPS_CSV_STRING_LENGTH-1, gps_lat_lng_field_csv_template, user_latitude, user_longitude);
        }
    }
    else if((gps_latitude != TinyGPS::GPS_INVALID_F_ANGLE) && (gps_longitude != TinyGPS::GPS_INVALID_F_ANGLE)) {
        if(gps_altitude != TinyGPS::GPS_INVALID_F_ALTITUDE) {
            snprintf(gps_mqtt_string, GPS_MQTT_STRING_LENGTH-1, gps_lat_lng_alt_field_mqtt_template, gps_latitude, gps_longitude, gps_altitude);
            snprintf(gps_csv_string, GPS_CSV_STRING_LENGTH-1, gps_lat_lng_alt_field_csv_template, gps_latitude, gps_longitude, gps_altitude);
        }
        else {
            snprintf(gps_mqtt_string, GPS_MQTT_STRING_LENGTH-1, gps_lat_lng_field_mqtt_template, gps_latitude, gps_longitude);
            snprintf(gps_csv_string, GPS_CSV_STRING_LENGTH-1, gps_lat_lng_field_csv_template, gps_latitude, gps_longitude);
        }
    }
    else {
        strcpy_P(gps_csv_string, PSTR(",---,---,---"));
    }
}

void suspendGpsProcessing(void) {
    if(gps_installed && !gps_disabled) {
        for(;;) {
            if(gpsSerial.available()) {
                char c = gpsSerial.read();
                if(gps.encode(c)) {
                    gps.f_get_position(&gps_latitude, &gps_longitude, &gps_age);
                    gps_altitude = gps.f_altitude();
                    updateGpsStrings();
                    break;
                }
                else if(c == '\n') {
                    break;
                }
            }
        }
    }
    gpsSerial.end();
    gps_disabled = true;
}

void resumeGpsProcessing(void) {
    gpsSerial.begin(9600);
    gps_disabled = false;
}

/****** NTP SUPPORT FUNCTIONS ******/
void getNetworkTime(void) {
    const unsigned long connectTimeout  = 15L * 1000L; // Max time to wait for server connection
    const unsigned long responseTimeout = 15L * 1000L; // Max time to wait for data from server
    char server[32] = {0};
    eeprom_read_block(server, (void *) EEPROM_NTP_SERVER_NAME, 31);
    uint8_t       buf[48];
    unsigned long ip, startTime, t = 0L;

    if(esp.getHostByName(server, &ip)) {
        static const char PROGMEM
        timeReqA[] = { 227,  0,  6, 236 },
                     timeReqB[] = {  49, 78, 49,  52 };

        Serial.print(F("Info: Getting NTP Time..."));
        startTime = millis();
        do {
            esp.connectUDP(ip, 123);
        } while((!esp.connected()) &&
                ((millis() - startTime) < connectTimeout));

        if(esp.connected()) {
            // Assemble and issue request packet
            memset(buf, 0, sizeof(buf));
            memcpy_P( buf, timeReqA, sizeof(timeReqA));
            memcpy_P(&buf[12], timeReqB, sizeof(timeReqB));
            esp.write(buf, sizeof(buf));
            memset(buf, 0, sizeof(buf));
            startTime = millis();
            while((esp.available() < 44) &&
                    ((millis() - startTime) < responseTimeout));

            if(esp.available() >= 44) {
                esp.read(buf, sizeof(buf));
                t = (((unsigned long)buf[40] << 24) |
                     ((unsigned long)buf[41] << 16) |
                     ((unsigned long)buf[42] <<  8) |
                     (unsigned long)buf[43]) - 2208988800UL;
            }
            esp.stop();
        }
    }

    if(t) {
        t += eeprom_read_float((float *) EEPROM_NTP_TZ_OFFSET_HRS) * 60UL * 60UL; // convert offset to seconds
        tmElements_t tm;
        breakTime(t, tm);
        setTime(t);

        selectSlot3();
        DateTime datetime(t);
        rtc.adjust(datetime);
        rtcClearOscillatorStopFlag();
        selectNoSlot();

        memset(buf, 0, 48);
        snprintf((char *) buf, 47,
                 "%d/%d/%d",
                 tm.Month,
                 tm.Day,
                 1970 + tm.Year);

        clearLCD();
        updateLCD((char *) buf, 0);

        Serial.print((char *) buf);
        Serial.print(" ");
        memset(buf, 0, 48);
        snprintf((char *) buf, 47,
                 "%02d:%02d:%02d",
                 tm.Hour,
                 tm.Minute,
                 tm.Second);

        updateLCD((char *) buf, 1);
        Serial.println((char *) buf);


        SUCCESS_MESSAGE_DELAY();

    }
    else {
        Serial.print(F("Failed."));
    }
}

/*
void dump_config(uint8_t * buf){
  uint16_t addr = 0;
  uint8_t ii = 0;
  while(addr < EEPROM_CONFIG_MEMORY_SIZE){
    if(ii == 0){
      Serial.print(addr, HEX);
      Serial.print(F(": "));
    }

    Serial.print(buf[addr], HEX);
    Serial.print(F("\t"));

    addr++;
    ii++;
    if(ii == 32){
      Serial.println();
      ii = 0;
    }
  }
}
*/




void floatToJsString(float f, char * target, uint8_t digits_after_decimal_point) {
    // only allow up 0 - 9 digits after decimal point
    if(digits_after_decimal_point > 9) {
        digits_after_decimal_point = 9;
    }

    if(isnan(f)) {
        strcpy(target, "null");
    }
    else {
        char format_string[] = "%.0f";                  // initialize digits after decimal point to 0
        format_string[2] += digits_after_decimal_point; // update the digits after decimal point
        sprintf(target, format_string, f);
    }
}

boolean parseConfigurationMessageBody(char * body) {
    jsmn_init(&parser);

    boolean found_ssid = false;
    boolean found_pwd = false;
    boolean handled_ssid_pwd = false;
    boolean found_exit = false;

    int16_t r = jsmn_parse(&parser, body, strlen(body), json_tokens, sizeof(json_tokens)/sizeof(json_tokens[0]));
    if(r > 0) {
        Serial.print(F("Info: Found "));
        Serial.print(r);
        Serial.println(F(" JSON tokens"));
    }
    else {
        Serial.print(F("Info: JSON parse failed for body \""));
        Serial.print(body);
        Serial.print(F("\" response code "));
        Serial.println(r);
    }
    char key[33] = {0};
    char value[33] = {0};
    char ssid[33] = {0};
    char pwd[33] = {0};

    for(uint8_t ii = 1; ii < r; ii+=2) {
        memset(key, 0, 33);
        memset(value, 0, 33);
        uint16_t keylen = json_tokens[ii].end - json_tokens[ii].start;
        uint16_t valuelen = json_tokens[ii+1].end - json_tokens[ii+1].start;

        if(keylen <= 32) {
            strncpy(key, body + json_tokens[ii].start, keylen);
        }

        if(valuelen <= 32) {
            strncpy(value, body + json_tokens[ii+1].start, valuelen);
        }

        Serial.print(F("Info: JSON token: \""));
        Serial.print(key);
        Serial.print("\" => \"");
        Serial.print(value);
        Serial.println("\"");

        // handlers for valid JSON keys
        if(strcmp(key, "ssid") == 0) {
            found_ssid = true;
            strcpy(ssid, value);
        }
        else if(strcmp(key, "pwd") == 0) {
            found_pwd = true;
            strcpy(pwd, value);
        }
        else if((strcmp(key, "exit") == 0) && (strcmp(value, "true") == 0)) {
            found_exit = true;
        }
        else if(strcmp(key, "use_gps") == 0) {
            if(strcmp(value, "true") == 0) {
                set_user_location_enable("disable");
            }
            else if(strcmp(value, "false") == 0) {
                set_user_location_enable("enable");
            }
        }
        else if(strcmp(key, "lat") == 0) {
            set_user_latitude(value);
        }
        else if(strcmp(key, "lng") == 0) {
            set_user_longitude(value);
        }
        else if(strcmp(key, "alt") == 0) {
            altitude_command(value);
        }
        else if(strcmp(key, "temp_unit") == 0) {
            set_temperature_units(value);
        }
        else if(strcmp(key, "opmode") == 0) {
            set_operational_mode(value);
        }
        else {
            Serial.print(F("Warn: posted key \""));
            Serial.print(key);
            Serial.print(F("\" is unrecognized and will be ignored."));
            Serial.println();
        }

        if(!handled_ssid_pwd && found_ssid && found_pwd) {
            Serial.print(F("Info: Trying to connect to target network"));
            wifi_connect_attempts++;

            set_ssid(ssid);
            set_network_password(pwd);
            set_network_security_mode("auto");

            if(esp.connectToNetwork((char *) ssid, (char *) pwd, 30000)) {
                Serial.print(F("Info: Successfully connected to Network \""));
                Serial.print(ssid);
                Serial.print(F("\""));
                Serial.println();
                wifi_can_connect = true;
            }
            else {
                Serial.print(F("Info: Unable to connect to Network \""));
                Serial.print(ssid);
                Serial.print(F("\""));
                Serial.println();
                wifi_can_connect = false;
            }
            handled_ssid_pwd = true;
        }
    }

    // commit changes, because it's annoying to lose settings because of an Egg reset
    if(!mirrored_config_matches_eeprom_config()) {
        if(checkConfigIntegrity()) {
            commitConfigToMirroredConfig();
        }
    }

    return found_exit;
}

void checkForESPFirmwareUpdates() {
    // the firmware version should be at least 1.5.0.0 = 01 50 00 00
    uint32_t version_int = 0;
    if(esp.getVersion(&version_int)) {
        Serial.print(F("Info: Current ESP8266 Firmware Version is "));
        Serial.println(version_int);
        if(version_int >= 1050000UL) {
            Serial.println(F("Info: ESP8266 Firmware Version is up to date"));
        }
        else {
            Serial.println(F("Info: ESP8266 Firmware Update required..."));
            doESP8266Update();
        }
    }
    else {
        Serial.println(F("Error: Failed to get ESP8266 firmware version"));
    }
}

void doESP8266Update() {
    boolean timeout = false;
    boolean gotOK = false;

    int32_t timeout_interval =  300000; // 5 minutes

    setLCD_P(PSTR("   PERFORMING   "
                  "   ESP UPDATES  "));

    Serial.print(F("Info: Starting ESP8266 Firmware Update..."));
    if(esp.firmwareUpdateBegin()) {
        // Serial.println(F("OK"));
        uint8_t status = 0xff;
        uint32_t previousMillis = millis();
        while(esp.firmwareUpdateStatus(&status) && !timeout) {
            // Serial.print(F("Info: Pet watchdog @ "));
            // Serial.println(millis());
            petWatchdog();
            delayForWatchdog();

            if(status == 2) {
                gotOK = true;
                // Serial.println(F("ESP8266 returned OK."));
            }
            else if(status == 3) {
                if(gotOK) {
                    Serial.println(F("Done."));
                    break; // break out of the loop and perform a reboot
                }
                else {
                    Serial.println(F("Unexpected Error."));
                    Serial.println(F("Warning: ESP8266 Reset occurred before receiving OK."));
                    return;
                }
            }

            if(status != 0xff) {
                previousMillis =  current_millis; // reset timeout on state change
                // Serial.print("ESP8266 Status Changed to ");
                // Serial.println(status);
                status = 0xff;
            }

            current_millis = millis();
            if(current_millis - previousMillis >= timeout_interval) {
                timeout = true;
            }
        }
        if(timeout) {
            Serial.println(F("Timeout Error."));
            return;
        }
        else if(status == 1) {
            Serial.println(F("Unknown Error."));
            return;
        }
    }
    else {
        Serial.println(F("Failed."));
        return;
    }

    Serial.print(F("Info: ESP8266 restoring defaults..."));
    if(esp.restoreDefault()) {
        Serial.println(F("OK."));
    }
    else {
        Serial.println(F("Failed."));
    }

    Serial.flush();
    watchdogForceReset();
}

void processTouchBetweenGpsMessages(char c) {
    // actually all we care about is not interfering in a GPGGA transaction
    // so what we should do is wait for $GPGG is a sufficient prefix, then
    // run collectTouch after the following \n
    static uint8_t idx = 0;
    switch(idx) {
    case 0:
        if(c == '$') {
            idx++;
        }
        break;
    case 1:
        if(c == 'G') {
            idx++;
        }
        else {
            idx = 0;
        }
        break;
    case 2:
        if(c == 'P') {
            idx++;
        }
        else {
            idx = 0;
        }
        break;
    case 3:
    case 4:
        if(c == 'G') {
            idx++;
        }
        else {
            idx = 0;
        }
        break;
    default:
        if(c == '\n') {
            suspendGpsProcessing();
            collectTouch();
            resumeGpsProcessing();
            idx = 0;
        }
        break;
    }

}

void verifyProgmemWithSpiFlash() {
    const boolean print_all_bytes = false;

    static boolean first = true;
    if(!first) {
        return;
    }
    first = false;

    setLCD_P(PSTR("    FIRMWARE    "
                  "INTEGRITY CHECK "));

    uint32_t bytes_read = 0;
    if(flash_file_size == 0 || flash_file_size > SECOND_TO_LAST_4K_PAGE_ADDRESS) {
        return;
    }

    Serial.println("Info: Checking Firmware Integrity...1");

    unsigned long currentMillis = millis();
    unsigned long previousMillis = currentMillis;
    const long interval = 1000;
    unsigned long start = currentMillis;

    enum {
        WAITING_FOR_COLON,
        CONSUMING_LL,
        CONSUMING_AAAA,
        CONSUMING_TT,
        CONSUMING_DATA
    } hex_parse_state = WAITING_FOR_COLON;

    char tmp[2] = {0};
    uint8_t section_byte_counter = 0;
    uint16_t section_byte_multiplier = 1;
    uint8_t line_length = 0;
    uint32_t extended_address = 0;
    uint8_t line_offset_address = 0;
    uint16_t line_base_address = 0;
    uint8_t line_record_type = 0;
    uint8_t data_byte = 0;
    uint8_t counter = 2;

    while(bytes_read < flash_file_size) {
        currentMillis = millis();
        if (currentMillis - previousMillis >= interval) {
            previousMillis = currentMillis;
            tinywdt.pet();
            updateCornerDot();
            Serial.print("Info: Checking Firmware Integrity...");
            Serial.println(counter++);
        }

        flash.readBytes(bytes_read, (uint8_t *) scratch, 256);

        for(uint16_t jj = 0; jj < 256; jj++) {
            uint8_t b = scratch[jj];
            tmp[0] = b;

            if(hex_parse_state != WAITING_FOR_COLON) {
                if(print_all_bytes) Serial.print((char) b);
            }

            switch(hex_parse_state) {
            case WAITING_FOR_COLON:
                if(b == ':') {
                    hex_parse_state = CONSUMING_LL;
                    line_length = 0;
                    section_byte_counter = 0;
                    section_byte_multiplier = 16;
                    line_base_address = 0;
                    line_offset_address = 0;
                    data_byte = 0;
                    line_record_type = 0;
                }
                break;
            case CONSUMING_LL:
                line_length += section_byte_multiplier * strtoul(tmp, NULL, 16);
                section_byte_multiplier /= 16;
                section_byte_counter++;
                if(section_byte_counter == 2) {
                    section_byte_multiplier = 16*16*16;
                    section_byte_counter = 0;
                    hex_parse_state = CONSUMING_AAAA;
                }

                break;
            case CONSUMING_AAAA:
                line_base_address += section_byte_multiplier * strtoul(tmp, NULL, 16);
                section_byte_multiplier /= 16;
                section_byte_counter++;
                if(section_byte_counter == 4) {
                    section_byte_multiplier = 16;
                    section_byte_counter = 0;
                    hex_parse_state = CONSUMING_TT;
                }

                break;
            case CONSUMING_TT:
                line_record_type += section_byte_multiplier * strtoul(tmp, NULL, 16);
                section_byte_multiplier /= 16;
                section_byte_counter++;
                if(section_byte_counter == 2) {
                    data_byte = 0;
                    if(line_record_type == 0x02) {
                        extended_address = 0;
                        section_byte_multiplier = 16*16*16; // expect an extended address
                    } else {
                        section_byte_multiplier = 16;
                    }

                    section_byte_counter = 0;
                    hex_parse_state = CONSUMING_DATA;
                }
                break;
            case CONSUMING_DATA:
                if(line_record_type == 0x02) {
                    extended_address += section_byte_multiplier * strtoul(tmp, NULL, 16);
                } else {
                    data_byte += section_byte_multiplier * strtoul(tmp, NULL, 16);
                }
                section_byte_multiplier /= 16;
                section_byte_counter++;

                if(section_byte_counter == 2) {

                    if(line_record_type == 0) {
                        uint32_t far_prog_address = (extended_address << 4) + line_base_address + line_offset_address;
                        uint16_t near_prog_address = far_prog_address & 0xFFFF;

                        // @ progmem[line_base_addres + line_offset_address]
                        uint8_t program_byte = far_prog_address == near_prog_address ?
                                               pgm_read_byte_near(near_prog_address) : pgm_read_byte_far(far_prog_address);

                        if(program_byte != data_byte) {
                            Serial.print("Warning: Firmware Corruption Detected @");
                            Serial.print(far_prog_address, HEX);
                            Serial.print(" -- EXPECTED=");
                            Serial.print("0x");
                            if(data_byte < 16) Serial.print("0");
                            Serial.print(data_byte, HEX);
                            Serial.print(" -- ACTUAL=");
                            Serial.print("0x");
                            if(program_byte < 16) Serial.print("0");
                            Serial.print(program_byte, HEX);
                            Serial.println();

                            //for diagnostic purposes only
                            setLCD_P(PSTR("CORRUPT FIRMWARE"
                                          "    DETECTED    "));
                            lcdFrownie(15, 1);
                            while(1) {
                                // currentMillis = millis();
                                // if (currentMillis - previousMillis >= interval) {
                                //     tinywdt.pet();
                                //     previousMillis = currentMillis;
                                // }
                            }
                            //end for diagnostic purposes only

                            return;
                        }
                    }

                    line_length--;
                    line_offset_address++;

                    section_byte_counter = 0;
                    section_byte_multiplier = 16;
                    data_byte = 0;
                }

                if(line_length == 0) {
                    hex_parse_state = WAITING_FOR_COLON;
                    if(print_all_bytes) Serial.println();
                }
                break;
            default:
                hex_parse_state = WAITING_FOR_COLON;
                break;
            }

            bytes_read++;
            if(bytes_read == flash_file_size) {
                break;
            }
        }
    }
    Serial.print("Info: Firmware Integrity Check Completed in ");
    Serial.print((millis() - start) / 1000.0, 1);
    Serial.println(" seconds");

}
