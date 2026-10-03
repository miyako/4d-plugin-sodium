#include "sodium-4dplugin.h"

#include <sodium.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#define PLUGIN_EXPORT
#else
#define PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

// 4D command numbers of built-in commands invoked through PA_ExecuteCommandByID
#define CMD_THROW    1805
#define CMD_OB_KEYS  1719

// Error codes raised with throw()
enum {
    ERR_INIT           = 1,
    ERR_PASSWORD_SIZE  = 2,
    ERR_BAD_TEXT       = 3,
    ERR_BAD_OPTIONS    = 4,
    ERR_OPTION_UNKNOWN = 5,
    ERR_OPTION_TYPE    = 6,
    ERR_OPTION_RANGE   = 7,
    ERR_HASH_FAILED    = 8
};

static const size_t MAX_PASSWORD_BYTES = 1024;
static const uint64_t DEFAULT_MEMORY_KIB = 19456;
static const uint64_t DEFAULT_ITERATIONS = 2;

static std::atomic<int> g_sodium_ready(0);

static bool ensure_sodium() {
    if (g_sodium_ready.load() == 1) return true;
    // sodium_init() is thread-safe and idempotent
    if (sodium_init() < 0) return false;
    g_sodium_ready.store(1);
    return true;
}

PLUGIN_EXPORT
void PluginMain(PA_long32 selector, PA_PluginParameters params) {
    switch (selector) {
        case kInitPlugin:
            ensure_sodium();
            break;
        case 1:
            Argon2_Generate_password_hash(params);
            break;
        case 2:
            Argon2_Verify_password_hash(params);
            break;
        case 3:
            Argon2_Hash_needs_rehash(params);
            break;
    }
}

#pragma mark - helpers

static void set_object_string(PA_ObjectRef obj, const char *key, const char *value) {
    PA_Unichar k[32], v[256];
    size_t i = 0;
    for (; key[i] && i < 31; ++i) k[i] = (PA_Unichar)(unsigned char)key[i];
    k[i] = 0;
    for (i = 0; value[i] && i < 255; ++i) v[i] = (PA_Unichar)(unsigned char)value[i];
    v[i] = 0;
    PA_Unistring ukey = PA_CreateUnistring(k);
    PA_Unistring uval = PA_CreateUnistring(v);
    PA_Variable var = PA_CreateVariable(eVK_Unistring);
    PA_SetStringVariable(&var, &uval);
    PA_SetObjectProperty(obj, &ukey, var);
}

static void set_object_real(PA_ObjectRef obj, const char *key, double value) {
    PA_Unichar k[32];
    size_t i = 0;
    for (; key[i] && i < 31; ++i) k[i] = (PA_Unichar)(unsigned char)key[i];
    k[i] = 0;
    PA_Unistring ukey = PA_CreateUnistring(k);
    PA_Variable var = PA_CreateVariable(eVK_Real);
    PA_SetRealVariable(&var, value);
    PA_SetObjectProperty(obj, &ukey, var);
}

// Raises a 4D error through the built-in throw command (errors are exposed via Last errors).
static void throw_error(PA_long32 code, const char *message) {
    PA_ObjectRef err = PA_CreateObject();
    set_object_string(err, "componentSignature", "sodm");
    set_object_real(err, "errCode", (double)code);
    set_object_string(err, "message", message);

    PA_Variable args[1];
    memset(args, 0, sizeof(args));
    PA_SetObjectVariable(&args[0], err);
    PA_ExecuteCommandByID(CMD_THROW, args, 1);
}

static void return_boolean(PA_PluginParameters params, bool value) {
    PA_Variable result = PA_CreateVariable(eVK_Boolean);
    PA_SetBooleanVariable(&result, value ? 1 : 0);
    PA_ReturnVariable(params, &result);
}

// Owns a UTF-8 buffer and wipes it on destruction
struct SecretBuffer {
    std::vector<char> data;
    size_t length;
    SecretBuffer() : length(0) {}
    ~SecretBuffer() {
        if (!data.empty()) sodium_memzero(data.data(), data.size());
    }
};

enum Utf8Result { UTF8_OK, UTF8_TOO_LONG, UTF8_INVALID };

// Explicit UTF-16 -> UTF-8. The buffer is sized once so no unwiped copies are left behind.
static Utf8Result utf16_to_utf8(const PA_Unichar *s, PA_long32 len, size_t maxBytes, SecretBuffer &out) {
    out.data.assign(maxBytes + 5, 0);
    size_t n = 0;
    for (PA_long32 i = 0; i < len; ++i) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (i + 1 >= len || s[i + 1] < 0xDC00 || s[i + 1] > 0xDFFF) return UTF8_INVALID;
            c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            ++i;
        } else if (c >= 0xDC00 && c <= 0xDFFF) {
            return UTF8_INVALID;
        }
        char *d = out.data.data() + n;
        if (c < 0x80) {
            d[0] = (char)c; n += 1;
        } else if (c < 0x800) {
            d[0] = (char)(0xC0 | (c >> 6));
            d[1] = (char)(0x80 | (c & 0x3F)); n += 2;
        } else if (c < 0x10000) {
            d[0] = (char)(0xE0 | (c >> 12));
            d[1] = (char)(0x80 | ((c >> 6) & 0x3F));
            d[2] = (char)(0x80 | (c & 0x3F)); n += 3;
        } else {
            d[0] = (char)(0xF0 | (c >> 18));
            d[1] = (char)(0x80 | ((c >> 12) & 0x3F));
            d[2] = (char)(0x80 | ((c >> 6) & 0x3F));
            d[3] = (char)(0x80 | (c & 0x3F)); n += 4;
        }
        if (n > maxBytes) return UTF8_TOO_LONG;
    }
    out.length = n;
    return UTF8_OK;
}

// Reads the password parameter. Returns false (after throwing) on failure.
static bool read_password(PA_PluginParameters params, short index, SecretBuffer &out) {
    PA_Unistring *text = PA_GetStringParameter(params, index);
    if (!text) {
        out.data.assign(1, 0);
        out.length = 0;
        return true;
    }
    switch (utf16_to_utf8(PA_GetUnistring(text), PA_GetUnistringLength(text), MAX_PASSWORD_BYTES, out)) {
        case UTF8_OK:
            return true;
        case UTF8_TOO_LONG:
            throw_error(ERR_PASSWORD_SIZE, "The password exceeds the maximum length of 1024 bytes (UTF-8).");
            return false;
        default:
            throw_error(ERR_BAD_TEXT, "The password is not valid Unicode text.");
            return false;
    }
}

static bool utf16_equals_ascii(const PA_Unistring *u, const char *ascii) {
    size_t n = strlen(ascii);
    if ((size_t)PA_GetUnistringLength((PA_Unistring *)u) != n) return false;
    const PA_Unichar *p = PA_GetUnistring((PA_Unistring *)u);
    for (size_t i = 0; i < n; ++i) {
        if (p[i] != (PA_Unichar)(unsigned char)ascii[i]) return false;
    }
    return true;
}

struct Argon2Options {
    uint64_t memoryKiB;
    uint64_t iterations;
    Argon2Options() : memoryKiB(DEFAULT_MEMORY_KIB), iterations(DEFAULT_ITERATIONS) {}
};

// Reads a positive integer option. Returns false (after throwing) on failure.
static bool read_uint_option(PA_ObjectRef obj, PA_Unistring *key, const char *name, uint64_t &out) {
    PA_Variable value = PA_GetObjectProperty(obj, key);
    PA_VariableKind kind = PA_GetVariableKind(value);
    double d = 0;
    bool numeric = true;
    switch (kind) {
        case eVK_Real:    d = PA_GetRealVariable(value); break;
        case eVK_Longint: d = (double)PA_GetLongintVariable(value); break;
        case eVK_Integer: d = (double)PA_GetLongintVariable(value); break;
        default: numeric = false; break;
    }
    PA_ClearVariable(&value);

    std::string msg = std::string("The option \"") + name;
    if (!numeric) {
        throw_error(ERR_OPTION_TYPE, (msg + "\" must be a number.").c_str());
        return false;
    }
    if (!(d >= 1) || d != (double)(uint64_t)d || d > 9007199254740992.0) {
        throw_error(ERR_OPTION_RANGE, (msg + "\" must be a positive integer.").c_str());
        return false;
    }
    out = (uint64_t)d;
    return true;
}

// Validates against libsodium limits and converts memory to bytes
static bool validate_options(const Argon2Options &o, size_t &memlimit, unsigned long long &opslimit) {
    unsigned long long maxOps = (unsigned long long)crypto_pwhash_OPSLIMIT_MAX;
    if (o.iterations < (uint64_t)crypto_pwhash_OPSLIMIT_MIN || o.iterations > maxOps) {
        throw_error(ERR_OPTION_RANGE, "The option \"iterations\" is out of range.");
        return false;
    }
    uint64_t minKiB = ((uint64_t)crypto_pwhash_MEMLIMIT_MIN + 1023) / 1024;
    uint64_t maxKiB = (uint64_t)crypto_pwhash_MEMLIMIT_MAX / 1024;
    if (o.memoryKiB < minKiB || o.memoryKiB > maxKiB) {
        throw_error(ERR_OPTION_RANGE, "The option \"memory\" (KiB) is out of range.");
        return false;
    }
    memlimit = (size_t)(o.memoryKiB * 1024);
    opslimit = (unsigned long long)o.iterations;
    return true;
}

// Parses the optional options object. Returns false (after throwing) on failure.
static bool read_options(PA_PluginParameters params, short index, Argon2Options &opts) {
    PA_ObjectRef obj = PA_GetObjectParameter(params, index);
    if (!obj) return true;

    PA_Variable arg[1];
    memset(arg, 0, sizeof(arg));
    PA_SetObjectVariable(&arg[0], obj);
    PA_Variable keysVar = PA_ExecuteCommandByID(CMD_OB_KEYS, arg, 1);
    PA_CollectionRef keys = PA_GetCollectionVariable(keysVar);
    if (!keys) {
        throw_error(ERR_BAD_OPTIONS, "The options parameter must be an object.");
        return false;
    }

    bool ok = true;
    PA_long32 count = PA_GetCollectionLength(keys);
    for (PA_long32 i = 0; i < count && ok; ++i) {
        PA_Variable keyVar = PA_GetCollectionElement(keys, i);
        PA_Unistring key = PA_GetStringVariable(keyVar);
        if (utf16_equals_ascii(&key, "memory")) {
            ok = read_uint_option(obj, &key, "memory", opts.memoryKiB);
        } else if (utf16_equals_ascii(&key, "iterations")) {
            ok = read_uint_option(obj, &key, "iterations", opts.iterations);
        } else {
            throw_error(ERR_OPTION_UNKNOWN, "Unknown option. Supported options: \"memory\", \"iterations\".");
            ok = false;
        }
        PA_ClearVariable(&keyVar);
    }
    PA_ClearVariable(&keysVar);
    return ok;
}

#pragma mark - commands

// Argon2 Generate password hash(password : Text {; options : Object}) : Text
static void Argon2_Generate_password_hash(PA_PluginParameters params) {
    if (!ensure_sodium()) {
        throw_error(ERR_INIT, "libsodium could not be initialised.");
        return;
    }

    Argon2Options opts;
    size_t memlimit = 0;
    unsigned long long opslimit = 0;
    if (!read_options(params, 2, opts) || !validate_options(opts, memlimit, opslimit)) return;

    SecretBuffer pw;
    if (!read_password(params, 1, pw)) return;

    char out[crypto_pwhash_STRBYTES];
    int r = crypto_pwhash_str_alg(out, pw.data.data(), (unsigned long long)pw.length,
                                  opslimit, memlimit, crypto_pwhash_ALG_ARGON2ID13);
    if (r != 0) {
        throw_error(ERR_HASH_FAILED, "Password hashing failed (out of memory or unsupported cost parameters).");
        return;
    }

    PA_Unichar result[crypto_pwhash_STRBYTES];
    size_t n = strlen(out);
    for (size_t i = 0; i < n; ++i) result[i] = (PA_Unichar)(unsigned char)out[i];
    result[n] = 0;
    PA_ReturnString(params, result);
}

// Converts the hash text parameter to a NUL-terminated ASCII string.
// Returns false if it cannot be a valid argon2id hash.
static bool read_hash(PA_PluginParameters params, short index, char *out /* crypto_pwhash_STRBYTES */) {
    PA_Unistring *text = PA_GetStringParameter(params, index);
    if (!text) return false;
    PA_long32 len = PA_GetUnistringLength(text);
    const PA_Unichar *s = PA_GetUnistring(text);
    if (len <= 0 || len >= (PA_long32)crypto_pwhash_STRBYTES) return false;
    for (PA_long32 i = 0; i < len; ++i) {
        if (s[i] == 0 || s[i] > 0x7E || s[i] < 0x20) return false;
        out[i] = (char)s[i];
    }
    out[len] = 0;
    return strncmp(out, crypto_pwhash_argon2id_STRPREFIX, strlen(crypto_pwhash_argon2id_STRPREFIX)) == 0;
}

// Argon2 Verify password hash(password : Text ; hash : Text) : Boolean
static void Argon2_Verify_password_hash(PA_PluginParameters params) {
    if (!ensure_sodium()) {
        throw_error(ERR_INIT, "libsodium could not be initialised.");
        return_boolean(params, false);
        return;
    }

    SecretBuffer pw;
    if (!read_password(params, 1, pw)) {
        return_boolean(params, false);
        return;
    }

    char hash[crypto_pwhash_STRBYTES];
    memset(hash, 0, sizeof(hash));
    if (!read_hash(params, 2, hash)) {
        return_boolean(params, false);
        return;
    }

    int r = crypto_pwhash_str_verify(hash, pw.data.data(), (unsigned long long)pw.length);
    return_boolean(params, r == 0);
}

// Argon2 Hash needs rehash(hash : Text {; options : Object}) : Boolean
static void Argon2_Hash_needs_rehash(PA_PluginParameters params) {
    if (!ensure_sodium()) {
        throw_error(ERR_INIT, "libsodium could not be initialised.");
        return_boolean(params, false);
        return;
    }

    Argon2Options opts;
    size_t memlimit = 0;
    unsigned long long opslimit = 0;
    if (!read_options(params, 2, opts) || !validate_options(opts, memlimit, opslimit)) {
        return_boolean(params, false);
        return;
    }

    char hash[crypto_pwhash_STRBYTES];
    memset(hash, 0, sizeof(hash));
    if (!read_hash(params, 1, hash)) {
        return_boolean(params, true);
        return;
    }

    // 0: up to date, 1: needs rehash, -1: invalid
    int r = crypto_pwhash_str_needs_rehash(hash, opslimit, memlimit);
    return_boolean(params, r != 0);
}
