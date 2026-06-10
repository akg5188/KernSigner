SCAD = esp32_p4_4_3_camera_case.scad

.PHONY: all case nfc preview nfc-preview scad-nfc scad-nfc-preview reference clean

all: case

case:
	openscad -o screen_protective_case.stl -D 'part="case"' $(SCAD)

nfc:
	@echo "screen_protective_case_nfc.stl is the verified FreeCAD-exported NFC print file."
	@echo "Edit screen_protective_case_nfc.FCStd and export from FreeCAD to update it."

preview:
	openscad -o case_preview.png -D 'part="preview"' --preview --autocenter --viewall --projection=o --imgsize=1600,900 --colorscheme=Tomorrow\ Night $(SCAD)

nfc-preview:
	@echo "The verified NFC case is FreeCAD-based; render screen_protective_case_nfc.FCStd from FreeCAD."

scad-nfc:
	openscad -o screen_protective_case_nfc_scad_unverified.stl -D 'part="case"' -D 'nfc_bump_enabled=true' $(SCAD)

scad-nfc-preview:
	openscad -o nfc_case_preview_scad_unverified.png -D 'part="preview"' -D 'nfc_bump_enabled=true' --preview --autocenter --viewall --projection=o --imgsize=1600,900 --colorscheme=Tomorrow\ Night $(SCAD)

reference:
	openscad -o screen_protective_case_reference.stl -D 'part="reference"' $(SCAD)

clean:
	rm -f screen_protective_case.stl screen_protective_case_reference.stl
	rm -f screen_protective_case_nfc_scad_unverified.stl
	rm -f case_preview.png nfc_case_preview_scad_unverified.png
	rm -f camera_test.stl
	rm -f rear_cover.stl rear_cover_reference.stl front_shell.stl back_lid.stl print_layout.stl
