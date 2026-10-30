************************************************************************************************
AmbiqSuite 4.5.0 Known Defects and Patches

The included directories can be dragged and dropped in the base directory /Ambiqsuite/ to replace only the affected files.
The change list and updated files are provided below. In some instances, the same file may be affected by multiple patches. In such cases only the latest, superset version will be provided. 

************************************************************************************************
--------------------------------------------------------------------------------------------------------
General Known Defects and Updates

* KD #1 New BLE Example - ble_freertos_power_cycle
- Description: This example shows the proper procedure for shutting down and powering off BLE.
- File/Update: Added example for the following boards. Please add to the SDK work area and rebuild the example.
		apollo4b_blue_evb
		apollo4l_blue_evb
		apollo4p_blue_kbr_evb
		apollo4p_blue_kxr_evb                    

* KD #2 Updated ADC Example - adc_measure
- Description: This example shows the proper procedure for using the ADC to measure incoming voltage samples.
- File/Update: Added example for the following boards. In the peripheral example section of each board, replace the existing adc_measure folder and rebuild the example. This will work for _blue_ boards as well.
		apollo4b_evb
		apollo4p_evb
		apollo4l_evb
	     	   
* KD #3 Remove all instances of unused BASEADDR macros from Apollo4 HALs.
- Description: This update removes the unused BASEADDR macros from the HALs for Apollo4, Apollo4 Plus, and Apollo4 Lite.
- File/Update: Replace the relevant files in the HAL for the SoC of interest.
		mcu/apollo4x/regs/am_reg_itm.h
		mcu/apollo4x/regs/am_reg_jedec.h
		mcu/apollo4x/regs/am_reg_sysctrl.h
		mcu/apollo4x/regs/am_reg_tpiu.h

* KD #4 Add SBL Update OTA image for the current factory SBL for Apollo4 Plus SoCs.
- Description: This update adds SBL_ap4p_v0p2 to the tools/apollo4b_scripts directory. To be used for testing purposes. Applicable to Apollo4 Plus only.
- File/Update: /tools/apollo4b_scripts/sbl_updates_apollo4p/sbl_ap4p_v0p2/

* KD #5 Updated apollo4b_scripts to use cross-platform cryptography library.
- Description: This update allows users to generate OEM certs and signatures in both Linux and Windows environments. This update also removed the dependency on local keys and passwords.
- File/Update: /tools/apollo4b_scripts/

--------------------------------------------------------------------------------------------------------
Cordio BLE Stack Updates

This portion of the document summarizes known security vulnerabilities affecting the Cordio BLE stack 
and describes their remediation status within AmbiqSuite SDK4.5.0.
The vulnerabilities listed below are associated with publicly disclosed CVEs from 2024. 
All identified issues have been reviewed and addressed through manual code changes, 
configuration hardening, and/or defensive runtime checks as applicable.
--------------------------------------------------------------------------------------------------------

* #1  CVE-2024-48981
- Description: Publicly reported vulnerability affecting Cordio BLE stack behavior under specific malformed or unexpected BLE inputs.
- Status: Already fix the vulnerabilities on April 22, 2021 prior to the issue got reported
- File/Update: 	N/A

* #2  CVE-2024-48982
- Description: Vulnerability related to improper handling of BLE protocol state or data structures.
- Status: Fix has been implemented by adding minimum length validation for Command Complete/Status events
- File/Update: 	third_party\cordio\ble-host\sources\hci\ambiq\hci_evt.c

* #3  CVE-2024-48983
- Description: Reported issue involving potential instability or denial-of-service scenarios.
- Status: Fix has been implemented by adding integer overflow checks in WSF message allocation. 
		Based on mbed-ce/mbed-os PR #388, add overflow protection to prevent integer wrap-around 
		when calculating buffer sizes.
- File/Update:	third_party\cordio\ble-host\sources\hci\ambiq\hci_tr.c
				third_party\cordio\wsf\sources\port\freertos\wsf_msg.c

* #4  CVE-2024-48984
- Description: Vulnerability impacting robustness of BLE event or packet handling.
- Status: Fix has been implemented by adding bounds checking in hciEvtProcessLeExtAdvReport. 
		Based on mbed-ce/mbed-os PR #387, add buffer bounds validation to prevent reading 
		beyond the event data when parsing extended advertising reports.
- File/Update: 	third_party\cordio\ble-host\sources\hci\ambiq\hci_evt.c

* #5  CVE-2024-48985
- Description: Issue related to memory handling or unexpected protocol interaction.
- Status: Already fix the vulnerabilities on April 22, 2021 prior to the issue got reported
- File/Update: 	N/A

* #6  CVE-2024-48986
- Description: Reported weakness allowing malformed input to affect BLE stack operation.
- Status: Fix has been implemented through handle variable length vendor specific events 
		Based on mbed-ce/mbed-os PR #385, add extra allocation for variable length 
		HCI events to prevent buffer overflow.
- File/Update: 	third_party\cordio\ble-host\sources\hci\ambiq\hci_evt.c
				third_party\cordio\ble-host\sources\stack\dm\dm_main.c
				third_party\cordio\ble-profiles\sources\apps\fit\fit_main.c
