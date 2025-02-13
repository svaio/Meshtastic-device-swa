#pragma once

#include "GPSStatus.h"
#include "Observer.h"
#include "concurrency/OSThread.h"

struct uBloxGnssModelInfo {
    char swVersion[30];
    char hwVersion[10];
    uint8_t extensionNo;
    char extension[10][30];
};

typedef enum {
    GNSS_MODEL_MTK,
    GNSS_MODEL_UBLOX,
    GNSS_MODEL_UC6850,
    GNSS_MODEL_UNKNOWN,
} GnssModel_t;

typedef enum {
    GNSS_RESPONSE_NONE,
    GNSS_RESPONSE_NAK,
    GNSS_RESPONSE_FRAME_ERRORS,
    GNSS_RESPONSE_OK,
} GPS_RESPONSE;

// Generate a string representation of DOP
const char *getDOPString(uint32_t dop);

/**
 * A gps class that only reads from the GPS periodically (and FIXME - eventually keeps the gps powered down except when reading)
 *
 * When new data is available it will notify observers.
 */
class GPS : private concurrency::OSThread
{
  private:
    uint32_t lastWakeStartMsec = 0, lastSleepStartMsec = 0, lastWhileActiveMsec = 0;
    const int serialSpeeds[6] = {9600, 4800, 38400, 57600, 115200, 9600};

    int speedSelect = 0;
    int probeTries = 2;

    /**
     * hasValidLocation - indicates that the position variables contain a complete
     *   GPS location, valid and fresh (< gps_update_interval + gps_attempt_time)
     */
    bool hasValidLocation = false; // default to false, until we complete our first read

    bool isAwake = false; // true if we want a location right now

    bool wakeAllowed = true; // false if gps must be forced to sleep regardless of what time it is

    bool shouldPublish = false; // If we've changed GPS state, this will force a publish the next loop()

    bool hasGPS = false; // Do we have a GPS we are talking to

    bool GPSInitFinished = false; // Init thread finished?
    bool GPSInitStarted = false;  // Init thread finished?

    uint8_t numSatellites = 0;

    CallbackObserver<GPS, void *> notifySleepObserver = CallbackObserver<GPS, void *>(this, &GPS::prepareSleep);
    CallbackObserver<GPS, void *> notifyDeepSleepObserver = CallbackObserver<GPS, void *>(this, &GPS::prepareDeepSleep);
    CallbackObserver<GPS, void *> notifyGPSSleepObserver = CallbackObserver<GPS, void *>(this, &GPS::prepareDeepSleep);

  public:
    /** If !NULL we will use this serial port to construct our GPS */
    static HardwareSerial *_serial_gps;

    static const uint8_t _message_PMREQ[];
    static const uint8_t _message_CFG_RXM_PSM[];
    static const uint8_t _message_CFG_RXM_ECO[];
    static const uint8_t _message_CFG_PM2[];
    static const uint8_t _message_GNSS_7[];
    static const uint8_t _message_GNSS[];
    static const uint8_t _message_JAM[];
    static const uint8_t _message_NAVX5[];
    static const uint8_t _message_1HZ[];
    static const uint8_t _message_GGL[];
    static const uint8_t _message_GSA[];
    static const uint8_t _message_GSV[];
    static const uint8_t _message_VTG[];
    static const uint8_t _message_RMC[];
    static const uint8_t _message_GGA[];
    static const uint8_t _message_PMS[];
    static const uint8_t _message_SAVE[];

    meshtastic_Position p = meshtastic_Position_init_default;

    GPS() : concurrency::OSThread("GPS") {}

    virtual ~GPS();

    /** We will notify this observable anytime GPS state has changed meaningfully */
    Observable<const meshtastic::GPSStatus *> newStatus;

    /**
     * Returns true if we succeeded
     */
    virtual bool setup();

    /// Returns true if we have acquired GPS lock.
    virtual bool hasLock();

    /// Returns true if there's valid data flow with the chip.
    virtual bool hasFlow();

    /// Return true if we are connected to a GPS
    bool isConnected() const { return hasGPS; }

    bool isPowerSaving() const { return !config.position.gps_enabled; }

    /**
     * Restart our lock attempt - try to get and broadcast a GPS reading ASAP
     * called after the CPU wakes from light-sleep state
     *
     * Or set to false, to disallow any sort of waking
     * */
    void forceWake(bool on);

    // Some GPS modules (ublock) require factory reset
    virtual bool factoryReset() { return true; }

    // Empty the input buffer as quickly as possible
    void clearBuffer();

    // Create a ublox packet for editing in memory
    uint8_t makeUBXPacket(uint8_t class_id, uint8_t msg_id, uint8_t payload_size, const uint8_t *msg);

    // scratch space for creating ublox packets
    uint8_t UBXscratch[250] = {0};

    int getACK(uint8_t *buffer, uint16_t size, uint8_t requestedClass, uint8_t requestedID, uint32_t waitMillis);
    GPS_RESPONSE getACK(uint8_t c, uint8_t i, uint32_t waitMillis);
    GPS_RESPONSE getACK(const char *message, uint32_t waitMillis);

  protected:
    /// If possible force the GPS into sleep/low power mode
    virtual void sleep();

    /// wake the GPS into normal operation mode
    virtual void wake();

    /** Subclasses should look for serial rx characters here and feed it to their GPS parser
     *
     * Return true if we received a valid message from the GPS
     */
    virtual bool whileIdle() = 0;

    /** Idle processing while GPS is looking for lock, called once per secondish */
    virtual void whileActive() {}

    /**
     * Perform any processing that should be done only while the GPS is awake and looking for a fix.
     * Override this method to check for new locations
     *
     * @return true if we've acquired a time
     */
    virtual bool lookForTime() = 0;

    /**
     * Perform any processing that should be done only while the GPS is awake and looking for a fix.
     * Override this method to check for new locations
     *
     * @return true if we've acquired a new location
     */
    virtual bool lookForLocation() = 0;

    /// Record that we have a GPS
    void setConnected();

    void setNumSatellites(uint8_t n);

  private:
    /// Prepare the GPS for the cpu entering deep or light sleep, expect to be gone for at least 100s of msecs
    /// always returns 0 to indicate okay to sleep
    int prepareSleep(void *unused);

    /// Prepare the GPS for the cpu entering deep sleep, expect to be gone for at least 100s of msecs
    /// always returns 0 to indicate okay to sleep
    int prepareDeepSleep(void *unused);

    // Calculate checksum
    void UBXChecksum(uint8_t *message, size_t length);

    /**
     * Switch the GPS into a mode where we are actively looking for a lock, or alternatively switch GPS into a low power mode
     *
     * calls sleep/wake
     */
    void setAwake(bool on);

    /** Get how long we should stay looking for each aquisition
     */
    uint32_t getWakeTime() const;

    /** Get how long we should sleep between aqusition attempts
     */
    uint32_t getSleepTime() const;

    /**
     * Tell users we have new GPS readings
     */
    void publishUpdate();

    virtual int32_t runOnce() override;

    // Get GNSS model
    String getNMEA();
    GnssModel_t probe(int serialSpeed);

    // delay counter to allow more sats before fixed position stops GPS thread
    uint8_t fixeddelayCtr = 0;

  protected:
    GnssModel_t gnssModel = GNSS_MODEL_UNKNOWN;
};

// Creates an instance of the GPS class.
// Returns the new instance or null if the GPS is not present.
GPS *createGps();

extern GPS *gps;