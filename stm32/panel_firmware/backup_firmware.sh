openocd -f yardforce500.cfg -c "init" -c "reset halt"   -c " dump_image panel_controller_backup.bin 0x08000000 0x10000" -c "reset" -c shutdown
