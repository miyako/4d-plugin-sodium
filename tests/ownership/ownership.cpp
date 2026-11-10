#include "4DPluginAPI.h"
#include "PrivateTypes.h"
#include "EntryPoints.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "sodium-4dplugin.cpp"

namespace {
using Text = std::vector<PA_Unichar>;
struct Node {
    bool collection = false;
    std::map<Text, PA_Variable> properties;
    std::vector<PA_Variable> elements;
};
std::map<PA_Unichar *, size_t> strings;
std::map<Node *, size_t> nodes;
std::map<PA_Handle, size_t> handles;
bool allocatedEmpty = true;
int failCommand = 0;
bool failProperty = false;
bool failElement = false;
bool wrongKeysKind = false;
int throwReturn = 0;
bool objectKey = false;
int observedError = 0;
int failures = 0;
int cases = 0;

void require(bool condition, const char *why) {
    if (!condition) {
        std::cerr << "contract violation: " << why << '\n';
        std::abort();
    }
}
Text text(const PA_Unistring &s) {
    if (!s.fString) {
        require(s.fLength == 0, "null nonempty string");
        return {};
    }
    PA_Unichar *pointer = s.fString;
    auto it = strings.find(pointer);
    require(it != strings.end(), "string already disposed");
    require(it->second == static_cast<size_t>(s.fLength) + 1, "unit capacity");
    require(s.fString[s.fLength] == 0, "string terminator");
    return Text(s.fString, s.fString + s.fLength);
}
PA_Unistring create(const Text &t) {
    PA_Unistring s{};
    s.fLength = static_cast<PA_long32>(t.size());
    if (!t.empty() || allocatedEmpty) {
        s.fString = new PA_Unichar[t.size() + 1];
        std::copy(t.begin(), t.end(), s.fString);
        s.fString[t.size()] = 0;
        PA_Unichar *pointer = s.fString;
        strings.emplace(pointer, t.size() + 1);
    }
    return s;
}
void dispose(PA_Unistring &s) {
    text(s);
    if (s.fString) {
        PA_Unichar *pointer = s.fString;
        require(strings.erase(pointer) == 1, "double string cleanup");
        delete[] s.fString;
    }
    s = {};
}
PA_Variable undefined() {
    PA_Variable v{};
    v.fType = eVK_Undefined;
    return v;
}
PA_Variable newNode(bool collection) {
    Node *node = new Node;
    node->collection = collection;
    nodes.emplace(node, 1);
    PA_Variable v = undefined();
    if (collection) PA_SetCollectionVariable(&v, node);
    else PA_SetObjectVariable(&v, node);
    return v;
}
PA_Variable copy(PA_Variable v) {
    if (v.fType == eVK_Unistring) v.uValue.fString = create(text(v.uValue.fString));
    if (v.fType == eVK_Object || v.fType == eVK_Collection) {
        Node *node = static_cast<Node *>(v.uValue.fObject);
        if (node) {
            require(nodes.count(node) == 1, "retain dead node");
            ++nodes.at(node);
        }
    }
    return v;
}
void clear(PA_Variable &v) {
    if (v.fType == eVK_Unistring) dispose(v.uValue.fString);
    else if (v.fType == eVK_ArrayUnicode) {
        auto *items = reinterpret_cast<PA_Unistring *>(*v.uValue.fArray.fData);
        for (PA_long32 i = 0; i <= v.uValue.fArray.fNbElements; ++i) dispose(items[i]);
        PA_DisposeHandle(v.uValue.fArray.fData);
    }
    else if (v.fType == eVK_Object || v.fType == eVK_Collection) {
        Node *node = static_cast<Node *>(v.uValue.fObject);
        if (node) {
            require(nodes.count(node) == 1, "release dead node");
            if (--nodes.at(node) == 0) {
                for (auto &entry : node->properties) clear(entry.second);
                for (auto &element : node->elements) clear(element);
                nodes.erase(node);
                delete node;
            }
        }
    } else {
        require(v.fType == eVK_Undefined || v.fType == eVK_Real ||
                v.fType == eVK_Longint || v.fType == eVK_Integer ||
                v.fType == eVK_Boolean, "clear unspecified return");
    }
    v = undefined();
}
Text ascii(const char *s) {
    Text t;
    for (; *s; ++s) t.push_back(static_cast<unsigned char>(*s));
    return t;
}
void host(short selector, EngineBlock *eb) {
    eb->fError = eER_NoErr;
    switch (selector) {
        case EX_GET_INFORMATION:
            require(eb->fParam3 == 'FAST', "unexpected information query");
            eb->fParam2 = 0x2101;
            return;
        case EX_CREATE_UNISTRING: {
            const auto *s = reinterpret_cast<PA_Unichar *>(eb->fHandle);
            Text t;
            if (s) for (; *s; ++s) t.push_back(*s);
            eb->fUniString1 = create(t);
            return;
        }
        case EX_DISPOSE_UNISTRING:
            dispose(eb->fUniString1);
            return;
        case EX_SET_UNISTRING: {
            const auto *s = reinterpret_cast<PA_Unichar *>(eb->fHandle);
            Text t;
            if (s) for (; *s; ++s) t.push_back(*s);
            dispose(eb->fUniString1);
            eb->fUniString1 = create(t);
            return;
        }
        case EX_CLEAR_VARIABLE:
            clear(*static_cast<PA_Variable *>(eb->fPtr1));
            return;
        case EX_VARIABLE_TO_STRING: {
            auto *v = reinterpret_cast<PA_Variable *>(eb->fHandle);
            require(v->fType == eVK_Unistring, "unexpected text conversion");
            text(v->uValue.fString);
            eb->fUniString1 = v->uValue.fString;
            return;
        }
        case EX_HANDLE_MANAGER:
            switch (eb->fParam1) {
                case 1: {
                    auto h = new char *;
                    *h = new char[static_cast<size_t>(eb->fParam2) + 1];
                    handles.emplace(h, static_cast<size_t>(eb->fParam2));
                    eb->fHandle = h;
                    return;
                }
                case 2:
                    {
                    PA_Handle handle = eb->fHandle;
                    require(handles.erase(handle) == 1, "handle cleanup");
                    delete[] *eb->fHandle;
                    delete eb->fHandle;
                    return;
                    }
                case 3: {
                    PA_Handle handle = eb->fHandle;
                    size_t size = static_cast<size_t>(eb->fParam2);
                    char *replacement = new char[size + 1];
                    std::memcpy(replacement, *handle, std::min(size, handles.at(handle)));
                    delete[] *handle;
                    *handle = replacement;
                    handles.at(handle) = size;
                    return;
                }
                case 5:
                    {
                    PA_Handle handle = eb->fHandle;
                    require(handles.count(handle) == 1, "handle lock");
                    eb->fParam3 = reinterpret_cast<sLONG_PTR>(*eb->fHandle);
                    return;
                    }
                case 6:
                    return;
                default: require(false, "unexpected handle operation");
            }
            return;
        case EX_EXECUTE_COMMAND_BY_ID: {
            auto **args = static_cast<PA_Variable **>(eb->fPtr1);
            auto *out = static_cast<PA_Variable *>(eb->fPtr2);
            int command = static_cast<int>(eb->fParam1);
            require(command == 1471 || command == CMD_OB_KEYS || command == CMD_THROW,
                    "unexpected command");
            if (command == failCommand) {
                std::memset(out, 0xA5, sizeof(*out));
                out->fType = 0; // Unspecified failed value must not be normalized/adopted.
                eb->fError = -1;
                return;
            }
            *out = undefined();
            if (command == 1471) *out = newNode(false);
            else if (command == CMD_OB_KEYS) {
                if (wrongKeysKind) {
                    out->fType = eVK_Unistring;
                    out->uValue.fString = create(ascii("not a collection"));
                    return;
                }
                *out = newNode(true);
                auto *object = static_cast<Node *>(args[0]->uValue.fObject);
                auto *collection = static_cast<Node *>(out->uValue.fCollection);
                for (const auto &entry : object->properties) {
                    if (objectKey) {
                        collection->elements.push_back(newNode(false));
                        continue;
                    }
                    PA_Variable key = undefined();
                    key.fType = eVK_Unistring;
                    key.uValue.fString = create(entry.first);
                    collection->elements.push_back(key);
                }
            } else {
                auto *error = static_cast<Node *>(args[0]->uValue.fObject);
                for (int i = 0; i < 32; ++i) {
                    PA_Unistring churn = create(ascii("synthetic allocator churn"));
                    dispose(churn);
                }
                observedError = static_cast<int>(error->properties.at(ascii("errCode")).uValue.fReal);
                require(text(error->properties.at(ascii("componentSignature")).uValue.fString) ==
                        ascii("sodm"), "error signature retained");
                require(!text(error->properties.at(ascii("message")).uValue.fString).empty(),
                        "error message retained");
                if (throwReturn == 1) {
                    out->fType = eVK_Unistring;
                    out->uValue.fString = create(ascii("callback result"));
                } else if (throwReturn == 2 || throwReturn == 3) {
                    *out = newNode(throwReturn == 3);
                }
            }
            return;
        }
        case EX_SET_OBJ_VALUE: {
            require(eb->fShort == eVK_Object, "unexpected set collection");
            auto *object = static_cast<Node *>(eb->fPtr1);
            Text key = text(eb->fUniString1);
            auto found = object->properties.find(key);
            if (found != object->properties.end()) clear(found->second);
            object->properties[key] = copy(*static_cast<PA_Variable *>(eb->fPtr2));
            return;
        }
        case EX_GET_OBJ_VALUE: {
            auto *object = static_cast<Node *>(eb->fPtr1);
            auto *out = static_cast<PA_Variable *>(eb->fPtr2);
            if ((eb->fShort == eVK_Object && failProperty) ||
                (eb->fShort == eVK_Collection && eb->fLongint >= 0 && failElement)) {
                std::memset(out, 0xA5, sizeof(*out));
                out->fType = 0;
                eb->fError = -1;
                return;
            }
            if (eb->fShort == eVK_Object) {
                *out = copy(object->properties.at(text(eb->fUniString1)));
            } else if (eb->fLongint < 0) {
                require(text(eb->fUniString1) == ascii("length"), "collection length key");
                *out = undefined();
                PA_SetLongintVariable(out, static_cast<PA_long32>(object->elements.size()));
            } else {
                *out = copy(object->elements.at(static_cast<size_t>(eb->fLongint)));
            }
            return;
        }
        default:
            std::cerr << "denied engine selector " << selector << '\n';
            std::abort();
    }
}
void finish(const std::string &name) {
    ++cases;
    if (!strings.empty() || !nodes.empty() || !handles.empty()) {
        ++failures;
        std::cout << "FAIL " << name << " strings=" << strings.size()
                  << " nodes=" << nodes.size() << " handles=" << handles.size() << '\n';
    } else std::cout << "PASS " << name << '\n';
    // Baseline leak fixtures must not contaminate the next named measurement.
    for (auto &entry : strings) delete[] entry.first;
    strings.clear();
    for (auto &entry : nodes) delete entry.first;
    nodes.clear();
    require(handles.empty(), "handle leak");
}
void option(const char *name, PA_Variable value, int expectedError = 0) {
    PA_Variable object = newNode(false);
    auto *node = static_cast<Node *>(object.uValue.fObject);
    node->properties.emplace(ascii(name), value);
    PA_ObjectRef ref = node;
    void *args[] = {nullptr, &ref};
    PluginBlock params{};
    params.fParameters = args;
    Argon2Options options;
    observedError = 0;
    bool ok = read_options(&params, 2, options);
    require(ok == (expectedError == 0), "option outcome");
    require(observedError == expectedError, "existing option error code");
    clear(object);
}
}

int main(int argc, char **argv) {
    require(argc == 3, "case and empty representation required");
    int selected = std::atoi(argv[1]);
    gCall4D = host;
    bool allocated = std::atoi(argv[2]) != 0;
    {
        allocatedEmpty = allocated;
        std::string mode = allocated ? "/allocated-empty" : "/null-empty";
        if (selected == 0) {
        for (const auto &fixture : std::vector<Text>{ascii("ASCII"), Text{0x65E5, 0x672C},
                                                   Text{0xD83D, 0xDE00}, {}}) {
            PA_Unistring original = create(fixture);
            SecretBuffer utf8;
            require(utf16_to_utf8(original.fString, original.fLength, 1024, utf8) == UTF8_OK,
                    "UTF16 conversion");
            size_t bytes = fixture.empty() ? 0 : fixture[0] == 0x65E5 ? 6 :
                           fixture[0] == 0xD83D ? 4 : 5;
            require(utf8.length == bytes && utf8.data[bytes] == 0, "UTF8 byte length/termination");
            const char *expected = fixture.empty() ? "" : fixture[0] == 0x65E5 ? "\xE6\x97\xA5\xE6\x9C\xAC" :
                                   fixture[0] == 0xD83D ? "\xF0\x9F\x98\x80" : "ASCII";
            require(std::memcmp(utf8.data.data(), expected, bytes + 1) == 0, "UTF8 content");
            dispose(original);
        }
        finish("text-units-capacity-termination" + mode);
        }

        if (selected == 1) {
        throw_error(ERR_BAD_TEXT, "Synthetic public fixture");
        require(observedError == ERR_BAD_TEXT, "throw error code");
        finish("error-temporaries" + mode);
        }

        PA_Variable number = undefined();
        PA_SetRealVariable(&number, 8);
        if (selected == 2) {
        option("memory", number);
        finish("numeric-option-control" + mode);
        }

        if (selected == 3) {
        PA_Variable string = undefined();
        string.fType = eVK_Unistring;
        string.uValue.fString = create(ascii("not numeric"));
        option("memory", string, ERR_OPTION_TYPE);
        finish("owning-property-type-mismatch" + mode);
        }

        if (selected == 4) {
        option("unknown", number, ERR_OPTION_UNKNOWN);
        finish("unknown-option" + mode);
        }

        if (selected == 5) {
        wrongKeysKind = true;
        option("memory", number, ERR_BAD_OPTIONS);
        wrongKeysKind = false;
        finish("owning-command-kind-mismatch" + mode);
        }

        if (selected == 6) {
        failCommand = CMD_OB_KEYS;
        option("memory", number, ERR_BAD_OPTIONS);
        failCommand = 0;
        finish("failed-command-unspecified-return" + mode);
        }

        if (selected == 7) {
        failProperty = true;
        option("memory", number, ERR_OPTION_TYPE);
        failProperty = false;
        finish("failed-property-unspecified-return" + mode);
        }

        if (selected == 8) {
        failElement = true;
        option("memory", number, ERR_OPTION_UNKNOWN);
        failElement = false;
        finish("failed-element-unspecified-return" + mode);
        }

        if (selected == 9) {
        PA_Unistring invalid = create(Text{0xD800});
        PA_ObjectRef noOptions = nullptr;
        void *args[] = {&invalid, &noOptions};
        PA_Unistring result = create({});
        PA_Unistring *resultPointer = &result;
        FourDPackex(1, args, nullptr, &resultPointer);
        require(observedError == ERR_BAD_TEXT, "actual dispatcher error path");
        require(text(result).empty(), "error does not manufacture hash");
        dispose(invalid);
        dispose(result);
        finish("actual-dispatcher-invalid-unicode" + mode);
        }
        if (selected == 10) {
            failCommand = CMD_THROW;
            throw_error(ERR_BAD_TEXT, "Failed synchronous throw fixture");
            finish("failed-throw-unspecified-return" + mode);
        }
        if (selected >= 11 && selected <= 13) {
            throwReturn = selected - 10;
            throw_error(ERR_BAD_TEXT, "Owning synchronous result fixture");
            finish("owning-throw-result-" + std::to_string(throwReturn) + mode);
        }
        if (selected == 14) {
            option("memory", newNode(false), ERR_OPTION_TYPE);
            finish("owning-object-property-mismatch" + mode);
        }
        if (selected == 15) {
            objectKey = true;
            option("memory", number, ERR_OPTION_UNKNOWN);
            finish("owning-object-collection-element-mismatch" + mode);
        }
        if (selected == 16) {
            PA_Variable array = PA_CreateVariable(eVK_ArrayUnicode);
            PA_ResizeArray(&array, 1);
            PA_Unistring zero = create(ascii("zero"));
            PA_SetStringInArray(array, 0, &zero);
            PA_Unistring value = create(Text{0x65E5, 0xD83D, 0xDE00});
            PA_SetStringInArray(array, 1, &value);
            PA_Unistring borrowed = PA_GetStringInArray(array, 1);
            PA_Unistring independent = create(text(borrowed));
            PA_SetStringInArray(array, 1, &independent);
            for (int i = 0; i < 32; ++i) {
                PA_Unistring churn = create(ascii("synthetic allocator churn"));
                dispose(churn);
            }
            require(text(PA_GetStringInArray(array, 1)) == Text{0x65E5, 0xD83D, 0xDE00},
                    "borrowed getter copied before replacement");
            PA_Unistring rejected = create(ascii("caller retains rejected value"));
            PA_SetStringInArray(array, -1, &rejected);
            PA_SetStringInArray(array, 2, &rejected);
            PA_SetStringInArray(number, 0, &rejected);
            PA_Variable noData = PA_CreateVariable(eVK_Undefined);
            noData.fType = eVK_ArrayUnicode;
            noData.uValue.fArray.fData = nullptr;
            PA_SetStringInArray(noData, 0, &rejected);
            PA_SetStringInArray(array, 1, nullptr);
            require(text(rejected) == ascii("caller retains rejected value"), "rejection retains owner");
            dispose(rejected);
            PA_ResizeArray(&array, 0);
            require(text(PA_GetStringInArray(array, 0)) == ascii("zero"), "index zero retained");
            clear(array);
            finish("sdk-array-transfer-rejection-borrow-resize-control" + mode);
        }
        if (selected == 17) {
            option("memory", newNode(true), ERR_OPTION_TYPE);
            finish("owning-collection-property-mismatch" + mode);
        }
        if (selected == 18) {
            SecretBuffer buffer;
            Text limit(1024, 'x');
            require(utf16_to_utf8(limit.data(), 1024, 1024, buffer) == UTF8_OK &&
                    buffer.length == 1024 && buffer.data[1024] == 0, "1024 byte boundary");
            limit.push_back('x');
            require(utf16_to_utf8(limit.data(), 1025, 1024, buffer) == UTF8_TOO_LONG,
                    "1025 byte rejection");
            Text supplementary;
            for (int i = 0; i < 256; ++i) {
                supplementary.push_back(0xD83D);
                supplementary.push_back(0xDE00);
            }
            require(utf16_to_utf8(supplementary.data(), 512, 1024, buffer) == UTF8_OK &&
                    buffer.length == 1024, "supplementary boundary uses bytes not units");
            PA_Unichar low = 0xDC00;
            require(utf16_to_utf8(&low, 1, 1024, buffer) == UTF8_INVALID, "lone low surrogate");
            finish("password-byte-boundaries-control" + mode);
        }
    }
    std::cout << "cases=" << cases << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
