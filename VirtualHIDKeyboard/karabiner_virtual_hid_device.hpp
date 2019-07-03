// Taken and modified from Karabiner-Elements
#include <IOKit/hid/IOHIDUsageTables.h>
#include "AppleHIDUsageTables.h"

class karabiner_virtual_hid_device final {
public:
    class hid_report final {
    public:
        class __attribute__((packed)) keys final {
        public:
            keys(void) : keys_{} {}

            const uint8_t(&get_raw_value(void) const)[32] {
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

        class __attribute__((packed)) consumer_input final {
        public:
            consumer_input(void) : report_id_(1) {}
            bool operator==(const consumer_input& other) const { return (memcmp(this, &other, sizeof(*this)) == 0); }
            bool operator!=(const consumer_input& other) const { return !(*this == other); }

        private:
            uint8_t report_id_ __attribute__((unused));

        public:
            keys keys;
        };

        class __attribute__((packed)) apple_vendor_top_case_input final {
        public:
            apple_vendor_top_case_input(void) : report_id_(2) {}
            bool operator==(const apple_vendor_top_case_input& other) const { return (memcmp(this, &other, sizeof(*this)) == 0); }
            bool operator!=(const apple_vendor_top_case_input& other) const { return !(*this == other); }

        private:
            uint8_t report_id_ __attribute__((unused));

        public:
            keys keys;
        };
    };
};
