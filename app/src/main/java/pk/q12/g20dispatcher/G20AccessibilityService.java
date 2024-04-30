package pk.q12.g20dispatcher;

import android.accessibilityservice.AccessibilityService;
import android.content.Context;
import android.content.Intent;
import android.os.FileObserver;
import android.os.SystemClock;
import android.os.SystemProperties;
import android.view.KeyEvent;
import android.view.accessibility.AccessibilityEvent;

import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Set;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

import org.lsposed.hiddenapibypass.HiddenApiBypass;

import io.github.muntashirakon.adb.AbsAdbConnectionManager;
import io.github.muntashirakon.adb.AdbStream;
import io.github.muntashirakon.adb.LocalServices;
import io.github.muntashirakon.adb.android.AndroidUtils;

public final class G20AccessibilityService extends AccessibilityService {
    private static final String DAEMON_TMPDIR = "/data/local/tmp/.g20dispatcher";
    private static final String DAEMON_BASENAME = "libg20dispatcher.so";
    private static final String DAEMON_TERMINATION_COMMAND = "killall " + DAEMON_BASENAME;
    private static final Set<Integer> blockedScanCodes = Set.of(
        370, // KEY_SUBTITLE
        358, // KEY_INFO
        398, // KEY_RED
        399, // KEY_GREEN
        400, // KEY_YELLOW
        401, // KEY_BLUE
        384, // KEY_TAPE
        240, // Input, YouTube and Netflix
        230, // KEY_KBDILLUMUP (Prime Video)
        229  // KEY_KBDILLUMDOWN (Google Play)
    );
    private final File fileG20 = new File(DAEMON_TMPDIR + "/");
    private ThreadPoolExecutor executor = null;
    private FileObserver fileObserverG20 = null;
    private boolean isProbablyRunning = false;

    @Override
    protected void attachBaseContext(final Context newBase) {
        super.attachBaseContext(newBase);
        HiddenApiBypass.addHiddenApiExemptions("Lcom/android/org/conscrypt/Conscrypt");
    }

    @Override
    protected void onServiceConnected() {
        super.onServiceConnected();

        isProbablyRunning = false;
        executor = (ThreadPoolExecutor) Executors.newFixedThreadPool(1);
        fileObserverG20 = new FileObserver(fileG20, FileObserver.CLOSE_WRITE) {
            @Override
            public void onEvent(int event, String path) {
                if (event != CLOSE_WRITE || !"sentinel".equals(path))
                    return;

                isProbablyRunning = false;
                stopWatching();
                try {
                    Thread.sleep(5000);
                } catch (final InterruptedException e) {
                    return;
                }

                if (fileObserverG20 != null)
                    startG20Dispatcher();
            }
        };

        startG20Dispatcher();
    }

    private void startG20Dispatcher() {
        executor.execute(() -> {
            if (SystemClock.uptimeMillis() < 100000) {
                try {
                    Thread.sleep(35000);
                } catch (final InterruptedException e) {
                    return;
                }
            }

            for (int i = 0; i < 60; ++i) {
                if (executor.isShutdown())
                    return;

                if (adbRunning())
                    break;

                try {
                    Thread.sleep(1000);
                } catch (final InterruptedException e) {
                    return;
                }
            }
            if (!adbRunning())
                return;

            AbsAdbConnectionManager manager = null;
            AdbStream adbShellStream = null;
            try {
                manager = AdbConnectionManager.getInstance(getApplication());
                manager.setTimeout(1, TimeUnit.MINUTES);
                if (!manager.connect(AndroidUtils.getHostIpAddress(getApplication()), 5555))
                    throw new IOException("ADB connection failed");

                adbShellStream = manager.openStream(LocalServices.SHELL);

                if (executor.isShutdown())
                    return;

                try (final OutputStream os = adbShellStream.openOutputStream()) {
                    final String stringBuilder =
                            DAEMON_TERMINATION_COMMAND
                            + ";rm -rf " + DAEMON_TMPDIR + "/*"
                            + ";mkdir " + DAEMON_TMPDIR
                            + ";chmod 775 " + DAEMON_TMPDIR + "/"
                            + "&&exec " + getApplicationContext().getApplicationInfo().nativeLibraryDir + "/" + DAEMON_BASENAME + "\n";
                    os.write(stringBuilder.getBytes(StandardCharsets.UTF_8));
                }
                adbShellStream.flush();
            } catch (final Throwable th) {
                th.printStackTrace();
                return;
            } finally {
                if (adbShellStream != null) {
                    try {
                        adbShellStream.close();
                    } catch (final IOException ignored) {}
                }

                if (manager != null) {
                    try {
                        manager.disconnect();
                    } catch (final IOException ignored) {}
                }
            }

            // Yeah... You can't use FileObserver if the target doesn't
            // already exist, and this block otherwise seemed to be ran
            // before ADB finished
            for (int i = 0; i < 60; ++i) {
                if (executor.isShutdown())
                    return;

                try {
                    Thread.sleep(1000);
                } catch (final InterruptedException e) {
                    return;
                }

                if (fileG20.isDirectory()) {
                    isProbablyRunning = true;

                    if (executor.isShutdown())
                        return;

                    if (fileObserverG20 != null)
                        fileObserverG20.startWatching();

                    break;
                }
            }
        });
    }

    @Override
    protected boolean onKeyEvent(final KeyEvent event) {
        return event.getKeyCode() == KeyEvent.KEYCODE_UNKNOWN && blockedScanCodes.contains(event.getScanCode());
    }

    @Override
    public boolean onUnbind(final Intent intent) {
        if (fileObserverG20 != null) {
            fileObserverG20.stopWatching();
            fileObserverG20 = null;
        }

        final boolean executorActive = executor != null && executor.getActiveCount() != 0;
        if (isProbablyRunning || executorActive) {
            if (executorActive) { // not reliable, I know
                boolean shutdown;
                executor.shutdown();
                try {
                    shutdown = executor.awaitTermination(5, TimeUnit.SECONDS);
                } catch (final InterruptedException e) {
                    shutdown = true;
                }
                if (!shutdown)
                    executor.shutdownNow();
                executor = null;
            }

            if (adbRunning()) {
                final Thread thread = new Thread(() -> {
                    try {
                        final AbsAdbConnectionManager manager = AdbConnectionManager.getInstance(getApplication());
                        manager.setTimeout(5, TimeUnit.SECONDS);
                        if (!manager.connect(AndroidUtils.getHostIpAddress(getApplication()), 5555))
                            return;

                        manager.openStream("exec:" + DAEMON_TERMINATION_COMMAND).close();
                        manager.disconnect();
                    } catch (final Throwable ignored) {}
                });
                thread.start();
                try {
                    thread.join(5000);
                } catch (final InterruptedException ignored) {}
                if (thread.isAlive())
                    thread.interrupt();
            }
        }

        return super.onUnbind(intent);
    }

    private static boolean adbRunning() {
        return "running".equals(SystemProperties.get("init.svc.adbd"));
    }

    @Override
    public void onAccessibilityEvent(final AccessibilityEvent event) {}
    @Override
    public void onInterrupt() {}
}
