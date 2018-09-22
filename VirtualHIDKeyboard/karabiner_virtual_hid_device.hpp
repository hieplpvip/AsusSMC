// Taken and modified from Karabiner-Elements
#include <IOKit/hid/IOHIDUsageTables.h>

class karabiner_virtual_hid_device final {
public:
    class hid_report final {
    public:
        enum class modifier : uint8_t {
            left_control = 0x1 << 0,
            left_shift = 0x1 << 1,
            left_option = 0x1 << 2,
            left_command = 0x1 << 3,
            right_control = 0x1 << 4,
            right_shift = 0x1 << 5,
            right_option = 0x1 << 6,
            right_command = 0x1 << 7,
        };

        class __attribute__((packed)) modifiers final {
        public:
            modifiers(void) : modifiers_(0) {}

            uint8_t get_raw_value(void) const {
                return modifiers_;
            }

            bool empty(void) const {
                return modifiers_ == 0;
            }

            void clear(void) {
                modifiers_ = 0;
            }

            void insert(modifier value) {
                modifiers_ |= static_cast<uint8_t>(value);
            }

            void erase(modifier value) {
                modifiers_ &= ~(static_cast<uint8_t>(value));
            }

            bool exists(modifier value) const {
                return modifiers_ & static_cast<uint8_t>(value);
            }

            bool operator==(const modifiers& other) const { return (memcmp(this, &other, sizeof(*this)) == 0); }
            bool operator!=(const modifiers& other) const { return !(*this == other); }

        private:
            uint8_t modifiers_;
        };

        class __attribute__((packed)) keys final {
        public:
            keys(void) : keys_{} {}

            const uint8_t (&get_raw_value(void) const)[32] {
                return keys_;
            }

            bool empty(void) const {
                for (const auto& k : keys_) {
                    if (k != 0) {
                        return false;
                    }
                }
                return true;
            }

            void clear(void) {
                memset(keys_, 0, sizeof(keys_));
            }

            void insert(uint8_t key) {
                if (!exists(key)) {
                    for (auto&& k : keys_) {
                        if (k == 0) {
                            k = key;
                            return;
                        }
                    }
                }
            }

            void erase(uint8_t key) {
                for (auto&& k : keys_) {
                    if (k == key) {
                        k = 0;
                    }
                }
            }

            bool exists(uint8_t key) const {
                for (const auto& k : keys_) {
                    if (k == key) {
                        return true;
                    }
                }
                return false;
            }

            size_t count(void) const {
                size_t result = 0;
                for (const auto& k : keys_) {
                    if (k) {
                        ++result;
                    }
                }
                return result;
            }

            bool operator==(const keys& other) const { return (memcmp(this, &other, sizeof(*this)) == 0); }
            bool operator!=(const keys& other) const { return !(*this == other); }

        private:
            uint8_t keys_[32];
        };

        class __attribute__((packed)) buttons final {
        public:
            buttons(void) : buttons_(0) {}

            uint32_t get_raw_value(void) const {
                return buttons_;
            }

            bool empty(void) const {
                return buttons_ == 0;
            }

            void clear(void) {
                buttons_ = 0;
            }

            void insert(uint8_t button) {
                if (1 <= button && button <= 32) {
                    buttons_ |= (0x1 << (button - 1));
                }
            }

            void erase(uint8_t button) {
                if (1 <= button && button <= 32) {
                    buttons_ &= ~(0x1 << (button - 1));
                }
            }

            bool exists(uint8_t button) const {
                if (1 <= button && button <= 32) {
                    return buttons_ & (0x1 << (button - 1));
                }
                return false;
            }

            bool operator==(const buttons& other) const { return (memcmp(this, &other, sizeof(*this)) == 0); }
            bool operator!=(const buttons& other) const { return !(*this == other); }

        private:
            uint32_t buttons_; // 32 bits for each button (32 buttons)

            // (0x1 << 0) -> button 1
            // (0x1 << 1) -> button 2
            // (0x1 << 2) -> button 3
            // ...
            // (0x1 << 0) -> button 9
            // ...
            // (0x1 << 0) -> button 17
            // ...
            // (0x1 << 0) -> button 25
        };

        class __attribute__((packed)) keyboard_input final {
        public:
            keyboard_input(void) : report_id_(1), reserved(0) {}
            bool operator==(const keyboard_input& other) const { return (memcmp(this, &other, sizeof(*this)) == 0); }
            bool operator!=(const keyboard_input& other) const { return !(*this == other); }

        private:
            uint8_t report_id_ __attribute__((unused));

        public:
            modifiers modifiers;

        private:
            uint8_t reserved __attribute__((unused));

        public:
            keys keys;
        };

        class __attribute__((packed)) consumer_input final {
        public:
            consumer_input(void) : report_id_(2) {}
            bool operator==(const consumer_input& other) const { return (memcmp(this, &other, sizeof(*this)) == 0); }
            bool operator!=(const consumer_input& other) const { return !(*this == other); }

        private:
            uint8_t report_id_ __attribute__((unused));

        public:
            keys keys;
        };

        class __attribute__((packed)) apple_vendor_top_case_input final {
        public:
            apple_vendor_top_case_input(void) : report_id_(3) {}
            bool operator==(const apple_vendor_top_case_input& other) const { return (memcmp(this, &other, sizeof(*this)) == 0); }
            bool operator!=(const apple_vendor_top_case_input& other) const { return !(*this == other); }

        private:
            uint8_t report_id_ __attribute__((unused));

        public:
            keys keys;
        };

        class __attribute__((packed)) apple_vendor_keyboard_input final {
        public:
            apple_vendor_keyboard_input(void) : report_id_(4) {}
            bool operator==(const apple_vendor_keyboard_input& other) const { return (memcmp(this, &other, sizeof(*this)) == 0); }
            bool operator!=(const apple_vendor_keyboard_input& other) const { return !(*this == other); }

        private:
            uint8_t report_id_ __attribute__((unused));

        public:
            keys keys;
        };

        class __attribute__((packed)) pointing_input final {
        public:
            pointing_input(void) : buttons{}, x(0), y(0), vertical_wheel(0), horizontal_wheel(0) {}
            bool operator==(const pointing_input& other) const { return (memcmp(this, &other, sizeof(*this)) == 0); }
            bool operator!=(const pointing_input& other) const { return !(*this == other); }

            buttons buttons;
            uint8_t x;
            uint8_t y;
            uint8_t vertical_wheel;
            uint8_t horizontal_wheel;
        };
    };

    class properties final {
    public:
        class __attribute__((packed)) keyboard_initialization final {
        public:
            keyboard_initialization(void) : country_code(0) {}

            bool operator==(const keyboard_initialization& other) const {
                return country_code == other.country_code;
            }
            bool operator!=(const keyboard_initialization& other) const { return !(*this == other); }

            uint8_t country_code;
        };
    };
};

/* AppleVendor Keyboard Page (0xff01) */
enum {
    kHIDUsage_AppleVendorKeyboard_Spotlight = 0x0001,
    kHIDUsage_AppleVendorKeyboard_Dashboard = 0x0002,
    kHIDUsage_AppleVendorKeyboard_Function = 0x0003,
    kHIDUsage_AppleVendorKeyboard_Launchpad = 0x0004,
    kHIDUsage_AppleVendorKeyboard_Reserved = 0x000a,
    kHIDUsage_AppleVendorKeyboard_CapsLockDelayEnable = 0x000b,
    kHIDUsage_AppleVendorKeyboard_PowerState = 0x000c,
    kHIDUsage_AppleVendorKeyboard_Expose_All = 0x0010,
    kHIDUsage_AppleVendorKeyboard_Expose_Desktop = 0x0011,
    kHIDUsage_AppleVendorKeyboard_Brightness_Up = 0x0020,
    kHIDUsage_AppleVendorKeyboard_Brightness_Down = 0x0021,
    kHIDUsage_AppleVendorKeyboard_Language = 0x0030
};

/* AppleVendor Page Top Case (0x00ff) */
enum {
    kHIDUsage_AV_TopCase_KeyboardFn = 0x0003,
    kHIDUsage_AV_TopCase_BrightnessUp = 0x0004,
    kHIDUsage_AV_TopCase_BrightnessDown = 0x0005,
    kHIDUsage_AV_TopCase_VideoMirror = 0x0006,
    kHIDUsage_AV_TopCase_IlluminationToggle = 0x0007,
    kHIDUsage_AV_TopCase_IlluminationUp = 0x0008,
    kHIDUsage_AV_TopCase_IlluminationDown = 0x0009,
    kHIDUsage_AV_TopCase_ClamshellLatched = 0x000a,
    kHIDUsage_AV_TopCase_Reserved_MouseData = 0x00c0
};
