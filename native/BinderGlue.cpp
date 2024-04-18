#include <stdlib.h>
#include <sys/cdefs.h>
#include <binder/IPCThreadState.h>
#include <utils/SystemClock.h>
#include <utils/String16.h>
#include <binder/IBinder.h>
#include <binder/IServiceManager.h>

#include "IInputManager.h"
#include "BinderGlue.h"

constexpr int32_t DEFAULT_DEVICE_ID = -1; // android.view.KeyCharacterMap.VIRTUAL_KEYBOARD
constexpr uint64_t GLOBAL_ACTIONS_KEY_TIMEOUT = 500;
constexpr int32_t FLAG_LONG_PRESS = 0x00000080;
//constexpr int PARCEL_TOKEN_MOTION_EVENT = 1;
constexpr int PARCEL_TOKEN_KEY_EVENT = 2;

class BpInputManager: public android::BpInterface<IInputManager>
{
    public:
        explicit BpInputManager(const android::sp<android::IBinder> &impl)
            : android::BpInterface<IInputManager>(impl)
        {
        }

        virtual bool injectInputEvent(InputEvent &ev, InputEventInjectionSync mode) override
        {
            android::Parcel data, reply;
            data.writeInterfaceToken(IInputManager::getInterfaceDescriptor());
            data.writeInt32(1); // prepare write object
            const int type = ev.getType();
            switch(type)
            {
                case AINPUT_EVENT_TYPE_KEY:
                    writeKeyEventToParcel((KeyEvent &) ev, data);
                    break;
                /*case AINPUT_EVENT_TYPE_MOTION:
                    writeMotionEventToParcel((MotionEvent &) ev, data);
                    break;*/
                default:
                    //ALOGE("unknown input type: %d", type);
                    return false;
            }
            data.writeInt32(mode);
            remote()->transact(INJECT_INPUT_EVENT, data, &reply);
            return reply.readBool();
        }

    private:
        static void writeKeyEventToParcel(KeyEvent& ev, Parcel& data)
        {
            data.writeInt32(PARCEL_TOKEN_KEY_EVENT);

            data.writeInt32(ev.getId());
            data.writeInt32(ev.getDeviceId());
            data.writeInt32(ev.getSource());
            data.writeInt32(ev.getDisplayId());
            data.writeByteVector(std::vector<uint8_t>(ev.getHmac().begin(), ev.getHmac().end()));
            data.writeInt32(ev.getAction());
            data.writeInt32(ev.getKeyCode());
            data.writeInt32(ev.getRepeatCount());
            data.writeInt32(ev.getMetaState());
            data.writeInt32(ev.getScanCode());
            data.writeInt32(ev.getFlags());
            data.writeInt64(ev.getDownTime());
            data.writeInt64(ev.getEventTime());
        }

        /*static void writeMotionEventToParcel(MotionEvent& ev, Parcel& data)
        {
            data.writeInt32(PARCEL_TOKEN_MOTION_EVENT);
            ev.writeToParcel(&data);
        }*/
};
IMPLEMENT_META_INTERFACE(InputManager, "android.hardware.input.IInputManager");

static android::sp<IInputManager> input = nullptr;
static InputEventFactoryInterface *inputEventFactory = new PreallocatedInputEventFactory();

class InputDeathRecipient : public android::IBinder::DeathRecipient
{
    public:
        virtual void binderDied(const android::wp<android::IBinder> &who) override
        {
            input = nullptr;
        }
};
const static android::sp<InputDeathRecipient> gDeathNotifier(new InputDeathRecipient);

void OnBinderReadReady()
{
    android::IPCThreadState::self()->handlePolledCommands();
}

int SetupBinderOrCrash()
{
    int binder_fd = -1;
    android::ProcessState::self()->setThreadPoolMaxThreadCount(0);
    android::IPCThreadState::self()->disableBackgroundScheduling(true);
    const int err = android::IPCThreadState::self()->setupPolling(&binder_fd);
    if (err) {
        fprintf(stderr, "Error setting up binder polling: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }

    if (binder_fd < 0) {
        fprintf(stderr, "Invalid binder FD: %d\n", binder_fd);
        exit(EXIT_FAILURE);
    }

    return binder_fd;
}

bool connectInputService()
{
    const sp<android::IServiceManager> sm = android::defaultServiceManager();
    if (!sm)
        return false;

    const android::sp<android::IBinder> binder = sm->getService(android::String16("input"));
    if (!binder)
        return false;

    input = android::interface_cast<IInputManager>(binder);
    if (__predict_false(!input))
        return false;

    binder->linkToDeath(gDeathNotifier);

    return true;
}

void injectInputEvent(int32_t keyCode, KeyPressMode mode)
{
    if (__predict_false(!input && !connectInputService()))
        return;

    KeyEvent *ev = inputEventFactory->createKeyEvent();
    const uint64_t now = uptimeMillis();
    ev->initialize(
            0/*id*/,
            DEFAULT_DEVICE_ID /*deviceId*/,
            InputDevice::SOURCE_KEYBOARD /*source*/,
            0 /*displayId*/,
            { {} } /*std::array<uint8_t, 32> hmac*/,
            AKEY_EVENT_ACTION_DOWN /* action*/,
            0 /*flags*/,
            keyCode /*keyCode*/,
            0 /*scanCode*/,
            0 /*metaState*/,
            0 /*repeatCount*/,
            now /*downTime*/,
            now /*eventTime*/);
    input->injectInputEvent(*ev, InputEventInjectionSync::WAIT_FOR_FINISHED);

    if (mode == KEYPRESS_LONG_PRESS) {
        const uint64_t nextEventTime = now + GLOBAL_ACTIONS_KEY_TIMEOUT;
        ev->initialize(0, DEFAULT_DEVICE_ID, InputDevice::SOURCE_KEYBOARD, 0, { {} }, AKEY_EVENT_ACTION_UP, FLAG_LONG_PRESS, keyCode, 0, 0, 1, now, nextEventTime);
    } else {
        ev->initialize(0, DEFAULT_DEVICE_ID, InputDevice::SOURCE_KEYBOARD, 0, { {} }, AKEY_EVENT_ACTION_UP, 0, keyCode, 0, 0, 0, now, now);
    }
    input->injectInputEvent(*ev, InputEventInjectionSync::NONE);
}