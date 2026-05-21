SCAD = esp32_p4_4_3_camera_case.scad

.PHONY: all case reference clean

all: case

case:
	openscad -o screen_protective_case.stl -D 'part="case"' $(SCAD)

reference:
	openscad -o screen_protective_case_reference.stl -D 'part="reference"' $(SCAD)

clean:
	rm -f screen_protective_case.stl screen_protective_case_reference.stl
	rm -f camera_test.stl
	rm -f rear_cover.stl rear_cover_reference.stl front_shell.stl back_lid.stl print_layout.stl
