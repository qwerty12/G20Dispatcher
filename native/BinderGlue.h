#ifndef BINDERGLUE_H
#define BINDERGLUE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

enum KeyPressMode {
    KEYPRESS_NORMAL,
    KEYPRESS_DOUBLE_TAP,
    KEYPRESS_LONG_PRESS
};

void OnBinderReadReady();
int SetupBinderOrCrash();
bool connectInputService();
void injectInputEvent(int32_t keyCode, KeyPressMode mode);

#ifdef __cplusplus
}
#endif

#endif /*BINDERGLUE_H*/