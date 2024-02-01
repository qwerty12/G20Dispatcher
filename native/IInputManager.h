#ifndef IINPUT_MANAGER_H
#define IINPUT_MANAGER_H

#include <binder/IInterface.h>
#include <input/Input.h>
#include <input/InputDevice.h>

enum InputEventInjectionSync {
  NONE = 0,
  WAIT_FOR_RESULT = 1,
  WAIT_FOR_FINISHED = 2,
};

class IInputManager: public android::IInterface
{
    public:
        DECLARE_META_INTERFACE(InputManager);

        enum {
            INJECT_INPUT_EVENT = android::IBinder::FIRST_CALL_TRANSACTION + 7,
        };

        virtual bool injectInputEvent(InputEvent &ev, InputEventInjectionSync mode) = 0;
};

#endif // IINPUT_MANAGER_H

