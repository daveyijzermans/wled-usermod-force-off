#include "wled.h"

/*
 * Force Off usermod
 * -----------------
 * Continuously re-sends the "off" (black) frame to addressable LED strips while
 * WLED is in its off state, so LEDs that glitch on due to electrical noise are
 * immediately driven back off.
 *
 * Why this is needed:
 *   By default WLED stops servicing the strip a short while after the light is
 *   turned off. The main loop only updates the strip when:
 *
 *     if (!offMode || strip.isOffRefreshRequired() || strip.needsUpdate())
 *       strip.service();
 *
 *   After ~600ms of being off, handleOnOff() (button.cpp) sets `offMode = true`
 *   and calls BusManager::off(). For ordinary clocked addressable buses (WS2812,
 *   SK6812, WS2811, ...) isOffRefreshRequired() is false, so no data is sent at
 *   all while off and the data line sits idle. On long wiring runs or in noisy
 *   environments, EMI can latch a spurious value into the first LED(s), turning
 *   them on -- and because WLED is no longer transmitting, nothing corrects them.
 *
 * What this does:
 *   While the global brightness is 0 (light off) and the usermod is enabled, it
 *   keeps WLED out of the power-save `offMode`:
 *     - re-enables the bus (BusManager::on()) if WLED had already parked it, and
 *     - keeps `lastOnTime` fresh so the 600ms off-timeout never fires.
 *   As a result the main loop keeps calling strip.service() -> show() pushes the
 *   (black) off frame every frame, at the configured LED refresh rate.
 *
 * Trade-offs:
 *   The strip (and a configured power relay) is kept actively driven while off,
 *   so this disables ESP8266 modem-sleep while off and keeps the relay on. That
 *   is required to keep the data line driven. Disable the usermod to restore the
 *   stock power-saving behaviour.
 */

class ForceOffUsermod : public Usermod {

  private:

    bool enabled  = true;   // active by default once compiled in
    bool initDone = false;

    // strings used multiple times (saves some flash memory)
    static const char _name[];
    static const char _enabled[];

  public:

    inline void enable(bool enable) { enabled = enable; }
    inline bool isEnabled() { return enabled; }

    void setup() override {
      initDone = true;
    }

    void loop() override {
      // Only act while the light is OFF. When on, leave everything to stock WLED.
      if (!enabled || bri != 0) return;

      // If WLED already parked the strip in power-save mode, bring the bus back so
      // the data line is driven again. BusManager::on() mirrors what handleOnOff()
      // does when turning on (re-inits the bus pin where needed, drives the relay,
      // etc.). Only called on the transition, not every loop.
      if (offMode) {
        BusManager::on();
        offMode = false;
      }

      // Keep WLED from re-entering offMode. handleOnOff() only powers the strip
      // down once `millis() - lastOnTime > 600` (and no pending update). Keeping
      // lastOnTime fresh stops that timeout from firing, so the main loop keeps
      // calling strip.service() -> show() pushes the (black) off frame every frame.
      lastOnTime = millis();
    }

    void addToConfig(JsonObject& root) override {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      top[FPSTR(_enabled)] = enabled;
    }

    bool readFromConfig(JsonObject& root) override {
      JsonObject top = root[FPSTR(_name)];
      bool configComplete = !top.isNull();
      configComplete &= getJsonValue(top[FPSTR(_enabled)], enabled, true);
      return configComplete;
    }

    void appendConfigData(Print& settingsScript) override {
      settingsScript.print(F("addInfo('")); settingsScript.print(FPSTR(_name));
      settingsScript.print(F(":enabled',1,'continuously push the off state to the strip while the light is off');"));
    }

    // No unique ID needed; the base class returns USERMOD_ID_UNSPECIFIED.
};

const char ForceOffUsermod::_name[]    PROGMEM = "Force Off";
const char ForceOffUsermod::_enabled[] PROGMEM = "enabled";

static ForceOffUsermod force_off_usermod;
REGISTER_USERMOD(force_off_usermod);
