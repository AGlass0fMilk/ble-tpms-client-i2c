/* mbed Microcontroller Library
 * Copyright (c) 2006-2018 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <events/mbed_events.h>
#include "platform/Callback.h"
#include "platform/mbed_wait_api.h"
#include "ble/BLE.h"
#include "gap/Gap.h"
#include "gap/AdvertisingDataParser.h"
#include "pretty_printer.h"

#define LIMIT_SIZE(x, y) ((x > y)? y : x)

#define TPMS_MAX_NAME_LENGTH 16
#define TPMS_MAX_MFG_DATA_LENGTH 20

events::EventQueue event_queue;

typedef struct {
    ble::scan_interval_t interval;
    ble::scan_window_t   window;
    ble::scan_duration_t duration;
    bool active;
} DemoScanParam_t;

/** the entries in this array are used to configure our scanning
 *  parameters for each of the modes we use in our demo */
static const DemoScanParam_t scanning_params[] = {
/*                      interval                  window                   duration  active */
/*                      0.625ms                  0.625ms                       10ms         */
    {   ble::scan_interval_t(200),   ble::scan_window_t(100),   ble::scan_duration_t(0), true },
    { ble::scan_interval_t(160), ble::scan_window_t(100), ble::scan_duration_t(300), false },
    { ble::scan_interval_t(160),  ble::scan_window_t(40),   ble::scan_duration_t(0), true  },
    { ble::scan_interval_t(500),  ble::scan_window_t(10),   ble::scan_duration_t(0), false }
};


/** Demonstrate advertising, scanning and connecting
 */
class GapDemo : private mbed::NonCopyable<GapDemo>, public ble::Gap::EventHandler
{
public:
    GapDemo(BLE& ble, events::EventQueue& event_queue) :
        _ble(ble),
        _gap(ble.gap()),
        _event_queue(event_queue),
        _is_in_scanning_mode(true),
        _on_duration_end_id(0) {
    }

    ~GapDemo()
    {
        if (_ble.hasInitialized()) {
            _ble.shutdown();
        }
    }

    /** Start BLE interface initialisation */
    void run()
    {
        if (_ble.hasInitialized()) {
            printf("ble: BLE instance already initialised.\r\n");
            return;
        }

        /* handle gap events */
        _gap.setEventHandler(this);

        ble_error_t error = _ble.init(this, &GapDemo::on_init_complete);
        if (error) {
            printf("ble: error returned by BLE::init\r\n");
            return;
        }

        /* this will not return until shutdown */
        _event_queue.dispatch_forever();
    }

private:
    /** This is called when BLE interface is initialised and starts the first mode */
    void on_init_complete(BLE::InitializationCompleteCallbackContext *event)
    {
        if (event->error) {
            printf("ble: error during the initialisation");
            return;
        }

        /* all calls are serialised on the user thread through the event queue */
        _event_queue.call(this, &GapDemo::scan);
    }

    /** Set up and start scanning */
    void scan()
    {
        const DemoScanParam_t &scan_params = scanning_params[0];

        /*
         * Scanning happens repeatedly and is defined by:
         *  - The scan interval which is the time (in 0.625us) between each scan cycle.
         *  - The scan window which is the scanning time (in 0.625us) during a cycle.
         * If the scanning process is active, the local device sends scan requests
         * to discovered peer to get additional data.
         */
        ble_error_t error = _gap.setScanParameters(
            ble::ScanParameters(
                ble::phy_t::LE_1M,   // scan on the 1M PHY
                scan_params.interval,
                scan_params.window,
                scan_params.active
            )
        );
        if (error) {
            printf("ble: error caused by Gap::setScanParameters");
            return;
        }

        /* start scanning and attach a callback that will handle advertisements
         * and scan requests responses */
        error = _gap.startScan(scan_params.duration);
        if (error) {
            printf("ble: error caused by Gap::startScan");
            return;
        }

        printf("Scanning started (interval: %dms, window: %dms, timeout: %dms).\r\n",
               scan_params.interval.valueInMs(), scan_params.window.valueInMs(), scan_params.duration.valueInMs());
    }

private:
    /* Gap::EventHandler */

    /** Look at scan payload to find a peer device and connect to it */
    virtual void onAdvertisingReport(const ble::AdvertisingReportEvent &event)
    {

        ble::AdvertisingDataParser adv_parser(event.getPayload());

        bool is_tpms_beacon = false;
        uint8_t mfg_data[TPMS_MAX_MFG_DATA_LENGTH];
        memset(mfg_data, 0, TPMS_MAX_MFG_DATA_LENGTH);

        /* parse the advertising payload, looking for a discoverable device */
        while (adv_parser.hasNext()) {
            ble::AdvertisingDataParser::element_t field = adv_parser.next();

            if(field.type == ble::adv_data_type_t::COMPLETE_LOCAL_NAME) {

            		// Copy over the name and null-terminate it
            		char name[TPMS_MAX_NAME_LENGTH];
            		uint8_t size = LIMIT_SIZE(field.value.size(), (TPMS_MAX_NAME_LENGTH-1));
            		memcpy(name, field.value.data(), size);
            		name[size] = '\0';

            		// Check to see if the name contains "TPMS"
            		if(strstr(name, "TPMS")) {
            			printf("ble-tpms - found tpms beacon: %s\r\n", name);
            			printf("\tpeer addr: ");
            			print_address((Gap::Address_t&)event.getPeerAddress());
            			printf("\tpeer addr type: %i\r\n", event.getPeerAddressType().value());
            			target_addr = event.getPeerAddress();
            			is_tpms_beacon = true;
            		}
            }

            /** Copy the manufacturer data */
            if(field.type == ble::adv_data_type_t::MANUFACTURER_SPECIFIC_DATA) {
            		if(event.getPeerAddress() == target_addr) {
            			uint32_t *tire_pressure, *tire_temperature;
					tire_pressure = (uint32_t*)&field.value[8];
					tire_temperature = (uint32_t*)&field.value[12];
					printf("ble-tpms - tire pressure: %i, tire temp: %i\r\n", *tire_pressure, *tire_temperature);
            		}
            }
        }
    }

    virtual void onScanTimeout(const ble::ScanTimeoutEvent&)
    {
        printf("ble-tpms: stopped scanning early due to timeout parameter\r\n");
    }

private:

    /** Finish the mode by shutting down advertising or scanning and move to the next mode. */
    void end_scanning_mode()
    {
        ble_error_t error = _gap.stopScan();
    }

private:
    BLE                &_ble;
    ble::Gap           &_gap;
    events::EventQueue &_event_queue;

    /* Keep track of our progress through demo modes */
    bool                _is_in_scanning_mode;

    /* Remember the call id of the function on _event_queue
     * so we can cancel it if we need to end the mode early */
    int                 _on_duration_end_id;

    ble::address_t target_addr;

};

/** Schedule processing of events from the BLE middleware in the event queue. */
void schedule_ble_events(BLE::OnEventsToProcessCallbackContext *context) {
    event_queue.call(mbed::Callback<void()>(&context->ble, &BLE::processEvents));
}

int main()
{
    BLE &ble = BLE::Instance();

    /* this will inform us off all events so we can schedule their handling
     * using our event queue */
    ble.onEventsToProcess(schedule_ble_events);

    GapDemo demo(ble, event_queue);
    demo.run();

    while (1) {
    };

    return 0;
}
