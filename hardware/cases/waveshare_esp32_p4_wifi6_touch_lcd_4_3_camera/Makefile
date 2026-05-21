SCAD = esp32_p4_4_3_camera_case.scad

.PHONY: all case preview reference clean

all: case

case:
	openscad -o screen_protective_case.stl -D 'part="case"' $(SCAD)

preview:
	openscad -o case_preview.png -D 'part="preview"' --preview --autocenter --viewall --projection=o --imgsize=1600,900 --colorscheme=Tomorrow\ Night $(SCAD)

reference:
	openscad -o screen_protective_case_reference.stl -D 'part="reference"' $(SCAD)

clean:
	rm -f screen_protective_case.stl screen_protective_case_reference.stl
	rm -f case_preview.png
	rm -f camera_test.stl
	rm -f rear_cover.stl rear_cover_reference.stl front_shell.stl back_lid.stl print_layout.stl
