OUTPUT:= hello_world.wota

all: $(OUTPUT)

%.ota: %.bin firmware.ini keys.ini
	python3 ../../create_cust_image_blob.py -c firmware.ini --bin $< -o $@

%.wota: %.ota
	python3 ../../create_cust_image_blob.py -c wired_ota.ini --bin $< -o $@

clean:
	rm -rf a $(OUTPUT)
