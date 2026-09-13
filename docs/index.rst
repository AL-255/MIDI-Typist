MIDI-Typist
===========

An optical keyboard that switches between NKRO typing and expressive USB MIDI.
Start with the user manual; developers can build the Huntsman firmware or port
the shared application to another board. These pages describe the latest build.

.. toctree::
   :maxdepth: 1
   :caption: Start here

   Overview <README>
   User manual <USER_MANUAL>
   Reading guide <docs/README>
   Build and test <docs/BUILDING>
   Validation and limitations <docs/VALIDATION>

.. toctree::
   :maxdepth: 1
   :caption: Operation and host tools

   Fn menu <docs/FN_MENU>
   Configuration GUI <docs/KEYBOARD_GUI>
   Calibration <docs/CALIBRATION>
   Whole-scan display <docs/SCAN_STREAM>
   Per-key capture <docs/LAST_KEY_STREAM>
   Private flash backups <docs/FLASH_DUMP>

.. toctree::
   :maxdepth: 1
   :caption: Design and protocols

   Architecture <docs/ARCHITECTURE>
   Device storage <docs/DEVICE_CONFIG_STORAGE>
   Telemetry reference <docs/TELEMETRY>
   MIDI and configuration protocol <docs/MIDI_PROTOCOL>
   MIDI engine <docs/MIDI_DESIGN>
   Root and scale filters <docs/MIDI_SCALES>
   Per-key velocity <docs/KEY_VELOCITY>
   Interval filter <docs/MIDI_FILTER>
   Velocity normalization <docs/NORMALIZED_VELOCITY>
   Lighting <docs/TRAVEL_LIGHTING>
   Optical scan and stock behavior <docs/KEYBOARD_RECOVERY>
   USB integration <docs/USB_DESIGN>
   Scheduling <docs/SCHEDULING>

.. toctree::
   :maxdepth: 1
   :caption: Development

   Porting guide <docs/PORTING>
   Documentation and Pages CI <docs/DOCUMENTATION>
   Contributor rules <AGENTS>
