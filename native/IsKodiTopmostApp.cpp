#include <utils/String8.h>
#include <utils/String16.h>
#include <binder/Parcel.h>
#include <binder/IBinder.h>
#include <binder/IInterface.h>
#include <binder/IServiceManager.h>

#include "IsKodiTopmostApp.h"

class IActivityTaskManager: public android::IInterface
{
    public:
        DECLARE_META_INTERFACE(ActivityTaskManager);

        enum {
            GET_TASKS = android::IBinder::FIRST_CALL_TRANSACTION + 21,
        };

        virtual bool getTasks(int maxNum, bool filterOnlyVisibleRecents, bool keepIntentExtra, android::Parcel *reply) = 0;
};

class BpActivityTaskManager: public android::BpInterface<IActivityTaskManager>
{
    public:
        explicit BpActivityTaskManager(const android::sp<android::IBinder> &impl)
            : android::BpInterface<IActivityTaskManager>(impl)
        {
        }

        virtual bool getTasks(int maxNum, bool filterOnlyVisibleRecents, bool keepIntentExtra, android::Parcel *reply) override
        {
            android::Parcel data;
            data.writeInterfaceToken(IActivityTaskManager::getInterfaceDescriptor());
            data.writeInt32(maxNum);
            data.writeBool(filterOnlyVisibleRecents);
            data.writeBool(keepIntentExtra);
            return remote()->transact(GET_TASKS, data, reply) == android::OK;
        }
};
IMPLEMENT_META_INTERFACE(ActivityTaskManager, "android.app.IActivityTaskManager");

static android::sp<IActivityTaskManager> activity_task = nullptr;

class ActivityTaskDeathRecipient : public android::IBinder::DeathRecipient
{
    public:
        virtual void binderDied(const android::wp<android::IBinder> &who) override
        {
            activity_task = nullptr;
        }
};
const static android::sp<ActivityTaskDeathRecipient> gDeathNotifier(new ActivityTaskDeathRecipient);

static bool connectActivityTaskService()
{
    const android::sp<android::IServiceManager> sm = android::defaultServiceManager();
    if (!sm)
        return false;

    const android::sp<android::IBinder> binder = sm->getService(android::String16("activity_task"));
    if (!binder)
        return false;

    activity_task = android::interface_cast<IActivityTaskManager>(binder);
    if (!activity_task)
        return false;

    binder->linkToDeath(gDeathNotifier);

    return true;
}

bool IsKodiTopmostApp()
{
    static const android::StaticString16 kodiBaseActivity = android::StaticString16(u"org.xbmc.kodi");
    static const size_t kodiBaseActivitySize = kodiBaseActivity.size();
    android::Parcel reply;

    if (!activity_task && !connectActivityTaskService())
        return false;

    if (!activity_task->getTasks(1, false, false, &reply))
        return false;

    // List shit
    int32_t status = reply.readInt32();
    if (status != android::OK)
        return false;
    int32_t length = reply.readInt32();
    if (length != 1)
        return false;
    int32_t nullcheck = reply.readInt32();
    if (!nullcheck)
        return false;

    /*reply.readInt32(); // RunningTaskInfo.id
    reply.readInt32(); // TaskInfo.userId
    reply.readInt32(); // TaskInfo.taskId
    reply.readInt32(); // TaskInfo.displayId */
    reply.setDataPosition(reply.dataPosition() + (sizeof(int32_t) * 4));
    if (!reply.readBool()) // TaskInfo.isRunning
        return false;
    if (reply.readBool()) { // TypedObject "not null" boolean
        // Intent
        /*const char *baseIntent =*/ reply.readString8(); // mAction

        // Trying to parse an Intent Parcel seems
        // incredibly time-consuming on account of 
        // how extremely varying it is...
        // So I won't. But I will write very unsafe
        // code that makes a lot of assumptions.    

        // Right at the very end of the Intent
        // are mContentUserHint and mExtras 
        // mContentUserHint is always likely
        // to be -2 (UserHandle.USER_CURRENT)
        // mExtras should always be an empty
        // Bundle because keepIntentExtra is
        // false

        // Check to see if there's room enough
        // for at least four integers:
        // mContentUserHint, the empty mExtras
        // length of a potential String16 and
        // a String16 with at least two characters
        do {
            if (reply.readInt32() == -2 && reply.readInt32() == 0xffffffff)
                goto readbaseActivityProbably;
        } while (reply.dataAvail() >= sizeof(int32_t) * 4);

        return false;
    }
    readbaseActivityProbably:
    int32_t stringLengthMaybe = 0;
    if (reply.readInt32(&stringLengthMaybe) != android::OK || stringLengthMaybe != kodiBaseActivitySize)
        return false;
    reply.setDataPosition(reply.dataPosition() - sizeof(int32_t));

    android::String16 baseActivity;
    if (reply.readString16(&baseActivity) == android::OK)
        return baseActivity == kodiBaseActivity;

    return false;
}
