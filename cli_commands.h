#ifndef CLI_COMMANDS_H
#define CLI_COMMANDS_H

#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"

extern DeviceConfig currentConfig;

void cli_begin();
void cli_update();
void cli_print_status();
void cli_print_help();

void config_load();
void config_save();
void config_reset_defaults();

#endif // CLI_COMMANDS_H
