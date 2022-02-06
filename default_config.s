.section .rodata
.global fanpico_default_config_start
fanpico_default_config_start:
.incbin "default_config.json"
.byte 0x00
.global fanpico_default_config_end
fanpico_default_config_end:

