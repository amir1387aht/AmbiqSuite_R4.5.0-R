INPUT:= $(wildcard *.bin)
OUTPUT = $(INPUT:.bin=_ota.bin)
CERT = $(shell grep '^cert-pkg =' am_oem_cnt_cert.cfg | cut -d' ' -f3)

INPUT_DATA_PATH = ../../oem_tools_pkg/cert_utils/cert_gen_utils/inputData
CERT_UTIL_PATH = ../../oem_tools_pkg/cert_utils/cert_gen_utils

all: $(OUTPUT) $(CERT)

# Update secure-firmware.ini with the correct certificate name
.PHONY: update-config
update-config:
	sed "s/certificate = .*/certificate = $(CERT)/" secure-firmware.ini > secure-firmware.ini.tmp && mv secure-firmware.ini.tmp secure-firmware.ini

secure-firmware.ini: am_oem_cnt_cert.cfg update-config

$(CERT): $(INPUT) am_oem_cnt_cert.cfg images_table_template.tbl
	sed "s/INPUT_FILE/\.\/inputData\/$(firstword $(INPUT))/" images_table_template.tbl > images_table.tbl
	cp images_table.tbl $(INPUT_DATA_PATH)
	cp $(INPUT) $(INPUT_DATA_PATH)
	cd $(CERT_UTIL_PATH) && python3 am_cert_content_util.py ../../../examples/secure-firmware/am_oem_cnt_cert.cfg
	cp $(CERT_UTIL_PATH)/am_cert_content_util_output/$(CERT) $@

%_ota.bin: %.bin $(CERT) secure-firmware.ini keys.ini
	python3 ../../create_cust_image_blob.py -c secure-firmware.ini --bin $< --certificate $(CERT) -o $@

clean:
	rm -rf $(OUTPUT) $(CERT) images_table.tbl *.bin *.log
