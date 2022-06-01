openocd -f yardforce500_melk.cfg  -c " program "get_fw_backup/mb_firmware_melk.bin" 0x08000000 verify reset; shutdown;"
