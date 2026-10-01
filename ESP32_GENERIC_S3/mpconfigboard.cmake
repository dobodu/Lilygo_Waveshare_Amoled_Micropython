set(IDF_TARGET esp32s3)

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base            #Base config
    boards/sdkconfig.ble             #BLE
    boards/sdkconfig.240mhz          #240Mhz clock
    boards/sdkconfig.spiram_oct      #Octal Spiram
    boards/sdkconfig.spiram_esp32    #Spiram tweaks
    boards/sdkconfig.flash_16MiB     #16MB flash
    boards/sdkconfig.flash_qio_80m   #Flash speed
)
