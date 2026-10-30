INPUT = $(wildcard *.bin)
OUTPUT = $(INPUT:.bin=.wire)

all: $(OUTPUT)

%.wire: %.bin wired.ini keys.ini
	python3 ../../create_cust_image_blob.py -c wired.ini --bin $< -o $@

clean:
	rm -rf $(OUTPUT)
