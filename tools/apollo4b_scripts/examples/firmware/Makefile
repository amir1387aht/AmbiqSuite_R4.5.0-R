INPUT:= $(wildcard *.bin)
OUTPUT:= $(INPUT:.bin=.ota)

all: $(OUTPUT)

%.ota: %.bin firmware.ini keys.ini
	python3 ../../create_cust_image_blob.py -c firmware.ini --bin $< -o $@

clean:
	rm -rf a $(OUTPUT)
