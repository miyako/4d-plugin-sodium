#ifndef SODIUM_4DPLUGIN_H
#define SODIUM_4DPLUGIN_H

#include "4DPluginAPI.h"

static void Argon2_Generate_password_hash(PA_PluginParameters params);
static void Argon2_Verify_password_hash(PA_PluginParameters params);
static void Argon2_Hash_needs_rehash(PA_PluginParameters params);

#endif
