@echo off
setlocal
pushd "%~dp0"
call "%USERPROFILE%\scoop\persist\android-clt\ndk\25.1.8937393\toolchains\llvm\prebuilt\windows-x86_64\bin\armv7a-linux-androideabi31-clang++.cmd" -s -pie -Wall -Wno-misleading-indentation -O3 -march=armv7-a -mthumb -fno-rtti -fno-exceptions -nostdlib++ -std=c++17 -fvisibility=hidden -fvisibility-inlines-hidden -fdata-sections -ffunction-sections -Wl,--gc-sections -D_LIBCPP_ABI_NAMESPACE=__1 -DDO_NOT_CHECK_MANUAL_BINDER_INTERFACES -DANDROID -DNDEBUG -isystem ./extra_ndk/include -L./extra_ndk/lib -lc++ -lutils -lbinder -linput -o ../app/src/main/jniLibs/armeabi-v7a/libg20dispatcher.so g20dispatcher.c BinderGlue.cpp IsKodiTopmostApp.cpp
popd